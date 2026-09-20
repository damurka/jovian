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

set(CARPO_HEADERS
    ${JOVIAN_INCLUDE_DIR}/carpo/engine.hpp
    ${JOVIAN_INCLUDE_DIR}/carpo/interpreter_py.hpp
)

# Organized by module
set(ADRASTEA_CORE_SOURCES
    ${JOVIAN_SOURCE_DIR}/adrastea/core/kernel/kernel.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/kernel/kernel_core.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/kernel/kernel_core.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/kernel/kernel_configuration.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/execution/interpreter.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/messaging/message.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/messaging/comm.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/messaging/request_context.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/history/history_manager.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/history/in_memory_history_manager.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/core/history/in_memory_history_manager.hpp
)

set(ADRASTEA_TRANSPORT_SOURCES
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/context.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/handshaking.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/handshaking.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/heartbeat.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/heartbeat.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/publisher.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/publisher.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/server_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/server_zmq_default.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/server_zmq_default.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/server_zmq_impl.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/server_zmq_impl.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/trivial_messenger.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/server/trivial_messenger.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_handshake_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_handshake_zmq.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_heartbeat.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_heartbeat.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_iopub.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_iopub.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_messenger.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_messenger.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_zmq.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_zmq.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_zmq_impl.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/client_zmq_impl.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/dealer_channel.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/client/dealer_channel.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/common/authentication.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/common/authentication.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/common/middleware.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/common/middleware_impl.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/common/zmq_serializer.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/transport/common/zmq_serializer.hpp
)

set(ADRASTEA_PLATFORM_SOURCES
    ${JOVIAN_SOURCE_DIR}/adrastea/platform/guid.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/platform/system.cpp
)

set(ELARA_R_SOURCES
    ${JOVIAN_SOURCE_DIR}/elara/r/routine.cpp
    ${JOVIAN_SOURCE_DIR}/elara/r/rtools.hpp
    ${JOVIAN_SOURCE_DIR}/elara/r/r_dynlib.cpp
    ${JOVIAN_SOURCE_DIR}/elara/r/r_dynlib.hpp
    # The concrete R-specific Interpreter implementation -- moved out of
    # core/execution/ (which is otherwise pure adrastea framework code, see
    # ADRASTEA_CORE_SOURCES above) now that Adrastea is its own target
    # (the `adrastea` library below): this is elara-specific, not
    # framework-level, and belongs with elara's other R-specific sources.
    ${JOVIAN_SOURCE_DIR}/elara/r/interpreter_r.cpp
)

set(ADRASTEA_UTILS_SOURCES
    ${JOVIAN_SOURCE_DIR}/adrastea/utils/helper.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/utils/input.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/utils/logger.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/utils/logger_impl.cpp
    ${JOVIAN_SOURCE_DIR}/adrastea/utils/logger_impl.hpp
    ${JOVIAN_SOURCE_DIR}/adrastea/utils/control_messenger.cpp
)

set(THEMISTO_SOURCES
    ${JOVIAN_SOURCE_DIR}/themisto/main.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/session_registry.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/session_registry.hpp
    ${JOVIAN_SOURCE_DIR}/themisto/kernel_process.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/kernel_process.hpp
    ${JOVIAN_SOURCE_DIR}/themisto/http_api.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/http_api.hpp
    ${JOVIAN_SOURCE_DIR}/themisto/ws_relay.cpp
    ${JOVIAN_SOURCE_DIR}/themisto/ws_relay.hpp
)

set(ELARA_EXE_SOURCES
    ${JOVIAN_SOURCE_DIR}/elara/elara.cpp
    # elara::Server (env setup + boot the Kernel, run it on the calling thread,
    # block until shutdown) -- the only consumer of bridge/engine.cpp
    # now that the Node addon (which used to share it) is gone.
    ${JOVIAN_SOURCE_DIR}/elara/bridge/engine.cpp
)

# A real Python kernel ("Carpo"), modeled on Positron's Ark/xeus-python --
# see native/include/carpo/interpreter_py.hpp's file comment. Mirrors
# ELARA_R_SOURCES/ELARA_EXE_SOURCES in shape deliberately, proving Adrastea's
# extension point (adrastea::Interpreter) genuinely generalizes beyond
# Elara/R: py/py_dynlib.cpp is Carpo's equivalent of elara/r/r_dynlib.cpp
# (dynamically loading Python's C API at runtime instead of linking against
# a specific installation at build time, for the same reasons).
set(CARPO_SOURCES
    ${JOVIAN_SOURCE_DIR}/carpo/interpreter_py.cpp
    ${JOVIAN_SOURCE_DIR}/carpo/py/py_dynlib.cpp
    ${JOVIAN_SOURCE_DIR}/carpo/py/py_dynlib.hpp
)

set(CARPO_EXE_SOURCES
    ${JOVIAN_SOURCE_DIR}/carpo/carpo.cpp
    ${JOVIAN_SOURCE_DIR}/carpo/bridge/engine.cpp
)

