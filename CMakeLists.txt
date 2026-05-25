cmake_minimum_required(VERSION 3.22)

if(NOT TARGET GoodNet::sdk)
    project(goodnet_handler_zstd_decompress
        VERSION   0.1.0
        LANGUAGES CXX)
    find_package(GoodNet REQUIRED)
    include(CTest)
    if(BUILD_TESTING)
        find_package(GTest REQUIRED)
        set(GOODNET_BUILD_TESTS ON)
    endif()
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED libzstd)

add_library(goodnet_zstd_decompress_objects OBJECT
    zstd_decompress.cpp
)
target_link_libraries(goodnet_zstd_decompress_objects
    PUBLIC  GoodNet::sdk
    PRIVATE ${ZSTD_LIBRARIES})
target_include_directories(goodnet_zstd_decompress_objects
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${ZSTD_INCLUDE_DIRS})
target_compile_features(goodnet_zstd_decompress_objects PUBLIC cxx_std_23)
target_compile_options(goodnet_zstd_decompress_objects PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter)
set_target_properties(goodnet_zstd_decompress_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

add_plugin(goodnet_handler_zstd_decompress
    plugin_entry.cpp
)
target_link_libraries(goodnet_handler_zstd_decompress
    PRIVATE goodnet_zstd_decompress_objects ${ZSTD_LIBRARIES})
