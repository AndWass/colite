cmake_minimum_required(VERSION 3.19)
project(colite)

set(CMAKE_CXX_STANDARD 20)

include(CTest)

include(${CMAKE_CURRENT_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

add_library(colite INTERFACE)
target_include_directories(colite INTERFACE include)

add_library(colite::colite ALIAS colite)

if(BUILD_TESTING)
    add_subdirectory(examples)
    add_subdirectory(tests)
endif()