# Create source groups for IDE organization
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES
    ${ADRASTEA_CORE_SOURCES}
    ${ADRASTEA_TRANSPORT_SOURCES}
    ${ADRASTEA_PLATFORM_SOURCES}
    ${ELARA_R_SOURCES}
    ${CARPO_SOURCES}
    ${CARPO_EXE_SOURCES}
    ${ADRASTEA_UTILS_SOURCES}
    ${THEMISTO_SOURCES}
    ${ELARA_EXE_SOURCES}
)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES ${ADRASTEA_HEADERS} ${ELARA_HEADERS} ${CARPO_HEADERS})

# ==========================================
# TARGET SELECTION
# ==========================================
# Three targets: the `adrastea` library (the shared, product-agnostic
# kernel-protocol framework -- transport, messaging, the Kernel/KernelCore
# request loop, the abstract Interpreter base) and the two independent,
# coexisting products built on top of it -- the standalone kernel
# executable (embeds R, one per session) and the supervisor (spawns/
# monitors kernel processes over ZMQ, exposes them over HTTP+WS). There
# used to be a fourth, a Node N-API addon -- removed once nothing depended
# on it anymore (see lib/session/session-manager.ts, which now talks to the
# supervisor instead of an in-process addon).
#
# `adrastea` didn't used to be a separate target at all: elara and themisto
# each compiled their own subset of its sources directly (themisto's subset
# was deliberately narrower -- see the removed ADRASTEA_MESSAGING_SOURCES/
# ADRASTEA_TRANSPORT_CLIENT_SOURCES, no longer needed now that themisto just
# *links* against the full library instead of *compiling* a hand-picked
# subset of it; an unreferenced object file in a static library is dropped
# at link time regardless, so nothing about themisto's own binary changes).
# That worked, but it meant Adrastea couldn't be reused as a library by
# anything else, and (until the old core/execution/interpreter_r.cpp moved
# out to elara/r/interpreter_r.cpp, alongside elara's other R-specific
# sources) the framework's own interpreter registry had a hard dependency
# back onto elara specifically (see interpreter.cpp's comment on
# getInterpreter()).

option(JOVIAN_BUILD_ELARA "Build the standalone kernel executable (elara)" ON)
option(JOVIAN_BUILD_THEMISTO "Build the kernel supervisor executable (themisto)" ON)
# Same default as elara/themisto now that carpo is a real, working Python
# kernel (native/include/carpo/interpreter_py.hpp) -- like elara's R
# dependency, Python is a *runtime* requirement (dynamically loaded, see
# native/src/carpo/py/py_dynlib.hpp), not a build-time one, so carpo.exe
# compiles fine even on a machine with no Python installed; it just can't
# actually run there without one, exactly like elara.exe and R_HOME.
option(JOVIAN_BUILD_CARPO "Build the Python kernel executable (carpo)" ON)

function(jovian_apply_platform_guid_define target_name)
    if (MSVC)
        target_compile_definitions(${target_name} PRIVATE -DNOMINMAX)
        target_compile_options(${target_name} PRIVATE /DGUID_WINDOWS /MP /bigobj)
        target_compile_options(${target_name} PRIVATE /wd4251 /wd4996)
    elseif (APPLE)
        target_compile_definitions(${target_name} PRIVATE -DGUID_CFUUID)
        # guid.cpp's GUID_CFUUID branch calls CFUUIDCreate/CFUUIDGetUUIDBytes/
        # CFRelease, which live in the CoreFoundation framework -- unlinked,
        # this fails at link time with "symbol(s) not found for architecture
        # arm64" the moment it's actually built on macOS.
        target_link_libraries(${target_name} PRIVATE "-framework CoreFoundation")
    else ()
        target_compile_definitions(${target_name} PRIVATE -DGUID_LIBUUID)
        # guid.cpp's GUID_LIBUUID branch calls uuid_generate() (<uuid/uuid.h>),
        # which on Linux lives in libuuid, not libc -- cmake/FindLibUUID.cmake
        # (present in this repo but previously unused by any target) locates
        # it. Without this, every non-Windows, non-Apple target fails at link
        # time with "undefined reference to uuid_generate" the moment it's
        # actually built there (never caught before there was Linux CI).
        find_package(LibUUID REQUIRED)
        target_link_libraries(${target_name} PRIVATE LibUUID::LibUUID)
    endif ()
endfunction()

# ==========================================
# ADRASTEA LIBRARY (the shared framework)
# ==========================================
# Static, not shared: elara/themisto are meant to be standalone,
# independently-distributable executables (see r_dynlib.hpp's file comment
# for the same reasoning applied to R itself) -- a static library avoids
# needing to ship/locate a companion DLL/.so alongside each one, matching
# the ADRASTEA_STATIC_LIB pattern the test suite already used for its own
# (now-replaced) adrastea_core lib. PUBLIC compile definitions/include
# dirs/link libraries below are deliberate: they're this library's own
# public usage requirements, and need to reach elara/themisto automatically
# through nothing more than `target_link_libraries(... adrastea)`.
add_library(adrastea STATIC
    ${ADRASTEA_CORE_SOURCES}
    ${ADRASTEA_TRANSPORT_SOURCES}
    ${ADRASTEA_PLATFORM_SOURCES}
    ${ADRASTEA_UTILS_SOURCES}
    ${ADRASTEA_HEADERS}
)

