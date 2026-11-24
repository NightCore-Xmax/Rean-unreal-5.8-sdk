# ReanCore: the engine-independent part of the REAN SDK for Unreal, built and
# tested without Unreal. The sources live in the plugin
# (ReanSDK/Source/ReanCore) so Unreal and these tests compile the same files.
#
#   cmake -S ReanCore -B build && cmake --build build && ctest --test-dir build --output-on-failure
#
# The library is compiled the way Unreal compiles plugin code: no exceptions,
# no RTTI, warnings as errors (including shadowed variables).
cmake_minimum_required(VERSION 3.16)
project(ReanCore VERSION 0.1.0 LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REAN_CORE_TESTS "Build the ReanCore tests" ON)
option(REAN_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer (gcc/clang)" OFF)
option(REAN_CORE_TOOLS "Build rean_device_check (needs libcurl)" ON)

set(REAN_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ReanSDK/Source/ReanCore")

add_library(rean_core STATIC
  "${REAN_CORE_DIR}/Private/ReanClient.cpp"
  "${REAN_CORE_DIR}/Private/ReanCrypto.cpp"
  "${REAN_CORE_DIR}/Private/ReanJson.cpp"
  "${REAN_CORE_DIR}/Private/ReanQrCode.cpp"
  "${REAN_CORE_DIR}/Private/ReanText.cpp"
)
target_include_directories(rean_core
  PUBLIC "${REAN_CORE_DIR}/Public"
  PRIVATE "${REAN_CORE_DIR}/Private")

# Unreal-like compile rules, applied to everything in this project.
if(MSVC)
  set(REAN_WARNINGS /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /GR- /EHs-c-)
  set(REAN_DEFINES _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS)
else()
  set(REAN_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
                    -Wnull-dereference -Werror -fno-exceptions -fno-rtti)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Unreal's Mac/Linux toolchains are clang; these catch narrowing that MSVC also reports.
    list(APPEND REAN_WARNINGS -Wshorten-64-to-32 -Wimplicit-int-conversion)
  endif()
  set(REAN_DEFINES "")
endif()
target_compile_options(rean_core PRIVATE ${REAN_WARNINGS})
target_compile_definitions(rean_core PRIVATE ${REAN_DEFINES})

if(REAN_CORE_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
  target_compile_options(rean_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
endif()

if(REAN_CORE_TESTS)
  enable_testing()
  add_executable(rean_core_tests
    tests/test_main.cpp
    tests/test_client.cpp
    tests/test_crypto.cpp
    tests/test_json.cpp
    tests/test_qr.cpp
    tests/test_text.cpp
  )
  target_link_libraries(rean_core_tests PRIVATE rean_core)
  # Tests may use the private headers (Json, crypto, text helpers).
  target_include_directories(rean_core_tests PRIVATE "${REAN_CORE_DIR}/Private" tests)
  target_include_directories(rean_core_tests SYSTEM PRIVATE tests/third_party)
  target_compile_options(rean_core_tests PRIVATE ${REAN_WARNINGS})
  target_compile_definitions(rean_core_tests PRIVATE ${REAN_DEFINES} DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
  if(NOT MSVC)
    # doctest's own macros trip these two; the library itself is still checked.
    target_compile_options(rean_core_tests PRIVATE -Wno-old-style-cast -Wno-null-dereference)
  endif()
  if(REAN_CORE_SANITIZE AND NOT MSVC)
    target_compile_options(rean_core_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(rean_core_tests PRIVATE -fsanitize=address,undefined)
  endif()
  add_test(NAME rean_core_tests COMMAND rean_core_tests)
endif()

if(REAN_CORE_TOOLS)
  find_package(CURL QUIET)
  if(CURL_FOUND)
    # Device sign-in against the real API (or any REAN-compatible base URL).
    add_executable(rean_device_check tools/rean_device_check.cpp tools/curl_transport.cpp)
    target_link_libraries(rean_device_check PRIVATE rean_core CURL::libcurl)
    target_compile_options(rean_device_check PRIVATE ${REAN_WARNINGS})
    target_compile_definitions(rean_device_check PRIVATE ${REAN_DEFINES})
  else()
    message(STATUS "libcurl not found: rean_device_check is not built")
  endif()
endif()
