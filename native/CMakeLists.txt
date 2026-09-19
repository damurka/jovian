set(JOVIAN_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(JOVIAN_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(JOVIAN_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

set(ADRASTEA_HEADERS
    ${JOVIAN_INCLUDE_DIR}/adrastea/context.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/comm.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/control_messenger.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/adrastea.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/guid.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/helper.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/history_manager.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/input.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/interpreter.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/json.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/kernel_configuration.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/kernel.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/logger.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/message.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/middleware.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/request_context.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/server.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/server_zmq.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/system.hpp
    ${JOVIAN_INCLUDE_DIR}/adrastea/thread.hpp
)

set(ELARA_HEADERS
    ${JOVIAN_INCLUDE_DIR}/elara/engine.hpp
    ${JOVIAN_INCLUDE_DIR}/elara/interpreter_r.hpp
)

# Organized by module
set(ADRASTEA_CORE_SOURCES
    ${JOVIAN_SOURCE_DIR}/core/kernel/kernel.cpp
    ${JOVIAN_SOURCE_DIR}/core/kernel/kernel_core.cpp
    ${JOVIAN_SOURCE_DIR}/core/kernel/kernel_core.hpp
    ${JOVIAN_SOURCE_DIR}/core/kernel/kernel_configuration.cpp
    ${JOVIAN_SOURCE_DIR}/core/execution/interpreter.cpp
    ${JOVIAN_SOURCE_DIR}/core/execution/interpreter_r.cpp
    ${JOVIAN_SOURCE_DIR}/core/messaging/message.cpp
    ${JOVIAN_SOURCE_DIR}/core/messaging/comm.cpp
    ${JOVIAN_SOURCE_DIR}/core/messaging/request_context.cpp
    ${JOVIAN_SOURCE_DIR}/core/history/history_manager.cpp
    ${JOVIAN_SOURCE_DIR}/core/history/in_memory_history_manager.cpp
    ${JOVIAN_SOURCE_DIR}/core/history/in_memory_history_manager.hpp
)

set(ADRASTEA_TRANSPORT_SOURCES
    ${JOVIAN_SOURCE_DIR}/transport/context.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/handshaking.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/handshaking.hpp
    ${JOVIAN_SOURCE_DIR}/transport/server/heartbeat.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/heartbeat.hpp
    ${JOVIAN_SOURCE_DIR}/transport/server/publisher.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/publisher.hpp
    ${JOVIAN_SOURCE_DIR}/transport/server/server_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/server_zmq_default.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/server_zmq_default.hpp
    ${JOVIAN_SOURCE_DIR}/transport/server/server_zmq_impl.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/server_zmq_impl.hpp
    ${JOVIAN_SOURCE_DIR}/transport/server/trivial_messenger.cpp
    ${JOVIAN_SOURCE_DIR}/transport/server/trivial_messenger.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_handshake_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_handshake_zmq.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_heartbeat.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_heartbeat.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_iopub.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_iopub.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_messenger.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_messenger.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq_impl.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq_impl.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/dealer_channel.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/dealer_channel.hpp
    ${JOVIAN_SOURCE_DIR}/transport/common/authentication.cpp
    ${JOVIAN_SOURCE_DIR}/transport/common/authentication.hpp
    ${JOVIAN_SOURCE_DIR}/transport/common/middleware.cpp
    ${JOVIAN_SOURCE_DIR}/transport/common/middleware_impl.hpp
    ${JOVIAN_SOURCE_DIR}/transport/common/zmq_serializer.cpp
    ${JOVIAN_SOURCE_DIR}/transport/common/zmq_serializer.hpp
)

set(ADRASTEA_PLATFORM_SOURCES
    ${JOVIAN_SOURCE_DIR}/platform/guid.cpp
    ${JOVIAN_SOURCE_DIR}/platform/system.cpp
)

set(ELARA_R_SOURCES
    ${JOVIAN_SOURCE_DIR}/r/routine.cpp
    ${JOVIAN_SOURCE_DIR}/r/rtools.hpp
)

set(ADRASTEA_UTILS_SOURCES
    ${JOVIAN_SOURCE_DIR}/utils/helper.cpp
    ${JOVIAN_SOURCE_DIR}/utils/input.cpp
    ${JOVIAN_SOURCE_DIR}/utils/logger.cpp
    ${JOVIAN_SOURCE_DIR}/utils/logger_impl.cpp
    ${JOVIAN_SOURCE_DIR}/utils/logger_impl.hpp
    ${JOVIAN_SOURCE_DIR}/utils/control_messenger.cpp
)

# Narrower source lists for the supervisor (native/src/supervisor), which is
# a ZMQ *client*/gateway only -- it never embeds R and never runs a Kernel/
# KernelCore, so it must not pull in ADRASTEA_CORE_SOURCES (which bundles
# the R-dependent interpreter_r.cpp) or the server-side half of
# ADRASTEA_TRANSPORT_SOURCES. It reuses the client transport code exactly
# as-is (see native/src/transport/client/client_handshake_zmq.cpp,
# client_zmq.cpp) rather than reimplementing wire handling.
set(ADRASTEA_MESSAGING_SOURCES
    ${JOVIAN_SOURCE_DIR}/core/messaging/message.cpp
    ${JOVIAN_SOURCE_DIR}/core/kernel/kernel_configuration.cpp
)

set(ADRASTEA_TRANSPORT_CLIENT_SOURCES
    ${JOVIAN_SOURCE_DIR}/transport/context.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_handshake_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_handshake_zmq.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_heartbeat.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_heartbeat.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_iopub.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_iopub.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_messenger.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_messenger.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq_impl.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/client_zmq_impl.hpp
    ${JOVIAN_SOURCE_DIR}/transport/client/dealer_channel.cpp
    ${JOVIAN_SOURCE_DIR}/transport/client/dealer_channel.hpp
    ${JOVIAN_SOURCE_DIR}/transport/common/authentication.cpp
    ${JOVIAN_SOURCE_DIR}/transport/common/authentication.hpp
    ${JOVIAN_SOURCE_DIR}/transport/common/middleware.cpp
    ${JOVIAN_SOURCE_DIR}/transport/common/middleware_impl.hpp
    ${JOVIAN_SOURCE_DIR}/transport/common/zmq_serializer.cpp
    ${JOVIAN_SOURCE_DIR}/transport/common/zmq_serializer.hpp
)

set(THEMISTO_SOURCES
    ${JOVIAN_SOURCE_DIR}/supervisor/main.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/session_registry.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/session_registry.hpp
    ${JOVIAN_SOURCE_DIR}/supervisor/kernel_process.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/kernel_process.hpp
    ${JOVIAN_SOURCE_DIR}/supervisor/http_api.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/http_api.hpp
    ${JOVIAN_SOURCE_DIR}/supervisor/ws_relay.cpp
    ${JOVIAN_SOURCE_DIR}/supervisor/ws_relay.hpp
)

set(ELARA_EXE_SOURCES
    ${JOVIAN_SOURCE_DIR}/elara.cpp
    # elara::Server (env setup + boot the Kernel, run it on the calling thread,
    # block until shutdown) -- the only consumer of bridge/engine.cpp
    # now that the Node addon (which used to share it) is gone.
    ${JOVIAN_SOURCE_DIR}/bridge/engine.cpp
)

# Create source groups for IDE organization
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES
    ${ADRASTEA_CORE_SOURCES}
    ${ADRASTEA_TRANSPORT_SOURCES}
    ${ADRASTEA_PLATFORM_SOURCES}
    ${ELARA_R_SOURCES}
    ${ADRASTEA_UTILS_SOURCES}
    ${THEMISTO_SOURCES}
    ${ELARA_EXE_SOURCES}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${ADRASTEA_HEADERS} ${ELARA_HEADERS})

# ==========================================
# TARGET SELECTION
# ==========================================
# Two independent, coexisting targets: the standalone kernel executable
# (embeds R, one per session) and the supervisor (spawns/monitors kernel
# processes over ZMQ, exposes them over HTTP+WS). There used to be a third,
# a Node N-API addon -- removed once nothing depended on it anymore (see
# lib/session/session-manager.ts, which now talks to the supervisor instead
# of an in-process addon).

option(JOVIAN_BUILD_ELARA "Build the standalone kernel executable (elara)" ON)
option(JOVIAN_BUILD_THEMISTO "Build the kernel supervisor executable (themisto)" ON)

function(jovian_apply_platform_guid_define target_name)
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

if (JOVIAN_BUILD_ELARA)
    message(STATUS "Building standalone kernel executable (elara)...")

    # ==========================================
    # STANDALONE KERNEL EXECUTABLE TARGET
    # ==========================================
    add_executable(elara
        ${ADRASTEA_CORE_SOURCES}
        ${ADRASTEA_TRANSPORT_SOURCES}
        ${ADRASTEA_PLATFORM_SOURCES}
        ${ELARA_R_SOURCES}
        ${ADRASTEA_UTILS_SOURCES}
        ${ELARA_EXE_SOURCES}
        ${ADRASTEA_HEADERS}
        ${ELARA_HEADERS}
    )

    target_compile_features(elara PRIVATE cxx_std_23)

    target_include_directories(elara PRIVATE
        ${JOVIAN_INCLUDE_DIR}
        ${JOVIAN_SOURCE_DIR}
        ${R_INCLUDE_DIR}
    )

    target_link_libraries(elara PRIVATE
        nlohmann_json::nlohmann_json
        cppzmq
        OpenSSL::Crypto
        ${R_LIBRARIES}
    )

    # ADRASTEA_API (adrastea.hpp) expands to __declspec(dllimport) unless
    # ADRASTEA_EXPORTS is defined -- this target compiles every adrastea::
    # symbol directly into a plain .exe with nothing external to import
    # from, so without this define the linker looks for import stubs that
    # don't exist ("unresolved external symbol ... dllimport ...").
    target_compile_definitions(elara PRIVATE ADRASTEA_EXPORTS)

    # Matches vcpkg's x64-windows triplet, which builds its packages
    # (cppzmq/zeromq/openssl) against the dynamic CRT (/MD) by default.
    set_target_properties(elara PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    if (MSVC)
        # R's embedded interpreter runs directly on this process's main
        # thread (see Server::start(), engine.cpp) --
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
        target_link_options(elara PRIVATE "/STACK:134217728")
    endif ()

    jovian_apply_platform_guid_define(elara)
endif ()

if (JOVIAN_BUILD_THEMISTO)
    message(STATUS "Building kernel supervisor executable (themisto)...")

    # ==========================================
    # SUPERVISOR EXECUTABLE TARGET
    # ==========================================
    # No R sources/libraries: the supervisor never embeds R, it only ever
    # speaks ZMQ (as a client, to kernel processes it spawns) and HTTP/WS
    # (as a server, to Node/Electron consumers). See ADRASTEA_MESSAGING_
    # SOURCES / ADRASTEA_TRANSPORT_CLIENT_SOURCES above for why it can't
    # reuse the full ADRASTEA_CORE_SOURCES/ADRASTEA_TRANSPORT_SOURCES
    # lists as-is.
    add_executable(themisto
        ${ADRASTEA_MESSAGING_SOURCES}
        ${ADRASTEA_TRANSPORT_CLIENT_SOURCES}
        ${ADRASTEA_PLATFORM_SOURCES}
        ${THEMISTO_SOURCES}
    )

    target_compile_features(themisto PRIVATE cxx_std_23)

    target_include_directories(themisto PRIVATE
        ${JOVIAN_INCLUDE_DIR}
        ${JOVIAN_SOURCE_DIR}
    )

    target_link_libraries(themisto PRIVATE
        nlohmann_json::nlohmann_json
        cppzmq
        OpenSSL::Crypto
        httplib::httplib
        ixwebsocket::ixwebsocket
    )

    # See the matching comment on the elara target above -- same two
    # issues apply here: ADRASTEA_API needs ADRASTEA_EXPORTS to resolve
    # adrastea:: symbols within this .exe instead of expecting a DLL to
    # import them from, and the CRT linkage needs to match vcpkg's
    # x64-windows triplet (/MD).
    target_compile_definitions(themisto PRIVATE ADRASTEA_EXPORTS)
    set_target_properties(themisto PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    if (WIN32)
        # mbedtls (ixwebsocket's TLS backend) calls BCryptGenRandom for
        # entropy -- not pulled in transitively by the vcpkg port on this
        # setup, so link it explicitly rather than relying on that.
        target_link_libraries(themisto PRIVATE bcrypt)
    endif ()

    jovian_apply_platform_guid_define(themisto)
endif ()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
