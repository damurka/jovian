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

# Combine all sources
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
# TARGET SELECTION (Addon vs Executable)
# ==========================================

# Always build addon for now to ensure it works
set(BUILD_ADDON ON)

if (BUILD_ADDON)
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

    if (MSVC)
        target_compile_definitions(datasuite_addon PRIVATE -DNOMINMAX)
        target_compile_options(datasuite_addon PRIVATE /DGUID_WINDOWS /MP /bigobj)
        target_compile_options(datasuite_addon PRIVATE /wd4251 /wd4996)
    elseif (APPLE)
        target_compile_definitions(datasuite_addon PRIVATE -DGUID_CFUUID)
    else ()
        target_compile_definitions(datasuite_addon PRIVATE -DGUID_LIBUUID)
    endif ()

    set_target_properties(datasuite_addon PROPERTIES PREFIX "" SUFFIX ".node")

else()
    message(STATUS "Standard CMake detected! Building Standalone Executable (datasuite-r.exe)...")

    # ==========================================
    # EXECUTABLE TARGET
    # ==========================================
    # Note: datasuite-r.cpp should be in native/src/ if building standalone
    add_executable(datasuite-r
        ${DATASUITE_SOURCES}
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

    if (MSVC)
        target_compile_definitions(datasuite-r PRIVATE -DNOMINMAX)
        target_compile_options(datasuite-r PRIVATE /DGUID_WINDOWS /MP /bigobj)
        target_compile_options(datasuite-r PRIVATE /wd4251 /wd4996)
    elseif (APPLE)
        target_compile_definitions(datasuite-r PRIVATE -DGUID_CFUUID)
    else ()
        target_compile_definitions(datasuite-r PRIVATE -DGUID_LIBUUID)
    endif ()

endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
