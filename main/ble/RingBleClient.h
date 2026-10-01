#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ble/RingTransport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"

namespace gateway {

class RingBleClient final : public IRingTransport {
public:
    esp_err_t open(const char* exact_name, const char* optional_address,
                   uint32_t timeout_ms);
    esp_err_t subscribe();
    bool has_channel(RingChannel channel) const override;
    esp_err_t write(RingChannel channel, const uint8_t* bytes, size_t length) override;
    esp_err_t receive(RingNotification& notification, uint32_t timeout_ms) override;
    esp_err_t read_device_info(uint16_t characteristic_uuid, uint8_t* out,
                               size_t capacity, size_t& length) override;
    uint32_t lost_notifications() const override { return lost_.load(); }
    bool is_connected() const override { return connected_.load(); }
    esp_err_t disconnect() override;

private:
    struct ChannelHandles {
        uint16_t service_start = 0;
        uint16_t service_end = 0;
        uint16_t write_value = 0;
        uint16_t notify_value = 0;
        uint16_t cccd = 0;
        uint8_t write_properties = 0;
        uint8_t notify_properties = 0;
        bool subscribed = false;
    };

    static RingBleClient* instance_;
    static void on_sync();
    static void host_task(void*);
    static int on_gap(struct ble_gap_event* event, void* arg);
    static int on_service(uint16_t conn, const struct ble_gatt_error* error,
                          const struct ble_gatt_svc* service, void* arg);
    static int on_characteristic(uint16_t conn, const struct ble_gatt_error* error,
                                 const struct ble_gatt_chr* chr, void* arg);
    static int on_descriptor(uint16_t conn, const struct ble_gatt_error* error,
                             uint16_t chr_val, const struct ble_gatt_dsc* dsc, void* arg);
    static int on_attribute(uint16_t conn, const struct ble_gatt_error* error,
                            struct ble_gatt_attr* attr, void* arg);

    esp_err_t discover();
    esp_err_t discover_channel(RingChannel channel);
    esp_err_t wait_procedure(int start_rc, uint32_t timeout_ms);
    ChannelHandles& handles(RingChannel channel);
    const ChannelHandles& handles(RingChannel channel) const;
    bool uuid_equals(const ble_uuid_t* actual, const ble_uuid_any_t& expected) const;

    EventGroupHandle_t events_ = nullptr;
    QueueHandle_t notifications_ = nullptr;
    std::atomic<uint32_t> lost_{0};
    std::atomic<bool> connected_{false};
    bool initialized_ = false;
    bool connecting_ = false;
    const char* exact_name_ = nullptr;
    const char* optional_address_ = nullptr;
    uint16_t conn_ = BLE_HS_CONN_HANDLE_NONE;
    int procedure_status_ = 0;
    RingChannel discovering_ = RingChannel::command;
    uint16_t characteristic_defs_[32]{};
    size_t characteristic_count_ = 0;
    uint16_t device_info_start_ = 0;
    uint16_t device_info_end_ = 0;
    uint8_t read_buffer_[256]{};
    size_t read_length_ = 0;
    bool reading_ = false;
    ChannelHandles command_{};
    ChannelHandles big_data_{};
    ble_uuid_any_t command_service_uuid_{};
    ble_uuid_any_t command_write_uuid_{};
    ble_uuid_any_t command_notify_uuid_{};
    ble_uuid_any_t big_service_uuid_{};
    ble_uuid_any_t big_write_uuid_{};
    ble_uuid_any_t big_notify_uuid_{};
};

}  // namespace gateway
