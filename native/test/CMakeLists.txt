# Debug info for a Release build (default MSVC Release flags omit it) --
# needed for OpenCppCoverage (scripts/test.js's --coverage step) to map
# instruction hits back to source lines/PDBs. Scoped to this test-only
# CMakeLists via add_compile_options/add_link_options (CMake applies both
# only to targets added afterward, and link options aren't used for static
# libraries), so it never touches the production addon/kernel-exe/supervisor
# targets built from native/CMakeLists.txt.
if (MSVC)
    add_compile_options(/Zi /FS)
    add_link_options(/DEBUG /INCREMENTAL:NO)
endif ()

add_executable(message_test message_test.cpp)

target_compile_features(message_test PRIVATE cxx_std_23)

target_include_directories(message_test PRIVATE 
    ${JOVIAN_INCLUDE_DIR}
    ${JOVIAN_SOURCE_DIR}
)

# We need to link against the core sources to test them
# Since the (now removed) Node addon was a MODULE library, we couldn't link against it directly
# We'll create a static library for the core components to link against in tests
add_library(adrastea_core STATIC ${ADRASTEA_CORE_SOURCES} ${ADRASTEA_PLATFORM_SOURCES} ${ADRASTEA_UTILS_SOURCES})
target_compile_features(adrastea_core PRIVATE cxx_std_23)
target_include_directories(adrastea_core PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR} ${R_INCLUDE_DIR})
target_link_libraries(adrastea_core PRIVATE nlohmann_json::nlohmann_json cppzmq OpenSSL::Crypto ${R_LIBRARIES})
# ADRASTEA_API expands to __declspec(dllimport) on Windows unless this is
# defined, which breaks linking here since adrastea_core is a static lib,
# not the DLL that dllimport expects to find these symbols in.
target_compile_definitions(adrastea_core PUBLIC ADRASTEA_STATIC_LIB)

if (MSVC)
    target_compile_definitions(adrastea_core PRIVATE -DNOMINMAX)
    target_compile_options(adrastea_core PRIVATE /DGUID_WINDOWS /MP /bigobj)
    target_compile_options(adrastea_core PRIVATE /wd4251 /wd4996)
elseif (APPLE)
    target_compile_definitions(adrastea_core PRIVATE -DGUID_CFUUID)
    # See the matching comment on jovian_apply_platform_guid_define
    # (native/CMakeLists.txt) -- guid.cpp's CFUUID calls need CoreFoundation.
    target_link_libraries(adrastea_core PRIVATE "-framework CoreFoundation")
else ()
    target_compile_definitions(adrastea_core PRIVATE -DGUID_LIBUUID)
    # See the matching comment on jovian_apply_platform_guid_define
    # (native/CMakeLists.txt) -- guid.cpp's uuid_generate() needs libuuid.
    find_package(LibUUID REQUIRED)
    target_link_libraries(adrastea_core PRIVATE LibUUID::LibUUID)
endif ()

find_package(GTest REQUIRED)

