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
target_compile_options(serializer_test PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
add_test(NAME gateway_serializer COMMAND serializer_test)
