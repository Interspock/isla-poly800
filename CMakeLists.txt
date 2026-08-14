cmake_minimum_required(VERSION 3.16)
project(isla_poly800 VERSION 0.4.0 LANGUAGES C)

# Audio DSP should not accidentally ship as an unoptimised debug-style build.
# Users can still explicitly request Debug/RelWithDebInfo when needed.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LV2 REQUIRED lv2)

set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/isla-poly800.lv2")
file(MAKE_DIRECTORY "${BUNDLE_DIR}")

add_library(isla_poly800 MODULE
    src/isla_poly800.c
    src/poly800_core.c
)
target_compile_features(isla_poly800 PRIVATE c_std_99)
target_include_directories(isla_poly800 PRIVATE
    ${LV2_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(isla_poly800 PRIVATE
    ${LV2_CFLAGS_OTHER}
    -Wall
    -Wextra
    -Wpedantic
)
target_link_libraries(isla_poly800 PRIVATE m)

set_target_properties(isla_poly800 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "isla-poly800"
    LIBRARY_OUTPUT_DIRECTORY "${BUNDLE_DIR}"
)

configure_file(lv2/manifest.ttl "${BUNDLE_DIR}/manifest.ttl" COPYONLY)
configure_file(lv2/isla-poly800.ttl "${BUNDLE_DIR}/isla-poly800.ttl" COPYONLY)
configure_file(lv2/state.ttl "${BUNDLE_DIR}/state.ttl" COPYONLY)
configure_file(lv2/presets.ttl "${BUNDLE_DIR}/presets.ttl" COPYONLY)

add_custom_target(bundle ALL DEPENDS isla_poly800)

install(DIRECTORY "${BUNDLE_DIR}/"
        DESTINATION "lib/lv2/isla-poly800.lv2")

enable_testing()

function(add_core_tool target source)
    add_executable(${target}
        ${source}
        src/poly800_core.c
    )
    target_compile_features(${target} PRIVATE c_std_99)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(${target} PRIVATE m)
endfunction()

add_core_tool(core_smoke tests/core_smoke.c)
add_test(NAME core_smoke COMMAND core_smoke)

add_core_tool(core_calibration tests/core_calibration.c)
add_test(NAME core_calibration COMMAND core_calibration)

add_core_tool(core_m4 tests/core_m4.c)
add_test(NAME core_m4 COMMAND core_m4)

add_executable(lv2_state_smoke
    tests/lv2_state_smoke.c
    src/isla_poly800.c
    src/poly800_core.c
)
target_compile_features(lv2_state_smoke PRIVATE c_std_99)
target_include_directories(lv2_state_smoke PRIVATE
    ${LV2_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_options(lv2_state_smoke PRIVATE
    ${LV2_CFLAGS_OTHER}
    -Wall
    -Wextra
    -Wpedantic
)
target_link_libraries(lv2_state_smoke PRIVATE m)
add_test(NAME lv2_state_smoke COMMAND lv2_state_smoke)

add_core_tool(core_bench tests/core_bench.c)
add_core_tool(core_ab_probe tools/core_ab_probe.c)
