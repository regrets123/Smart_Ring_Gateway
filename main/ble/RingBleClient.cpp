#include "ble/RingBleClient.h"

#include <cstring>
#include <initializer_list>

#include "ble/RingPeerMatch.h"
#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

namespace gateway {
namespace {
constexpr EventBits_t kSynced = BIT0;
constexpr EventBits_t kConnected = BIT1;
constexpr EventBits_t kFailed = BIT2;
constexpr EventBits_t kProcedureDone = BIT3;
constexpr EventBits_t kDisconnected = BIT4;
constexpr uint32_t kGattTimeoutMs = 10000;
constexpr char kTag[] = "ring_ble";

esp_err_t from_nimble(int rc) { return rc == 0 ? ESP_OK : ESP_FAIL; }
} // namespace

RingBleClient* RingBleClient::instance_ = nullptr;

void RingBleClient::on_sync() {
    if (instance_ && instance_->events_) {
        xEventGroupSetBits(instance_->events_, kSynced);
    }
}

void RingBleClient::host_task(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

RingBleClient::ChannelHandles& RingBleClient::handles(RingChannel channel) {
    return channel == RingChannel::command ? command_ : big_data_;
}

const RingBleClient::ChannelHandles& RingBleClient::handles(RingChannel channel) const {
    return channel == RingChannel::command ? command_ : big_data_;
}

bool RingBleClient::uuid_equals(const ble_uuid_t* actual, const ble_uuid_any_t& expected) const {
    return actual && ble_uuid_cmp(actual, &expected.u) == 0;
}

int RingBleClient::on_gap(ble_gap_event* event, void* arg) {
    auto& self = *static_cast<RingBleClient*>(arg);
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (self.connecting_) {
            break;
        }
        const auto& disc = event->disc;
        if (!RingPeerMatch::matches(disc.data, disc.length_data, self.exact_name_,
                                    self.optional_address_, disc.addr.val)) {
            break;
        }
        self.connecting_ = true;
        ESP_LOGI(kTag, "Matched %s addr=%02x:%02x:%02x:%02x:%02x:%02x RSSI=%d", self.exact_name_,
                 disc.addr.val[5], disc.addr.val[4], disc.addr.val[3], disc.addr.val[2],
                 disc.addr.val[1], disc.addr.val[0], disc.rssi);
        ble_gap_disc_cancel();
        uint8_t own_addr_type = 0;
        int rc = ble_hs_id_infer_auto(0, &own_addr_type);
        if (!rc) {
            rc = ble_gap_connect(own_addr_type, &disc.addr, 20000, nullptr, on_gap, &self);
        }
        if (rc) {
            self.procedure_status_ = rc;
            xEventGroupSetBits(self.events_, kFailed);
        }
        break;
    }
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            self.conn_ = event->connect.conn_handle;
            self.connected_.store(true);
            xEventGroupSetBits(self.events_, kConnected);
        } else {
            self.procedure_status_ = event->connect.status;
            xEventGroupSetBits(self.events_, kFailed);
        }
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (!self.connecting_) {
            xEventGroupSetBits(self.events_, kFailed);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        self.connected_.store(false);
        self.conn_ = BLE_HS_CONN_HANDLE_NONE;
        self.command_.subscribed = false;
        self.big_data_.subscribed = false;
        xEventGroupSetBits(self.events_, kDisconnected | kProcedureDone);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        const size_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        RingNotification notification;
        if (length > sizeof(notification.bytes)) {
            self.lost_.fetch_add(1);
            break;
        }
        if (event->notify_rx.attr_handle == self.command_.notify_value) {
            notification.channel = RingChannel::command;
        } else if (event->notify_rx.attr_handle == self.big_data_.notify_value) {
            notification.channel = RingChannel::big_data;
        } else {
            break;
        }
        notification.length = length;
        if (os_mbuf_copydata(event->notify_rx.om, 0, length, notification.bytes) != 0 ||
            xQueueSend(self.notifications_, &notification, 0) != pdTRUE) {
            self.lost_.fetch_add(1);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

int RingBleClient::on_service(uint16_t, const ble_gatt_error* error, const ble_gatt_svc* service,
                              void* arg) {
    auto& self = *static_cast<RingBleClient*>(arg);
    if (error->status == 0 && service) {
        ChannelHandles* target = nullptr;
        if (self.uuid_equals(&service->uuid.u, self.command_service_uuid_)) {
            target = &self.command_;
        }
        if (self.uuid_equals(&service->uuid.u, self.big_service_uuid_)) {
            target = &self.big_data_;
        }
        if (target) {
            target->service_start = service->start_handle;
            target->service_end = service->end_handle;
        }
        if (ble_uuid_u16(&service->uuid.u) == 0x180a) {
            self.device_info_start_ = service->start_handle;
            self.device_info_end_ = service->end_handle;
        }
    } else {
        self.finish_procedure(error->status);
    }
    return 0;
}

int RingBleClient::on_characteristic(uint16_t, const ble_gatt_error* error, const ble_gatt_chr* chr,
                                     void* arg) {
    auto& self = *static_cast<RingBleClient*>(arg);
    if (error->status == 0 && chr) {
        if (self.characteristic_count_ < sizeof(self.characteristic_defs_) / sizeof(uint16_t)) {
            self.characteristic_defs_[self.characteristic_count_++] = chr->def_handle;
        }
        auto& target = self.handles(self.discovering_);
        const auto& write_uuid = self.discovering_ == RingChannel::command
                                     ? self.command_write_uuid_
                                     : self.big_write_uuid_;
        const auto& notify_uuid = self.discovering_ == RingChannel::command
                                      ? self.command_notify_uuid_
                                      : self.big_notify_uuid_;
        if (self.uuid_equals(&chr->uuid.u, write_uuid)) {
            target.write_value = chr->val_handle;
            target.write_properties = chr->properties;
        }
        if (self.uuid_equals(&chr->uuid.u, notify_uuid)) {
            target.notify_value = chr->val_handle;
            target.notify_properties = chr->properties;
        }
    } else {
        self.finish_procedure(error->status);
    }
    return 0;
}

int RingBleClient::on_descriptor(uint16_t, const ble_gatt_error* error, uint16_t,
                                 const ble_gatt_dsc* dsc, void* arg) {
    auto& self = *static_cast<RingBleClient*>(arg);
    if (error->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            self.handles(self.discovering_).cccd = dsc->handle;
        }
    } else {
        self.finish_procedure(error->status);
    }
    return 0;
}

int RingBleClient::on_attribute(uint16_t, const ble_gatt_error* error, ble_gatt_attr* attr,
                                void* arg) {
    auto& self = *static_cast<RingBleClient*>(arg);
    if (self.reading_ && error->status == 0 && attr && attr->om) {
        const size_t length = OS_MBUF_PKTLEN(attr->om);
        if (length > sizeof(self.read_buffer_) ||
            os_mbuf_copydata(attr->om, 0, length, self.read_buffer_) != 0) {
            self.procedure_status_ = BLE_HS_EMSGSIZE;
        } else {
            self.read_length_ = length;
        }
        return 0;
    }
    self.procedure_status_ = error->status == BLE_HS_EDONE ? self.procedure_status_ : error->status;
    xEventGroupSetBits(self.events_, kProcedureDone);
    return 0;
}

esp_err_t RingBleClient::wait_procedure(int start_rc, uint32_t timeout_ms) {
    if (start_rc != 0) {
        return ESP_FAIL;
    }
    const auto bits = xEventGroupWaitBits(events_, kProcedureDone | kDisconnected, pdTRUE, pdFALSE,
                                          pdMS_TO_TICKS(timeout_ms));
    if (bits & kDisconnected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!(bits & kProcedureDone)) {
        return ESP_ERR_TIMEOUT;
    }
    return procedure_status_ == 0 ? ESP_OK : ESP_FAIL;
}

void RingBleClient::prepare_procedure() {
    procedure_status_ = 0;
    xEventGroupClearBits(events_, kProcedureDone);
}

void RingBleClient::finish_procedure(int status) {
    procedure_status_ = status == BLE_HS_EDONE ? 0 : status;
    xEventGroupSetBits(events_, kProcedureDone);
}

esp_err_t RingBleClient::discover_channel(RingChannel channel) {
    discovering_ = channel;
    auto& target = handles(channel);
    if (!target.service_start) {
        return ESP_OK;
    }
    characteristic_count_ = 0;
    prepare_procedure();
    esp_err_t result =
        wait_procedure(ble_gattc_disc_all_chrs(conn_, target.service_start, target.service_end,
                                               on_characteristic, this),
                       kGattTimeoutMs);
    if (result != ESP_OK || !target.notify_value) {
        return result;
    }
    uint16_t end = target.service_end;
    for (size_t i = 0; i < characteristic_count_; ++i) {
        if (characteristic_defs_[i] > target.notify_value && characteristic_defs_[i] - 1 < end) {
            end = characteristic_defs_[i] - 1;
        }
    }
    if (end <= target.notify_value) {
        return ESP_OK;
    }
    prepare_procedure();
    return wait_procedure(
        ble_gattc_disc_all_dscs(conn_, target.notify_value, end, on_descriptor, this),
        kGattTimeoutMs);
}

esp_err_t RingBleClient::discover() {
    prepare_procedure();
    esp_err_t result =
        wait_procedure(ble_gattc_disc_all_svcs(conn_, on_service, this), kGattTimeoutMs);
    if (result != ESP_OK) {
        return result;
    }
    result = discover_channel(RingChannel::command);
    if (result != ESP_OK) {
        return result;
    }
    result = discover_channel(RingChannel::big_data);
    ESP_LOGI(kTag, "GATT command=%s big_data=%s device_info=%s",
             command_.service_start ? "found" : "missing",
             big_data_.service_start ? "found" : "missing",
             device_info_start_ ? "found" : "missing");
    return result;
}

esp_err_t RingBleClient::open(const char* exact_name, const char* optional_address,
                              uint32_t timeout_ms) {
    if (!exact_name || !*exact_name || connected_.load()) {
        return ESP_ERR_INVALID_ARG;
    }
    exact_name_ = exact_name;
    optional_address_ = optional_address;
    if (!events_) {
        events_ = xEventGroupCreate();
    }
    if (!notifications_) {
        notifications_ = xQueueCreate(24, sizeof(RingNotification));
    }
    if (!events_ || !notifications_) {
        return ESP_ERR_NO_MEM;
    }
    struct UuidSpec {
        ble_uuid_any_t* value;
        const char* text;
    };
    const UuidSpec uuids[] = {
        {&command_service_uuid_, "6e40fff0-b5a3-f393-e0a9-e50e24dcca9e"},
        {&command_write_uuid_, "6e400002-b5a3-f393-e0a9-e50e24dcca9e"},
        {&command_notify_uuid_, "6e400003-b5a3-f393-e0a9-e50e24dcca9e"},
        {&big_service_uuid_, "de5bf728-d711-4e47-af26-65e3012a5dc7"},
        {&big_write_uuid_, "de5bf72a-d711-4e47-af26-65e3012a5dc7"},
        {&big_notify_uuid_, "de5bf729-d711-4e47-af26-65e3012a5dc7"},
    };
    for (const auto& item : uuids) {
        if (ble_uuid_from_str(item.value, item.text) != 0) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!initialized_) {
        instance_ = this;
        if (nimble_port_init() != ESP_OK) {
            return ESP_FAIL;
        }
        ble_hs_cfg.sync_cb = on_sync;
        nimble_port_freertos_init(host_task);
        initialized_ = true;
    }
    auto bits = xEventGroupWaitBits(events_, kSynced, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if (!(bits & kSynced)) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t own_addr_type = 0;
    if (ble_hs_id_infer_auto(0, &own_addr_type) != 0) {
        return ESP_FAIL;
    }
    ble_gap_disc_params params{};
    params.passive = 0;
    params.filter_duplicates = 0;
    connecting_ = false;
    xEventGroupClearBits(events_, kConnected | kFailed | kDisconnected);
    int rc = ble_gap_disc(own_addr_type, timeout_ms, &params, on_gap, this);
    if (rc != 0) {
        return ESP_FAIL;
    }
    bits = xEventGroupWaitBits(events_, kConnected | kFailed, pdTRUE, pdFALSE,
                               pdMS_TO_TICKS(timeout_ms + 20000));
    if (!(bits & kConnected)) {
        return (bits & kFailed) ? ESP_FAIL : ESP_ERR_TIMEOUT;
    }
    return discover();
}

esp_err_t RingBleClient::subscribe() {
    if (!connected_.load()) {
        return ESP_ERR_INVALID_STATE;
    }
    for (auto channel : {RingChannel::command, RingChannel::big_data}) {
        auto& target = handles(channel);
        if (!target.write_value || !target.notify_value || !target.cccd ||
            !(target.notify_properties & BLE_GATT_CHR_F_NOTIFY) ||
            !(target.write_properties & (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP))) {
            continue;
        }
        const uint8_t enabled[] = {1, 0};
        reading_ = false;
        prepare_procedure();
        const auto result = wait_procedure(
            ble_gattc_write_flat(conn_, target.cccd, enabled, sizeof(enabled), on_attribute, this),
            kGattTimeoutMs);
        target.subscribed = result == ESP_OK;
        ESP_LOGI(kTag, "Subscribe %s: %s", channel == RingChannel::command ? "command" : "big_data",
                 target.subscribed ? "ok" : esp_err_to_name(result));
    }
    return command_.subscribed || big_data_.subscribed ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool RingBleClient::has_channel(RingChannel channel) const {
    return connected_.load() && handles(channel).subscribed;
}

esp_err_t RingBleClient::write(RingChannel channel, const uint8_t* bytes, size_t length) {
    if (!has_channel(channel)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!bytes || !length || length > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    const auto& target = handles(channel);
    if (target.write_properties & BLE_GATT_CHR_F_WRITE_NO_RSP) {
        return from_nimble(ble_gattc_write_no_rsp_flat(conn_, target.write_value, bytes,
                                                       static_cast<uint16_t>(length)));
    }
    reading_ = false;
    prepare_procedure();
    return wait_procedure(ble_gattc_write_flat(conn_, target.write_value, bytes,
                                               static_cast<uint16_t>(length), on_attribute, this),
                          kGattTimeoutMs);
}

esp_err_t RingBleClient::receive(RingNotification& notification, uint32_t timeout_ms) {
    if (xQueueReceive(notifications_, &notification, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }
    return connected_.load() ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
}

esp_err_t RingBleClient::read_device_info(uint16_t characteristic_uuid, uint8_t* out,
                                          size_t capacity, size_t& length) {
    length = 0;
    if (!connected_.load()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!device_info_start_) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!out || !capacity) {
        return ESP_ERR_INVALID_ARG;
    }
    ble_uuid16_t uuid{};
    uuid.u.type = BLE_UUID_TYPE_16;
    uuid.value = characteristic_uuid;
    reading_ = true;
    read_length_ = 0;
    prepare_procedure();
    const auto result =
        wait_procedure(ble_gattc_read_by_uuid(conn_, device_info_start_, device_info_end_, &uuid.u,
                                              on_attribute, this),
                       kGattTimeoutMs);
    reading_ = false;
    if (procedure_status_ == BLE_HS_ATT_ERR(BLE_ATT_ERR_ATTR_NOT_FOUND)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (result != ESP_OK || !read_length_) {
        return result == ESP_OK ? ESP_ERR_NOT_FOUND : result;
    }
    if (read_length_ > capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(out, read_buffer_, read_length_);
    length = read_length_;
    return ESP_OK;
}

esp_err_t RingBleClient::disconnect() {
    if (!connected_.load()) {
        return ESP_OK;
    }
    return from_nimble(ble_gap_terminate(conn_, BLE_ERR_REM_USER_CONN_TERM));
}

} // namespace gateway
