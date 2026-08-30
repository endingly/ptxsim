cmake_minimum_required(VERSION 3.28)
project(ptxsim_arith_external_consumer LANGUAGES CXX)

find_package(ptxsim CONFIG REQUIRED)

add_executable(ptxsim_arith_external_consumer main.cpp)
target_link_libraries(ptxsim_arith_external_consumer PRIVATE
    ptxsim::arith
    ptxsim::common
    ptxsim::exec_ir
    ptxsim::program)

if(PTXSIM_CONSUMER_ENABLE_SANITIZERS)
    target_compile_options(ptxsim_arith_external_consumer PRIVATE
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer)
    target_link_options(ptxsim_arith_external_consumer PRIVATE
        -fsanitize=address
        -fsanitize=undefined)
endif()