target_compile_features(adrastea PUBLIC cxx_std_23)

target_include_directories(adrastea PUBLIC
    ${JOVIAN_INCLUDE_DIR}
    ${JOVIAN_SOURCE_DIR}
)

target_link_libraries(adrastea PUBLIC
    nlohmann_json::nlohmann_json
    cppzmq
    OpenSSL::Crypto
)

# ADRASTEA_API (adrastea.hpp) expands to nothing under ADRASTEA_STATIC_LIB
# (as opposed to __declspec(dllexport)/dllimport) -- correct for a static
# library, whose symbols are linked directly into whatever consumes it
# rather than imported from a separate DLL at runtime. PUBLIC so elara/
# themisto (and anything else that links this library) get it too, without
# each needing to redeclare it themselves.
target_compile_definitions(adrastea PUBLIC ADRASTEA_STATIC_LIB)

jovian_apply_platform_guid_define(adrastea)

if (JOVIAN_BUILD_ELARA)
    message(STATUS "Building standalone kernel executable (elara)...")

    # ==========================================
    # STANDALONE KERNEL EXECUTABLE TARGET
    # ==========================================
    add_executable(elara
        ${ELARA_R_SOURCES}
        ${ELARA_EXE_SOURCES}
        ${ELARA_HEADERS}
    )

    target_compile_features(elara PRIVATE cxx_std_23)

    target_include_directories(elara PRIVATE
        ${R_INCLUDE_DIR}
    )

    # nlohmann_json/cppzmq/OpenSSL::Crypto, ADRASTEA_STATIC_LIB, and
    # JOVIAN_INCLUDE_DIR/JOVIAN_SOURCE_DIR all arrive transitively via
    # linking `adrastea` (PUBLIC on that target) -- no need to redeclare
    # them here.
    target_link_libraries(elara PRIVATE adrastea)

    # elara loads R's shared library (R.dll/libR.so/libR.dylib) dynamically
    # at runtime on every platform instead of linking against it at build
    # time -- see native/src/elara/r/r_dynlib.hpp/.cpp for why. R_LIBRARIES is
    # therefore never linked here; dlopen()/LoadLibrary() need no import
    # library, only R_INCLUDE_DIR (for types) above.
    if (NOT WIN32)
        target_link_libraries(elara PRIVATE ${CMAKE_DL_LIBS})
    endif ()

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
    # No R sources: the supervisor never embeds R, it only ever speaks ZMQ
    # (as a client, to kernel processes it spawns) and HTTP/WS (as a
    # server, to Node/Electron consumers). It links the full `adrastea`
    # library like elara does (below) -- it doesn't need everything in it
    # (the server-side transport, the Kernel/KernelCore request loop), but
    # a static library only contributes the object files something actually
    # references, so linking the whole thing costs nothing over hand-picking
    # a subset, and there's now only one place (the adrastea target) that
    # source list needs to be maintained.
    add_executable(themisto
        ${THEMISTO_SOURCES}
    )

    target_compile_features(themisto PRIVATE cxx_std_23)

    # nlohmann_json/cppzmq/OpenSSL::Crypto, ADRASTEA_STATIC_LIB, and
    # JOVIAN_INCLUDE_DIR/JOVIAN_SOURCE_DIR all arrive transitively via
    # linking `adrastea` (PUBLIC on that target) -- no need to redeclare
    # them here.
    target_link_libraries(themisto PRIVATE
        adrastea
        httplib::httplib
        ixwebsocket::ixwebsocket
    )

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

if (JOVIAN_BUILD_CARPO)
    message(STATUS "Building Python kernel executable (carpo)...")

    # ==========================================
    # CARPO EXECUTABLE TARGET
    # ==========================================
    # See CARPO_SOURCES's comment above and native/include/carpo/
    # interpreter_py.hpp's file comment for what's real vs. still stubbed.
    add_executable(carpo
        ${CARPO_SOURCES}
        ${CARPO_EXE_SOURCES}
        ${CARPO_HEADERS}
    )

    target_compile_features(carpo PRIVATE cxx_std_23)

    # Same as elara: nlohmann_json/cppzmq/OpenSSL::Crypto, ADRASTEA_STATIC_LIB,
    # and JOVIAN_INCLUDE_DIR/JOVIAN_SOURCE_DIR all arrive transitively via
    # linking `adrastea` (PUBLIC on that target).
    target_link_libraries(carpo PRIVATE adrastea)

    # carpo loads Python's shared library dynamically at runtime on every
    # platform instead of linking against it at build time -- see
    # native/src/carpo/py/py_dynlib.hpp/.cpp for why (mirrors elara's own
    # R_LIBRARIES-free dlopen()/LoadLibrary() approach exactly).
    if (NOT WIN32)
        target_link_libraries(carpo PRIVATE ${CMAKE_DL_LIBS})
    endif ()

    set_target_properties(carpo PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    jovian_apply_platform_guid_define(carpo)
endif ()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
