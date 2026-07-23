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
