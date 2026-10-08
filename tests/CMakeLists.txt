cmake_minimum_required(VERSION 3.24)
project(gateway_protocol_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

function(configure_host_test target)
    target_include_directories(${target} PRIVATE compat ../main)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
    endif()
endfunction()

add_executable(protocol_probe_test protocol_probe.cpp
    ../main/protocol/ColmiProtocol.cpp ../main/protocol/BigDataProtocol.cpp
    ../main/ble/RingPeerMatch.cpp ../main/probe/RingProbe.cpp
    ../main/probe/ProbeDate.cpp)
configure_host_test(protocol_probe_test)
add_test(NAME gateway_protocol_probe COMMAND protocol_probe_test)

add_executable(hrv_history_test hrv_history.cpp
    ../main/protocol/ColmiProtocol.cpp ../main/protocol/BigDataProtocol.cpp
    ../main/protocol/RingParsers.cpp ../main/serialization/RingJson.cpp)
configure_host_test(hrv_history_test)
target_include_directories(hrv_history_test PRIVATE
    ../managed_components/johboh__nlohmann-json/include)
add_test(NAME gateway_hrv_history COMMAND hrv_history_test)
