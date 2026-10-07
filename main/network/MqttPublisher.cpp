#include "network/MqttPublisher.h"

#include <cstring>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "sdkconfig.h"

extern const char mqtt_ca_pem_start[] asm("_binary_mqtt_ca_pem_start");

namespace gateway {

namespace {
constexpr EventBits_t kConnected = BIT0;
constexpr const char* kTag = "mqtt";
} // namespace

void MqttPublisher::on_event(void* arg, esp_event_base_t, int32_t id, void* data) {
    auto& self = *static_cast<MqttPublisher*>(arg);
    auto* event = static_cast<esp_mqtt_event_t*>(data);
    if (id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(self.events_, kConnected);
        ESP_LOGI(kTag, "Broker connected");
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(self.events_, kConnected);
        ESP_LOGW(kTag, "Broker disconnected; client will reconnect");
    } else if (id == MQTT_EVENT_PUBLISHED) {
        ESP_LOGI(kTag, "Broker acknowledged message %d (not a database commit)", event->msg_id);
    } else if (id == MQTT_EVENT_ERROR) {
        ESP_LOGW(kTag, "MQTT connection/transport error");
    }
}

esp_err_t MqttPublisher::sync_clock() {
    if (clock_synced_) {
        return ESP_OK;
    }
    if (!sntp_initialized_) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        const esp_err_t result = esp_netif_sntp_init(&config);
        if (result != ESP_OK) {
            return result;
        }
        sntp_initialized_ = true;
    }
    ESP_LOGI(kTag, "Waiting for clock synchronization before TLS");
    const esp_err_t result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000));
    if (result == ESP_OK) {
        clock_synced_ = true;
        ESP_LOGI(kTag, "Clock synchronized");
    } else {
        ESP_LOGW(kTag, "Clock synchronization failed: %s", esp_err_to_name(result));
    }
    return result;
}

esp_err_t MqttPublisher::connect() {
    if (CONFIG_GATEWAY_MQTT_URI[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    const bool use_tls = std::strncmp(CONFIG_GATEWAY_MQTT_URI, "mqtts://", 8) == 0;
    if (use_tls) {
        const esp_err_t result = sync_clock();
        if (result != ESP_OK) {
            return result;
        }
    }
    if (!client_) {
        events_ = xEventGroupCreate();
        if (!events_) {
            return ESP_ERR_NO_MEM;
        }
        esp_mqtt_client_config_t config{};
        config.broker.address.uri = CONFIG_GATEWAY_MQTT_URI;
        config.credentials.client_id = CONFIG_GATEWAY_GATEWAY_ID;
        if (CONFIG_GATEWAY_MQTT_USERNAME[0] != '\0') {
            config.credentials.username = CONFIG_GATEWAY_MQTT_USERNAME;
            config.credentials.authentication.password = CONFIG_GATEWAY_MQTT_PASSWORD;
        }
        if (use_tls) {
            config.broker.verification.certificate = mqtt_ca_pem_start;
        }
        client_ = esp_mqtt_client_init(&config);
        if (!client_) {
            vEventGroupDelete(events_);
            events_ = nullptr;
            return ESP_ERR_NO_MEM;
        }
        ESP_ERROR_CHECK(esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &on_event, this));
        ESP_ERROR_CHECK(esp_mqtt_client_start(client_));
    }
    const auto bits =
        xEventGroupWaitBits(events_, kConnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    return (bits & kConnected) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t MqttPublisher::publish(const char* topic, const char* json) {
    if (!topic || !json || !client_) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!(xEventGroupGetBits(events_) & kConnected)) {
        return ESP_ERR_INVALID_STATE;
    }
    const int id = esp_mqtt_client_publish(client_, topic, json, 0, 1, 0);
    if (id < 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(kTag, "Submitted message %d", id);
    ESP_LOGI(kTag, "topic is: %s", topic);
    return ESP_OK;
}

esp_err_t MqttPublisher::disconnect() {
    if (!client_) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = esp_mqtt_client_stop(client_);
    if (result != ESP_OK) {
        return result;
    }
    result = esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    vEventGroupDelete(events_);
    events_ = nullptr;
    return result;
}

} // namespace gateway
