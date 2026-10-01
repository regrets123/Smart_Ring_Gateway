idf_component_register(
    SRCS
        "main.cpp"
        "ble/RingBleClient.cpp"
        "ble/RingPeerMatch.cpp"
        "protocol/ColmiProtocol.cpp"
        "protocol/BigDataProtocol.cpp"
        "probe/RingProbe.cpp"
        "probe/ProbeDate.cpp"
        "protocol/RingParsers.cpp"
        "serialization/RingJson.cpp"
        "network/WifiManager.cpp"
        "network/MqttPublisher.cpp"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "certs/mqtt_ca.pem"
    PRIV_REQUIRES esp_wifi esp_event esp_netif nvs_flash mbedtls bt
)
