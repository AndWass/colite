
add_executable(examples-channel channel.cpp)
target_link_libraries(examples-channel PRIVATE colite::colite CONAN_PKG::folly)