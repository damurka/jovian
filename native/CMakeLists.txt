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

set(DATASUITE_BRIDGE_SOURCES
    ${DATASUITE_SOURCE_DIR}/bridge/addon.cpp
    ${DATASUITE_SOURCE_DIR}/bridge/datasuite_engine.cpp
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
    # block until shutdown) is exactly what a standalone kernel process
    # needs -- reused as-is from the addon's bridge layer. Only
    # bridge/addon.cpp itself depends on N-API; this file does not.
    ${DATASUITE_SOURCE_DIR}/bridge/datasuite_engine.cpp
)

# Combine all sources (addon target only)
set(DATASUITE_SOURCES
    ${DATASUITE_CORE_SOURCES}
    ${DATASUITE_TRANSPORT_SOURCES}
    ${DATASUITE_BRIDGE_SOURCES}
    ${DATASUITE_PLATFORM_SOURCES}
    ${DATASUITE_R_SOURCES}
    ${DATASUITE_UTILS_SOURCES}
)

# Create source groups for IDE organization
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES ${DATASUITE_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${DATASUITE_HEADERS})

# ==========================================
# TARGET SELECTION
# ==========================================
# The three targets below are independent, not mutually exclusive: nothing
# stops a Node addon, a standalone kernel executable, and a supervisor from
# all being built out of the same configure. They previously lived behind a
# single hardcoded either/or that only ever built the addon.

option(DATASUITE_BUILD_ADDON "Build the Node.js N-API addon (datasuite_addon.node)" ON)
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

if (DATASUITE_BUILD_ADDON)
    message(STATUS "Building Node.js Addon (datasuite_addon.node)...")

    # ==========================================
    # NODE.JS ADDON TARGET
    # ==========================================
    if (DEFINED CMAKE_JS_INC)
        include_directories(${CMAKE_JS_INC})
    endif()

    execute_process(COMMAND node -e "console.log(require('node-addon-api').include.replace(/\"/g, ''))"
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE NODE_ADDON_API_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE)

    file(TO_CMAKE_PATH "${NODE_ADDON_API_DIR}" NODE_ADDON_API_DIR)
    include_directories(${NODE_ADDON_API_DIR})

    # Build addon with all sources (not linking to library)
    add_library(datasuite_addon MODULE ${DATASUITE_SOURCES} ${DATASUITE_HEADERS})
    target_compile_features(datasuite_addon PRIVATE cxx_std_23)

    target_include_directories(datasuite_addon PRIVATE
        ${DATASUITE_INCLUDE_DIR}
        ${DATASUITE_SOURCE_DIR}
        ${R_INCLUDE_DIR}
    )

    target_link_libraries(datasuite_addon PRIVATE
        ${CMAKE_JS_LIB}
        nlohmann_json::nlohmann_json
        cppzmq
        OpenSSL::Crypto
        ${R_LIBRARIES}
    )

    target_compile_definitions(datasuite_addon PRIVATE DATASUITE_EXPORTS)
    datasuite_apply_platform_guid_define(datasuite_addon)

    set_target_properties(datasuite_addon PROPERTIES PREFIX "" SUFFIX ".node")
endif ()

if (DATASUITE_BUILD_KERNEL_EXE)
    message(STATUS "Building standalone kernel executable (datasuite-r)...")

    # ==========================================
    # STANDALONE KERNEL EXECUTABLE TARGET
    # ==========================================
    # Same core/transport/R sources as the addon, minus the N-API bridge
    # itself (bridge/addon.cpp) -- bridge/datasuite_engine.cpp (DatasuiteServer)
    # has no N-API dependency and is reused as-is (see DATASUITE_KERNEL_EXE_
    # SOURCES above). This target has its own entry point (datasuite-r.cpp)
    # that boots the kernel directly and registers with a supervisor instead
    # of an in-process Node callback.
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
    # DATASUITE_EXPORTS is defined -- fine for the addon (a .node DLL that
    # defines DATASUITE_EXPORTS already), but this target compiles every
    # datasuite:: symbol directly into a plain .exe with nothing external to
    # import from, so without this define the linker looks for import stubs
    # that don't exist ("unresolved external symbol ... dllimport ...").
    target_compile_definitions(datasuite-r PRIVATE DATASUITE_EXPORTS)

    # cmake-js forces the static CRT (/MT) project-wide via
    # CMAKE_MSVC_RUNTIME_LIBRARY, needed for Node addon ABI compatibility --
    # irrelevant here, and actively wrong for vcpkg's x64-windows triplet,
    # whose prebuilt/built packages default to the dynamic CRT (/MD). This
    # target isn't Node-ABI-constrained, so it overrides back to match what
    # its vcpkg dependencies were actually built against.
    set_target_properties(datasuite-r PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

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
    # x64-windows triplet (/MD) rather than cmake-js's Node-ABI-driven /MT.
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
