cmake_minimum_required(VERSION 3.24)
project(gateway_serializer_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_MakeAvailable(json)

enable_testing()
add_executable(serializer_test serializer.cpp ../main/serialization/RingJson.cpp)
target_include_directories(serializer_test PRIVATE compat ../main)
target_link_libraries(serializer_test PRIVATE nlohmann_json::nlohmann_json)
if(MSVC)
    target_compile_options(serializer_test PRIVATE /W4 /EHsc /GR-)
else()
    target_compile_options(serializer_test PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
endif()
add_test(NAME gateway_serializer COMMAND serializer_test)

add_executable(protocol_probe_test protocol_probe.cpp
    ../main/protocol/ColmiProtocol.cpp ../main/protocol/BigDataProtocol.cpp
    ../main/ble/RingPeerMatch.cpp ../main/probe/RingProbe.cpp
    ../main/probe/ProbeDate.cpp)
target_include_directories(protocol_probe_test PRIVATE compat ../main)
if(MSVC)
    target_compile_options(protocol_probe_test PRIVATE /W4)
else()
    target_compile_options(protocol_probe_test PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
endif()
add_test(NAME gateway_protocol_probe COMMAND protocol_probe_test)
