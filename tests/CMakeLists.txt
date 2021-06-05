
enable_testing()

add_executable(colite-tests
        executor.cpp
        task.cpp
        yield.cpp
        channel.cpp
        )

target_link_libraries(colite-tests PRIVATE colite::colite CONAN_PKG::gtest CONAN_PKG::folly)
add_test(NAME colite-tests COMMAND colite-tests)