target_link_libraries(message_test PRIVATE
    adrastea_core
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

add_test(NAME MessageTest COMMAND message_test)

# ==========================================
# SUPERVISOR TESTS
# ==========================================
# Same ADRASTEA_STATIC_LIB reasoning as adrastea_core above, applied to
# the supervisor's own sources (message.cpp/kernel_configuration.cpp for the
# messaging layer, the client-side transport, and SessionRegistry/
# KernelProcess themselves) -- deliberately excludes http_api.cpp/
# ws_relay.cpp/main.cpp, since this test drives SessionRegistry directly
# rather than through HTTP/WS (that path is covered by
# test/integration/session-manager.test.ts on the TypeScript side).
add_library(themisto_core STATIC
    ${ADRASTEA_MESSAGING_SOURCES}
    ${ADRASTEA_TRANSPORT_CLIENT_SOURCES}
    ${ADRASTEA_PLATFORM_SOURCES}
    ${JOVIAN_SOURCE_DIR}/supervisor/session_registry.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/session_registry.hpp
    ${JOVIAN_SOURCE_DIR}/supervisor/kernel_process.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/kernel_process.hpp
)
target_compile_features(themisto_core PRIVATE cxx_std_23)
target_include_directories(themisto_core PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(themisto_core PRIVATE nlohmann_json::nlohmann_json cppzmq OpenSSL::Crypto)
target_compile_definitions(themisto_core PUBLIC ADRASTEA_STATIC_LIB)

if (MSVC)
    target_compile_definitions(themisto_core PRIVATE -DNOMINMAX)
    target_compile_options(themisto_core PRIVATE /DGUID_WINDOWS /MP /bigobj)
    target_compile_options(themisto_core PRIVATE /wd4251 /wd4996)
elseif (APPLE)
    target_compile_definitions(themisto_core PRIVATE -DGUID_CFUUID)
    # See the matching comment on jovian_apply_platform_guid_define
    # (native/CMakeLists.txt) -- guid.cpp's CFUUID calls need CoreFoundation.
    target_link_libraries(themisto_core PRIVATE "-framework CoreFoundation")
else ()
    target_compile_definitions(themisto_core PRIVATE -DGUID_LIBUUID)
    # See the matching comment on jovian_apply_platform_guid_define
    # (native/CMakeLists.txt) -- guid.cpp's uuid_generate() needs libuuid.
    find_package(LibUUID REQUIRED)
    target_link_libraries(themisto_core PRIVATE LibUUID::LibUUID)
endif ()

add_executable(session_registry_test session_registry_test.cpp)
target_compile_features(session_registry_test PRIVATE cxx_std_23)
target_include_directories(session_registry_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(session_registry_test PRIVATE
    themisto_core
    GTest::gtest
    nlohmann_json::nlohmann_json
)

# Only wire up the real kernel exe path if the target actually exists in
# this configure (JOVIAN_BUILD_ELARA defaults ON, but this guards
# against a configure where it was explicitly turned off) -- the test skips
# itself at runtime (GTEST_SKIP) when this isn't defined.
if (TARGET elara)
    target_compile_definitions(session_registry_test PRIVATE
        "ELARA_TEST_KERNEL_EXE=\"$<TARGET_FILE:elara>\"")
endif ()

# R_HOME here is the same variable find_package(R REQUIRED) in the root
# CMakeLists.txt already resolved for this machine (cmake/FindR.cmake) --
# reusing it instead of guessing at a path keeps the test portable across
# whichever R installation/OS actually built this configure.
if (R_HOME)
    target_compile_definitions(session_registry_test PRIVATE
        "ELARA_TEST_R_HOME=\"${R_HOME}\"")
endif ()

add_test(NAME SessionRegistryTest COMMAND session_registry_test)
# In practice this suite runs in ~2s; 60s is a wide margin over that,
# not a tuned expectation -- this only needs to catch the case where
# something is genuinely hung (see the no-timeout limitation on
# ClientHandshakeZmqImpl::waitForConfiguration() noted in
# session_registry.cpp) rather than the happy path's normal latency.
set_tests_properties(SessionRegistryTest PROPERTIES TIMEOUT 60)

# ==========================================
# PURE-FUNCTION / UNIT-LEVEL TESTS
# ==========================================
# No R, no spawned process, no ZMQ sockets -- these run in milliseconds and
# don't need SessionRegistryTest's fixture, R detection, or kernel exe.

add_executable(middleware_test middleware_test.cpp)
target_compile_features(middleware_test PRIVATE cxx_std_23)
target_include_directories(middleware_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(middleware_test PRIVATE themisto_core GTest::gtest_main)
add_test(NAME MiddlewareTest COMMAND middleware_test)

add_executable(authentication_test authentication_test.cpp)
target_compile_features(authentication_test PRIVATE cxx_std_23)
target_include_directories(authentication_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(authentication_test PRIVATE themisto_core GTest::gtest_main)
add_test(NAME AuthenticationTest COMMAND authentication_test)

add_executable(zmq_serializer_test zmq_serializer_test.cpp)
target_compile_features(zmq_serializer_test PRIVATE cxx_std_23)
target_include_directories(zmq_serializer_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(zmq_serializer_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ZmqSerializerTest COMMAND zmq_serializer_test)

add_executable(kernel_configuration_test kernel_configuration_test.cpp)
target_compile_features(kernel_configuration_test PRIVATE cxx_std_23)
target_include_directories(kernel_configuration_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(kernel_configuration_test PRIVATE themisto_core GTest::gtest_main)
add_test(NAME KernelConfigurationTest COMMAND kernel_configuration_test)

# Standalone dummy helper kernel_process_test.cpp spawns instead of a real
# elara.exe -- see dummy_process_helper.cpp's own comment for why.
add_executable(dummy_process_helper dummy_process_helper.cpp)
target_compile_features(dummy_process_helper PRIVATE cxx_std_23)

add_executable(kernel_process_test kernel_process_test.cpp)
target_compile_features(kernel_process_test PRIVATE cxx_std_23)
target_include_directories(kernel_process_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(kernel_process_test PRIVATE themisto_core GTest::gtest_main)
target_compile_definitions(kernel_process_test PRIVATE
    "THEMISTO_TEST_DUMMY_PROCESS_EXE=\"$<TARGET_FILE:dummy_process_helper>\"")
add_test(NAME KernelProcessTest COMMAND kernel_process_test)

# Drives ClientZmqImpl/ClientZmq/DealerChannel/ClientIopub against a
# hand-rolled fake kernel (bare ROUTER/PUB sockets) -- no R, no spawned
# process -- to reach poll()/waitForMessage()/listener-callback paths that
# SessionRegistry itself never exercises (see client_zmq_test.cpp's own
# comment for why).
add_executable(client_zmq_test client_zmq_test.cpp)
target_compile_features(client_zmq_test PRIVATE cxx_std_23)
target_include_directories(client_zmq_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(client_zmq_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ClientZmqTest COMMAND client_zmq_test)

# ClientHeartbeat in isolation, with short custom timeout/max_retry values
# -- see client_heartbeat_test.cpp's own comment for why this can't be
# exercised through ClientZmqImpl's hardcoded 20s/3-retry constants.
add_executable(client_heartbeat_test client_heartbeat_test.cpp)
target_compile_features(client_heartbeat_test PRIVATE cxx_std_23)
target_include_directories(client_heartbeat_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(client_heartbeat_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ClientHeartbeatTest COMMAND client_heartbeat_test)

add_executable(client_handshake_zmq_test client_handshake_zmq_test.cpp)
target_compile_features(client_handshake_zmq_test PRIVATE cxx_std_23)
target_include_directories(client_handshake_zmq_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(client_handshake_zmq_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ClientHandshakeZmqTest COMMAND client_handshake_zmq_test)
