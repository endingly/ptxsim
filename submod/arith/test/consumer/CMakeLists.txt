cmake_minimum_required(VERSION 3.28)
project(ptxsim_arith_external_consumer LANGUAGES CXX)

# The default core package must not discover the optional frontend.
set(CMAKE_DISABLE_FIND_PACKAGE_ptx_frontend TRUE)
find_package(ptxsim CONFIG REQUIRED)
find_package(ptxsim CONFIG REQUIRED OPTIONAL_COMPONENTS lowering)
if(ptxsim_lowering_FOUND)
    message(FATAL_ERROR "optional lowering unexpectedly loaded with frontend disabled")
endif()

add_executable(ptxsim_arith_external_consumer main.cpp)
target_link_libraries(ptxsim_arith_external_consumer PRIVATE
    ptxsim::arith
    ptxsim::common
    ptxsim::exec_ir
    ptxsim::program
    ptxsim::state
    ptxsim::runtime)

if(PTXSIM_CONSUMER_ENABLE_SANITIZERS)
    target_compile_options(ptxsim_arith_external_consumer PRIVATE
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer)
    target_link_options(ptxsim_arith_external_consumer PRIVATE
        -fsanitize=address
        -fsanitize=undefined)
endif()
