idf_component_register(
    SRCS
        "main.cpp"
        "ble/RingBleClient.cpp"
        "protocol/ColmiProtocol.cpp"
        "protocol/BigDataReassembler.cpp"
        "protocol/RingParsers.cpp"
        "network/WifiManager.cpp"
        "network/MqttPublisher.cpp"
    INCLUDE_DIRS "."
)
