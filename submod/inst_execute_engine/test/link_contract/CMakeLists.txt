cmake_minimum_required(VERSION 3.28)
project(ptxsim_inst_execute_engine_link_contract LANGUAGES CXX)

# Match the parent project and keep ordinary translation units cacheable.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

find_package(ptxsim CONFIG REQUIRED COMPONENTS
    arith execution_model memory runtime exec_ir inst_execute_engine)

get_filename_component(PTXSIM_GENERATED_DIR
    "${PTXSIM_GENERATED_HEADER}" DIRECTORY)
file(GLOB PTXSIM_SEMANTICS_SOURCES CONFIGURE_DEPENDS
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src/semantics/*.cpp")
set(PTXSIM_ADD_SEMANTICS_SOURCE
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src/semantics/add_semantics.cpp")

function(configure_link_contract_target target)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_include_directories(${target} PRIVATE
        $<TARGET_PROPERTY:ptxsim::inst_execute_engine,INTERFACE_INCLUDE_DIRECTORIES>
        "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src"
        "${PTXSIM_GENERATED_DIR}")
    target_link_libraries(${target} PRIVATE
        ptxsim::arith ptxsim::exec_ir ptxsim::runtime)
    set(PTXSIM_LINK_CONTRACT_SANITIZER_FLAGS)
    if(PTXSIM_ENABLE_ASAN)
        list(APPEND PTXSIM_LINK_CONTRACT_SANITIZER_FLAGS -fsanitize=address)
    endif()
    if(PTXSIM_ENABLE_UBSAN)
        list(APPEND PTXSIM_LINK_CONTRACT_SANITIZER_FLAGS -fsanitize=undefined)
    endif()
    if(PTXSIM_LINK_CONTRACT_SANITIZER_FLAGS)
        target_compile_options(${target} PRIVATE
            ${PTXSIM_LINK_CONTRACT_SANITIZER_FLAGS}
            -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            ${PTXSIM_LINK_CONTRACT_SANITIZER_FLAGS})
    endif()
endfunction()

set(PTXSIM_LINK_CONTRACT_COMMON_SOURCES
    "${PTXSIM_GENERATED_SOURCE}"
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src/resource_resolution.cpp"
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src/mov_preparation.cpp"
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src/memory_preparation.cpp"
    "${PTXSIM_SOURCE_DIR}/submod/inst_execute_engine/src/barrier_preparation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/missing_add_semantics.cc")
# Both links consume the same object files; only the positive control supplies
# Add semantics. Object inclusion keeps unresolved references visible to the linker.
set(PTXSIM_SEMANTICS_SOURCES_WITHOUT_ADD ${PTXSIM_SEMANTICS_SOURCES})
list(REMOVE_ITEM PTXSIM_SEMANTICS_SOURCES_WITHOUT_ADD
    "${PTXSIM_ADD_SEMANTICS_SOURCE}")
add_library(ptxsim_inst_execute_engine_link_contract_objects OBJECT
    ${PTXSIM_LINK_CONTRACT_COMMON_SOURCES}
    ${PTXSIM_SEMANTICS_SOURCES_WITHOUT_ADD})
configure_link_contract_target(ptxsim_inst_execute_engine_link_contract_objects)

add_executable(ptxsim_inst_execute_engine_link_contract_complete
    $<TARGET_OBJECTS:ptxsim_inst_execute_engine_link_contract_objects>
    "${PTXSIM_ADD_SEMANTICS_SOURCE}")
configure_link_contract_target(ptxsim_inst_execute_engine_link_contract_complete)

add_executable(ptxsim_inst_execute_engine_link_contract_missing_add
    $<TARGET_OBJECTS:ptxsim_inst_execute_engine_link_contract_objects>)
configure_link_contract_target(ptxsim_inst_execute_engine_link_contract_missing_add)
