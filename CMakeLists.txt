cmake_minimum_required(VERSION 3.19)
project(colite)

include(CTest)

include(${CMAKE_CURRENT_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

add_library(colite INTERFACE)
target_include_directories(colite INTERFACE include)
target_compile_features(colite INTERFACE cxx_std_20)

add_library(colite::colite ALIAS colite)

if(BUILD_TESTING)
    add_subdirectory(examples)
    add_subdirectory(tests)
endif()