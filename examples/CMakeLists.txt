
add_executable(examples-channel channel.cpp)
target_link_libraries(examples-channel PRIVATE colite::colite CONAN_PKG::folly)

add_executable(examples-yield yield.cpp)
target_link_libraries(examples-yield PRIVATE colite::colite CONAN_PKG::folly)