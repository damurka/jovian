set(DATASUITE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(DATASUITE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DATASUITE_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

set(DATASUITE_HEADERS
    ${DATASUITE_INCLUDE_DIR}/datasuite/context.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/comm.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/control_messenger.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/datasuite.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/datasuite_engine.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/guid.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/helper.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/history_manager.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/input.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/interpreter.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/interpreter_r.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/json.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/kernel_configuration.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/kernel.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/logger.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/message.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/middleware.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/request_context.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/server.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/server_zmq.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/system.hpp
    ${DATASUITE_INCLUDE_DIR}/datasuite/thread.hpp
)

# Organized by module
set(DATASUITE_CORE_SOURCES
    ${DATASUITE_SOURCE_DIR}/core/kernel/kernel.cpp
    ${DATASUITE_SOURCE_DIR}/core/kernel/kernel_core.cpp
    ${DATASUITE_SOURCE_DIR}/core/kernel/kernel_core.hpp
    ${DATASUITE_SOURCE_DIR}/core/kernel/kernel_configuration.cpp
    ${DATASUITE_SOURCE_DIR}/core/execution/interpreter.cpp
    ${DATASUITE_SOURCE_DIR}/core/execution/interpreter_r.cpp
    ${DATASUITE_SOURCE_DIR}/core/messaging/message.cpp
    ${DATASUITE_SOURCE_DIR}/core/messaging/comm.cpp
    ${DATASUITE_SOURCE_DIR}/core/messaging/request_context.cpp
    ${DATASUITE_SOURCE_DIR}/core/history/history_manager.cpp
    ${DATASUITE_SOURCE_DIR}/core/history/in_memory_history_manager.cpp
    ${DATASUITE_SOURCE_DIR}/core/history/in_memory_history_manager.hpp
)

set(DATASUITE_TRANSPORT_SOURCES
    ${DATASUITE_SOURCE_DIR}/transport/context.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/handshaking.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/handshaking.hpp
    ${DATASUITE_SOURCE_DIR}/transport/server/heartbeat.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/heartbeat.hpp
    ${DATASUITE_SOURCE_DIR}/transport/server/publisher.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/publisher.hpp
    ${DATASUITE_SOURCE_DIR}/transport/server/server_zmq.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/server_zmq_default.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/server_zmq_default.hpp
    ${DATASUITE_SOURCE_DIR}/transport/server/server_zmq_impl.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/server_zmq_impl.hpp
    ${DATASUITE_SOURCE_DIR}/transport/server/trivial_messenger.cpp
    ${DATASUITE_SOURCE_DIR}/transport/server/trivial_messenger.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_handshake_zmq.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_handshake_zmq.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_heartbeat.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_heartbeat.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_iopub.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_iopub.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_messenger.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_messenger.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq_impl.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq_impl.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/dealer_channel.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/dealer_channel.hpp
    ${DATASUITE_SOURCE_DIR}/transport/common/authentication.cpp
    ${DATASUITE_SOURCE_DIR}/transport/common/authentication.hpp
    ${DATASUITE_SOURCE_DIR}/transport/common/middleware.cpp
    ${DATASUITE_SOURCE_DIR}/transport/common/middleware_impl.hpp
    ${DATASUITE_SOURCE_DIR}/transport/common/zmq_serializer.cpp
    ${DATASUITE_SOURCE_DIR}/transport/common/zmq_serializer.hpp
)

set(DATASUITE_PLATFORM_SOURCES
    ${DATASUITE_SOURCE_DIR}/platform/guid.cpp
    ${DATASUITE_SOURCE_DIR}/platform/system.cpp
)

set(DATASUITE_R_SOURCES
    ${DATASUITE_SOURCE_DIR}/r/routine.cpp
    ${DATASUITE_SOURCE_DIR}/r/rtools.hpp
)

set(DATASUITE_UTILS_SOURCES
    ${DATASUITE_SOURCE_DIR}/utils/helper.cpp
    ${DATASUITE_SOURCE_DIR}/utils/input.cpp
    ${DATASUITE_SOURCE_DIR}/utils/logger.cpp
    ${DATASUITE_SOURCE_DIR}/utils/logger_impl.cpp
    ${DATASUITE_SOURCE_DIR}/utils/logger_impl.hpp
    ${DATASUITE_SOURCE_DIR}/utils/control_messenger.cpp
)

# Narrower source lists for the supervisor (native/src/supervisor), which is
# a ZMQ *client*/gateway only -- it never embeds R and never runs a Kernel/
# KernelCore, so it must not pull in DATASUITE_CORE_SOURCES (which bundles
# the R-dependent interpreter_r.cpp) or the server-side half of
# DATASUITE_TRANSPORT_SOURCES. It reuses the client transport code exactly
# as-is (see native/src/transport/client/client_handshake_zmq.cpp,
# client_zmq.cpp) rather than reimplementing wire handling.
set(DATASUITE_MESSAGING_SOURCES
    ${DATASUITE_SOURCE_DIR}/core/messaging/message.cpp
    ${DATASUITE_SOURCE_DIR}/core/kernel/kernel_configuration.cpp
)

set(DATASUITE_TRANSPORT_CLIENT_SOURCES
    ${DATASUITE_SOURCE_DIR}/transport/context.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_handshake_zmq.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_handshake_zmq.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_heartbeat.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_heartbeat.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_iopub.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_iopub.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_messenger.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_messenger.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq_impl.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/client_zmq_impl.hpp
    ${DATASUITE_SOURCE_DIR}/transport/client/dealer_channel.cpp
    ${DATASUITE_SOURCE_DIR}/transport/client/dealer_channel.hpp
    ${DATASUITE_SOURCE_DIR}/transport/common/authentication.cpp
    ${DATASUITE_SOURCE_DIR}/transport/common/authentication.hpp
    ${DATASUITE_SOURCE_DIR}/transport/common/middleware.cpp
    ${DATASUITE_SOURCE_DIR}/transport/common/middleware_impl.hpp
    ${DATASUITE_SOURCE_DIR}/transport/common/zmq_serializer.cpp
    ${DATASUITE_SOURCE_DIR}/transport/common/zmq_serializer.hpp
)

set(DATASUITE_SUPERVISOR_SOURCES
    ${DATASUITE_SOURCE_DIR}/supervisor/main.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/session_registry.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/session_registry.hpp
    ${DATASUITE_SOURCE_DIR}/supervisor/kernel_process.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/kernel_process.hpp
    ${DATASUITE_SOURCE_DIR}/supervisor/http_api.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/http_api.hpp
    ${DATASUITE_SOURCE_DIR}/supervisor/ws_relay.cpp
    ${DATASUITE_SOURCE_DIR}/supervisor/ws_relay.hpp
)

set(DATASUITE_KERNEL_EXE_SOURCES
    ${DATASUITE_SOURCE_DIR}/datasuite-r.cpp
    # DatasuiteServer (env setup + boot the Kernel on a background thread,
    # block until shutdown) -- the only consumer of bridge/datasuite_engine.cpp
    # now that the Node addon (which used to share it) is gone.
    ${DATASUITE_SOURCE_DIR}/bridge/datasuite_engine.cpp
)

# Create source groups for IDE organization
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES
    ${DATASUITE_CORE_SOURCES}
    ${DATASUITE_TRANSPORT_SOURCES}
    ${DATASUITE_PLATFORM_SOURCES}
    ${DATASUITE_R_SOURCES}
    ${DATASUITE_UTILS_SOURCES}
    ${DATASUITE_SUPERVISOR_SOURCES}
    ${DATASUITE_KERNEL_EXE_SOURCES}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${DATASUITE_HEADERS})

# ==========================================
# TARGET SELECTION
# ==========================================
# Two independent, coexisting targets: the standalone kernel executable
# (embeds R, one per session) and the supervisor (spawns/monitors kernel
# processes over ZMQ, exposes them over HTTP+WS). There used to be a third,
# a Node N-API addon -- removed once nothing depended on it anymore (see
# lib/session/session-manager.ts, which now talks to the supervisor instead
# of an in-process addon).

option(DATASUITE_BUILD_KERNEL_EXE "Build the standalone kernel executable (datasuite-r)" ON)
option(DATASUITE_BUILD_SUPERVISOR "Build the kernel supervisor executable (datasuite-supervisor)" ON)

function(datasuite_apply_platform_guid_define target_name)
    if (MSVC)
        target_compile_definitions(${target_name} PRIVATE -DNOMINMAX)
        target_compile_options(${target_name} PRIVATE /DGUID_WINDOWS /MP /bigobj)
        target_compile_options(${target_name} PRIVATE /wd4251 /wd4996)
    elseif (APPLE)
        target_compile_definitions(${target_name} PRIVATE -DGUID_CFUUID)
    else ()
        target_compile_definitions(${target_name} PRIVATE -DGUID_LIBUUID)
    endif ()
endfunction()

if (DATASUITE_BUILD_KERNEL_EXE)
    message(STATUS "Building standalone kernel executable (datasuite-r)...")

    # ==========================================
    # STANDALONE KERNEL EXECUTABLE TARGET
    # ==========================================
    add_executable(datasuite-r
        ${DATASUITE_CORE_SOURCES}
        ${DATASUITE_TRANSPORT_SOURCES}
        ${DATASUITE_PLATFORM_SOURCES}
        ${DATASUITE_R_SOURCES}
        ${DATASUITE_UTILS_SOURCES}
        ${DATASUITE_KERNEL_EXE_SOURCES}
        ${DATASUITE_HEADERS}
    )

    target_compile_features(datasuite-r PRIVATE cxx_std_23)

    target_include_directories(datasuite-r PRIVATE
        ${DATASUITE_INCLUDE_DIR}
        ${DATASUITE_SOURCE_DIR}
        ${R_INCLUDE_DIR}
    )

    target_link_libraries(datasuite-r PRIVATE
        nlohmann_json::nlohmann_json
        cppzmq
        OpenSSL::Crypto
        ${R_LIBRARIES}
    )

    # DATASUITE_API (datasuite.hpp) expands to __declspec(dllimport) unless
    # DATASUITE_EXPORTS is defined -- this target compiles every datasuite::
    # symbol directly into a plain .exe with nothing external to import
    # from, so without this define the linker looks for import stubs that
    # don't exist ("unresolved external symbol ... dllimport ...").
    target_compile_definitions(datasuite-r PRIVATE DATASUITE_EXPORTS)

    # Matches vcpkg's x64-windows triplet, which builds its packages
    # (cppzmq/zeromq/openssl) against the dynamic CRT (/MD) by default.
    set_target_properties(datasuite-r PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    if (MSVC)
        # R's embedded interpreter runs directly on this process's main
        # thread (see DatasuiteServer::start(), datasuite_engine.cpp) --
        # deliberately, so R's own C-stack-bounds auto-detection (which
        # assumes main-thread execution) computes correct values instead of
        # needing to be second-guessed at runtime. That auto-detection
        # reflects whatever stack this thread was actually given, though,
        # so it still needs to be a real one: the linker's default reserve
        # (1MB) is comfortably enough for typical R evaluation but not for
        # the deep C-level recursion real workloads can hit (R6 method
        # dispatch chains, sf/geometry operations, Shiny's reactive graph)
        # -- a real crash traced back to exactly this (cd2030.core's
        # init_CacheConnection()$reactive() during a live Shiny session,
        # STATUS_STACK_OVERFLOW) is what surfaced the need for a
        # deliberately larger one. 128MB reserved (not committed -- actual
        # memory use only grows as the stack is actually touched) is
        # negligible next to R's own heap footprint.
        target_link_options(datasuite-r PRIVATE "/STACK:134217728")
    endif ()

    datasuite_apply_platform_guid_define(datasuite-r)
endif ()

if (DATASUITE_BUILD_SUPERVISOR)
    message(STATUS "Building kernel supervisor executable (datasuite-supervisor)...")

    # ==========================================
    # SUPERVISOR EXECUTABLE TARGET
    # ==========================================
    # No R sources/libraries: the supervisor never embeds R, it only ever
    # speaks ZMQ (as a client, to kernel processes it spawns) and HTTP/WS
    # (as a server, to Node/Electron consumers). See DATASUITE_MESSAGING_
    # SOURCES / DATASUITE_TRANSPORT_CLIENT_SOURCES above for why it can't
    # reuse the full DATASUITE_CORE_SOURCES/DATASUITE_TRANSPORT_SOURCES
    # lists as-is.
    add_executable(datasuite-supervisor
        ${DATASUITE_MESSAGING_SOURCES}
        ${DATASUITE_TRANSPORT_CLIENT_SOURCES}
        ${DATASUITE_PLATFORM_SOURCES}
        ${DATASUITE_SUPERVISOR_SOURCES}
    )

    target_compile_features(datasuite-supervisor PRIVATE cxx_std_23)

    target_include_directories(datasuite-supervisor PRIVATE
        ${DATASUITE_INCLUDE_DIR}
        ${DATASUITE_SOURCE_DIR}
    )

    target_link_libraries(datasuite-supervisor PRIVATE
        nlohmann_json::nlohmann_json
        cppzmq
        OpenSSL::Crypto
        httplib::httplib
        ixwebsocket::ixwebsocket
    )

    # See the matching comment on the datasuite-r target above -- same two
    # issues apply here: DATASUITE_API needs DATASUITE_EXPORTS to resolve
    # datasuite:: symbols within this .exe instead of expecting a DLL to
    # import them from, and the CRT linkage needs to match vcpkg's
    # x64-windows triplet (/MD).
    target_compile_definitions(datasuite-supervisor PRIVATE DATASUITE_EXPORTS)
    set_target_properties(datasuite-supervisor PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    if (WIN32)
        # mbedtls (ixwebsocket's TLS backend) calls BCryptGenRandom for
        # entropy -- not pulled in transitively by the vcpkg port on this
        # setup, so link it explicitly rather than relying on that.
        target_link_libraries(datasuite-supervisor PRIVATE bcrypt)
    endif ()

    datasuite_apply_platform_guid_define(datasuite-supervisor)
endif ()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
