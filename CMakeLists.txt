cmake_minimum_required(VERSION 3.16)
project(isla_poly800 VERSION 0.0.1 LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LV2 REQUIRED lv2)

set(BUNDLE_DIR "${CMAKE_BINARY_DIR}/isla-poly800.lv2")
file(MAKE_DIRECTORY "${BUNDLE_DIR}")

add_library(isla_poly800 MODULE src/isla_poly800.c)
target_compile_features(isla_poly800 PRIVATE c_std_99)
target_include_directories(isla_poly800 PRIVATE ${LV2_INCLUDE_DIRS})
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

add_custom_target(bundle ALL DEPENDS isla_poly800)

install(DIRECTORY "${BUNDLE_DIR}/"
        DESTINATION "lib/lv2/isla-poly800.lv2")
