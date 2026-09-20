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

# ==========================================
# ADRASTEA TESTS
# ==========================================
add_executable(message_test adrastea/message_test.cpp)

target_compile_features(message_test PRIVATE cxx_std_23)

target_include_directories(message_test PRIVATE 
    ${JOVIAN_INCLUDE_DIR}
    ${JOVIAN_SOURCE_DIR}
)

# The (now removed) Node addon used to be a MODULE library the test suite
# couldn't link against directly; the shared `adrastea` library (native/
# CMakeLists.txt -- built once, from the exact same sources elara/themisto
# themselves link against) replaces the two separate, duplicate-compiling
# adrastea_core/themisto_core static libs this file used to build just for
# tests, now that Adrastea is a real, reusable target of its own.
find_package(GTest REQUIRED)

target_link_libraries(message_test PRIVATE
    adrastea
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

add_test(NAME MessageTest COMMAND message_test)

# ==========================================
# THEMISTO TESTS
# ==========================================
# Just SessionRegistry/KernelProcess (Themisto-specific, not part of
# Adrastea the framework) on top of `adrastea` -- deliberately excludes
# http_api.cpp/ws_relay.cpp/main.cpp, since this test drives SessionRegistry
# directly rather than through HTTP/WS (that path is covered by
# test/integration/session-manager.test.ts on the TypeScript side).
add_library(themisto_core STATIC
    ${JOVIAN_SOURCE_DIR}/themisto/session_registry.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/session_registry.hpp
    ${JOVIAN_SOURCE_DIR}/themisto/kernel_process.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/kernel_process.hpp
)
target_compile_features(themisto_core PRIVATE cxx_std_23)
# PUBLIC: everything that links themisto_core (every test target below)
# needs adrastea's own headers/compile definitions/link libraries too --
# session_registry.hpp itself includes adrastea headers directly.
target_link_libraries(themisto_core PUBLIC adrastea)

add_executable(session_registry_test themisto/session_registry_test.cpp)
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
# ELARA TESTS
# ==========================================
# elara.exe's own startup behavior, spawned directly (no SessionRegistry/
# KernelProcess involved) -- see elara_test.cpp's own comment for why.
# No adrastea/themisto_core link needed: this test only shells out to the
# built elara.exe and inspects its exit code/output, it doesn't call into
# any production code directly.
add_executable(elara_test elara/elara_test.cpp)
target_compile_features(elara_test PRIVATE cxx_std_23)
target_link_libraries(elara_test PRIVATE GTest::gtest_main)

if (TARGET elara)
    target_compile_definitions(elara_test PRIVATE
        "ELARA_TEST_KERNEL_EXE=\"$<TARGET_FILE:elara>\"")
endif ()

add_test(NAME ElaraTest COMMAND elara_test)

# ==========================================
# CARPO TESTS (see native/include/carpo/interpreter_py.hpp)
# ==========================================
# Only built/registered when JOVIAN_BUILD_CARPO is on (default OFF) -- not
# part of a normal build/test run yet, since it needs a real Python
# installation available wherever it runs (mirrors ElaraTest needing R).
if (JOVIAN_BUILD_CARPO)
    add_executable(carpo_test
        carpo/carpo_test.cpp
        ${JOVIAN_SOURCE_DIR}/carpo/interpreter_py.cpp
        ${JOVIAN_SOURCE_DIR}/carpo/py/py_dynlib.cpp
    )
    target_compile_features(carpo_test PRIVATE cxx_std_23)
    target_link_libraries(carpo_test PRIVATE
        adrastea
        GTest::gtest_main
    )
    if (NOT WIN32)
        target_link_libraries(carpo_test PRIVATE ${CMAKE_DL_LIBS})
    endif ()

    # Unlike R_HOME (found unconditionally in the root CMakeLists.txt via
    # find_package(R REQUIRED), since every other target needs it), Python
    # discovery only matters here, for this optional (JOVIAN_BUILD_CARPO
    # default OFF) test target -- so it's resolved locally instead of
    # burdening every configure with a Python dependency. CMake's own
    # FindPython3 locates an interpreter; asking THAT interpreter for its
    # own sys.prefix (rather than guessing from its executable's directory,
    # which is wrong on POSIX -- .../bin/python3's parent isn't the
    # install prefix) matches exactly what PyInterpreter needs PYTHONHOME
    # set to for that same interpreter to load correctly.
    find_package(Python3 COMPONENTS Interpreter)
    if (Python3_Interpreter_FOUND)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c "import sys; print(sys.prefix)"
            OUTPUT_VARIABLE CARPO_TEST_PYTHON_HOME
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        # sys.prefix reports backslashes on Windows -- normalized to
        # forward slashes both so it survives being embedded in a quoted
        # compile-definition string literal unescaped, and because
        # Windows' own APIs/std::filesystem accept forward slashes fine
        # (R_HOME gets the same normalization for free, since `R RHOME`
        # already reports forward slashes on every platform).
        string(REPLACE "\\" "/" CARPO_TEST_PYTHON_HOME "${CARPO_TEST_PYTHON_HOME}")
    endif ()

    if (CARPO_TEST_PYTHON_HOME)
        target_compile_definitions(carpo_test PRIVATE
            "CARPO_TEST_PYTHON_HOME=\"${CARPO_TEST_PYTHON_HOME}\"")
    endif ()

    add_test(NAME CarpoTest COMMAND carpo_test)
endif ()

# ==========================================
# ADRASTEA TESTS (continued) -- pure-function / unit-level
# ==========================================
# No R, no spawned process, no ZMQ sockets -- these run in milliseconds and
# don't need SessionRegistryTest's fixture, R detection, or kernel exe.

add_executable(middleware_test adrastea/middleware_test.cpp)
target_compile_features(middleware_test PRIVATE cxx_std_23)
target_include_directories(middleware_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(middleware_test PRIVATE themisto_core GTest::gtest_main)
add_test(NAME MiddlewareTest COMMAND middleware_test)

add_executable(authentication_test adrastea/authentication_test.cpp)
target_compile_features(authentication_test PRIVATE cxx_std_23)
target_include_directories(authentication_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(authentication_test PRIVATE themisto_core GTest::gtest_main)
add_test(NAME AuthenticationTest COMMAND authentication_test)

add_executable(zmq_serializer_test adrastea/zmq_serializer_test.cpp)
target_compile_features(zmq_serializer_test PRIVATE cxx_std_23)
target_include_directories(zmq_serializer_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(zmq_serializer_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ZmqSerializerTest COMMAND zmq_serializer_test)

add_executable(kernel_configuration_test adrastea/kernel_configuration_test.cpp)
target_compile_features(kernel_configuration_test PRIVATE cxx_std_23)
target_include_directories(kernel_configuration_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(kernel_configuration_test PRIVATE themisto_core GTest::gtest_main)
add_test(NAME KernelConfigurationTest COMMAND kernel_configuration_test)

# Standalone dummy helper kernel_process_test.cpp spawns instead of a real
# elara.exe -- see dummy_process_helper.cpp's own comment for why.
add_executable(dummy_process_helper themisto/dummy_process_helper.cpp)
target_compile_features(dummy_process_helper PRIVATE cxx_std_23)

add_executable(kernel_process_test themisto/kernel_process_test.cpp)
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
add_executable(client_zmq_test adrastea/client_zmq_test.cpp)
target_compile_features(client_zmq_test PRIVATE cxx_std_23)
target_include_directories(client_zmq_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(client_zmq_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ClientZmqTest COMMAND client_zmq_test)

# ClientHeartbeat in isolation, with short custom timeout/max_retry values
# -- see client_heartbeat_test.cpp's own comment for why this can't be
# exercised through ClientZmqImpl's hardcoded 20s/3-retry constants.
add_executable(client_heartbeat_test adrastea/client_heartbeat_test.cpp)
target_compile_features(client_heartbeat_test PRIVATE cxx_std_23)
target_include_directories(client_heartbeat_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(client_heartbeat_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ClientHeartbeatTest COMMAND client_heartbeat_test)

add_executable(client_handshake_zmq_test adrastea/client_handshake_zmq_test.cpp)
target_compile_features(client_handshake_zmq_test PRIVATE cxx_std_23)
target_include_directories(client_handshake_zmq_test PRIVATE ${JOVIAN_INCLUDE_DIR} ${JOVIAN_SOURCE_DIR})
target_link_libraries(client_handshake_zmq_test PRIVATE themisto_core cppzmq GTest::gtest_main)
add_test(NAME ClientHandshakeZmqTest COMMAND client_handshake_zmq_test)
