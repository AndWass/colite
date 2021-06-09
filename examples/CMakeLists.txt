
function(add_example NAME)
    add_executable(examples-${NAME} ${NAME}.cpp)
    target_link_libraries(examples-${NAME} PRIVATE colite::colite CONAN_PKG::folly)
endfunction()

add_example(channel)
add_example(yield)
add_example(mutex)

add_custom_target(all-examples)
add_dependencies(all-examples examples-channel examples-yield examples-mutex)
