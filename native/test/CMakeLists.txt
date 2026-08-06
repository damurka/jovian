add_executable(message_test message_test.cpp)

target_compile_features(message_test PRIVATE cxx_std_23)

target_include_directories(message_test PRIVATE 
    ${DATASUITE_INCLUDE_DIR}
    ${DATASUITE_SOURCE_DIR}
)

# We need to link against the core sources to test them
# Since datasuite_addon is a MODULE library, we can't link against it directly
# We'll create a static library for the core components to link against in tests
add_library(datasuite_core STATIC ${DATASUITE_CORE_SOURCES} ${DATASUITE_PLATFORM_SOURCES} ${DATASUITE_UTILS_SOURCES})
target_compile_features(datasuite_core PRIVATE cxx_std_23)
target_include_directories(datasuite_core PRIVATE ${DATASUITE_INCLUDE_DIR} ${DATASUITE_SOURCE_DIR} ${R_INCLUDE_DIR})
target_link_libraries(datasuite_core PRIVATE nlohmann_json::nlohmann_json cppzmq OpenSSL::Crypto ${R_LIBRARIES})
# DATASUITE_API expands to __declspec(dllimport) on Windows unless this is
# defined, which breaks linking here since datasuite_core is a static lib,
# not the DLL that dllimport expects to find these symbols in.
target_compile_definitions(datasuite_core PUBLIC DATASUITE_STATIC_LIB)

if (MSVC)
    target_compile_definitions(datasuite_core PRIVATE -DNOMINMAX)
    target_compile_options(datasuite_core PRIVATE /DGUID_WINDOWS /MP /bigobj)
    target_compile_options(datasuite_core PRIVATE /wd4251 /wd4996)
elseif (APPLE)
    target_compile_definitions(datasuite_core PRIVATE -DGUID_CFUUID)
else ()
    target_compile_definitions(datasuite_core PRIVATE -DGUID_LIBUUID)
endif ()

find_package(GTest REQUIRED)

target_link_libraries(message_test PRIVATE
    datasuite_core
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

add_test(NAME MessageTest COMMAND message_test)

# ==========================================
# SUPERVISOR TESTS
# ==========================================
# Same DATASUITE_STATIC_LIB reasoning as datasuite_core above, applied to
# the supervisor's own sources (message.cpp/kernel_configuration.cpp for the
# messaging layer, the client-side transport, and SessionRegistry/
# KernelProcess themselves) -- deliberately excludes http_api.cpp/
# ws_relay.cpp/main.cpp, since this test drives SessionRegistry directly
# rather than through HTTP/WS (that path is covered by
# test/integration/session-manager.test.ts on the TypeScript side).
add_library(datasuite_supervisor_core STATIC
    ${DATASUITE_MESSAGING_SOURCES}
    ${DATASUITE_TRANSPORT_CLIENT_SOURCES}
    ${DATASUITE_PLATFORM_SOURCES}
    ${DATASUITE_SOURCE_DIR}/supervisor/session_registry.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/session_registry.hpp
    ${DATASUITE_SOURCE_DIR}/supervisor/kernel_process.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/kernel_process.hpp
)
target_compile_features(datasuite_supervisor_core PRIVATE cxx_std_23)
target_include_directories(datasuite_supervisor_core PRIVATE ${DATASUITE_INCLUDE_DIR} ${DATASUITE_SOURCE_DIR})
target_link_libraries(datasuite_supervisor_core PRIVATE nlohmann_json::nlohmann_json cppzmq OpenSSL::Crypto)
target_compile_definitions(datasuite_supervisor_core PUBLIC DATASUITE_STATIC_LIB)

if (MSVC)
    target_compile_definitions(datasuite_supervisor_core PRIVATE -DNOMINMAX)
    target_compile_options(datasuite_supervisor_core PRIVATE /DGUID_WINDOWS /MP /bigobj)
    target_compile_options(datasuite_supervisor_core PRIVATE /wd4251 /wd4996)
elseif (APPLE)
    target_compile_definitions(datasuite_supervisor_core PRIVATE -DGUID_CFUUID)
else ()
    target_compile_definitions(datasuite_supervisor_core PRIVATE -DGUID_LIBUUID)
endif ()

add_executable(session_registry_test session_registry_test.cpp)
target_compile_features(session_registry_test PRIVATE cxx_std_23)
target_include_directories(session_registry_test PRIVATE ${DATASUITE_INCLUDE_DIR} ${DATASUITE_SOURCE_DIR})
target_link_libraries(session_registry_test PRIVATE
    datasuite_supervisor_core
    GTest::gtest
    nlohmann_json::nlohmann_json
)

# Only wire up the real kernel exe path if the target actually exists in
# this configure (DATASUITE_BUILD_KERNEL_EXE defaults ON, but this guards
# against a configure where it was explicitly turned off) -- the test skips
# itself at runtime (GTEST_SKIP) when this isn't defined.
if (TARGET datasuite-r)
    target_compile_definitions(session_registry_test PRIVATE
        "DATASUITE_TEST_KERNEL_EXE=\"$<TARGET_FILE:datasuite-r>\"")
endif ()

add_test(NAME SessionRegistryTest COMMAND session_registry_test)
# Generous but bounded: a real R startup + registration handshake can
# legitimately take several seconds; this only needs to catch the case
# where something is genuinely hung (see the no-timeout limitation on
# ClientHandshakeZmqImpl::waitForConfiguration() noted in
# session_registry.cpp) rather than the happy path's normal latency.
set_tests_properties(SessionRegistryTest PROPERTIES TIMEOUT 180)
