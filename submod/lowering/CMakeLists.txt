find_package(ptx_frontend CONFIG QUIET)

if(ptx_frontend_FOUND)
    add_library(ptxsim_lowering INTERFACE)
    add_library(ptxsim::lowering ALIAS ptxsim_lowering)
    set_target_properties(ptxsim_lowering PROPERTIES EXPORT_NAME lowering)
    target_link_libraries(ptxsim_lowering INTERFACE
        ptxsim::exec_ir
        ptxsim::program
        ptx_frontend::resolved_ir)

    if(BUILD_TESTING)
        add_executable(test_ptxsim_frontend_lowering_smoke
            test/frontend_lowering_smoke.cpp)
        target_link_libraries(test_ptxsim_frontend_lowering_smoke
            PRIVATE ptxsim::lowering)
        ptxsim_enable_sanitizers(test_ptxsim_frontend_lowering_smoke)
        add_test(NAME ptxsim_frontend_lowering_smoke
            COMMAND test_ptxsim_frontend_lowering_smoke)
    endif()
endif()
