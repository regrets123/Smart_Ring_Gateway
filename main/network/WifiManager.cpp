#include "network/WifiManager.h"

#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

namespace gateway {

namespace {
constexpr EventBits_t kConnected = BIT0;
constexpr EventBits_t kFailed = BIT1;
constexpr const char* kTag = "wifi";
}

void WifiManager::on_event(void* arg, esp_event_base_t base, int32_t id, void* data) {
    auto& self = *static_cast<WifiManager*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* event = static_cast<const wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(kTag, "Wi-Fi disconnected, reason=%u", static_cast<unsigned>(event->reason));
        xEventGroupClearBits(self.events_, kConnected);
        if (self.retries_++ < 5) esp_wifi_connect();
        else xEventGroupSetBits(self.events_, kFailed);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* event = static_cast<ip_event_got_ip_t*>(data);
        self.retries_ = 0;
        xEventGroupClearBits(self.events_, kFailed);
        xEventGroupSetBits(self.events_, kConnected);
        ESP_LOGI(kTag, "IP address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

esp_err_t WifiManager::connect() {
    if (CONFIG_GATEWAY_WIFI_SSID[0] == '\0') return ESP_ERR_INVALID_ARG;
    wifi_config_t config{};
    const auto ssid_length = std::strlen(CONFIG_GATEWAY_WIFI_SSID);
    const auto password_length = std::strlen(CONFIG_GATEWAY_WIFI_PASSWORD);
    if (ssid_length > sizeof(config.sta.ssid) ||
        password_length > sizeof(config.sta.password)) return ESP_ERR_INVALID_ARG;
    if (!initialized_) {
        events_ = xEventGroupCreate();
        if (!events_) return ESP_ERR_NO_MEM;
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        if (!esp_netif_create_default_wifi_sta()) return ESP_ERR_NO_MEM;
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&init));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, this, &wifi_handler_));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, this, &ip_handler_));
        std::memcpy(config.sta.ssid, CONFIG_GATEWAY_WIFI_SSID, ssid_length);
        std::memcpy(config.sta.password, CONFIG_GATEWAY_WIFI_PASSWORD, password_length);
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
        initialized_ = true;
        ESP_ERROR_CHECK(esp_wifi_start());
    } else if (!(xEventGroupGetBits(events_) & kConnected)) {
        retries_ = 0;
        xEventGroupClearBits(events_, kFailed);
        esp_wifi_connect();
    }
    const auto bits = xEventGroupWaitBits(events_, kConnected | kFailed, pdFALSE,
                                         pdFALSE, pdMS_TO_TICKS(30000));
    return (bits & kConnected) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t WifiManager::disconnect() {
    if (!initialized_) return ESP_ERR_INVALID_STATE;
    // Prevent the disconnect notification from immediately reconnecting.
    retries_ = 5;
    return esp_wifi_disconnect();
}

}  // namespace gateway
