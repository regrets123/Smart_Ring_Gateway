#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "models/MockData.h"
#include "network/MqttPublisher.h"
#include "network/WifiManager.h"
#include "serialization/RingJson.h"

extern "C" void app_main(void) {
#if CONFIG_GATEWAY_MOCK_ENABLED
    constexpr const char* tag = "mock_gateway";
    if (CONFIG_GATEWAY_WIFI_SSID[0] == '\0' || CONFIG_GATEWAY_MQTT_URI[0] == '\0' ||
        CONFIG_GATEWAY_MQTT_TOPIC[0] == '\0') {
        ESP_LOGW(tag, "Set Wi-Fi, broker and topic under menuconfig -> Gateway MQTT publishing");
        return;
    }
    // Do not erase existing NVS automatically if initialization fails.
    ESP_ERROR_CHECK(nvs_flash_init());
    static gateway::WifiManager wifi;
    static gateway::MqttPublisher mqtt;
    gateway::RingJson serializer;
    const gateway::MockReading mock{CONFIG_GATEWAY_DEVICE_ID,
                                   CONFIG_GATEWAY_GATEWAY_ID,
                                   CONFIG_GATEWAY_USER_ID,
                                   CONFIG_GATEWAY_MOCK_READING};
    std::string payload;
    ESP_ERROR_CHECK(serializer.serialize(mock, payload));
    ESP_LOGI(tag, "Mock source -> JSON: %s", payload.c_str());
    ESP_LOGI(tag, "Publish topic: %s", CONFIG_GATEWAY_MQTT_TOPIC);
    for (;;) {
        esp_err_t result = wifi.connect();
        if (result == ESP_OK) result = mqtt.connect();
        if (result == ESP_OK) result = mqtt.publish(CONFIG_GATEWAY_MQTT_TOPIC, payload.c_str());
        if (result != ESP_OK) ESP_LOGW(tag, "Retry next interval: %s", esp_err_to_name(result));
        vTaskDelay(pdMS_TO_TICKS(CONFIG_GATEWAY_MOCK_INTERVAL_SECONDS * 1000));
    }
#else
    ESP_LOGI("smart_ring_gateway", "Mock disabled; real BLE gateway remains unimplemented");
#endif
}
