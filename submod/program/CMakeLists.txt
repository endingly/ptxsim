add_library(ptxsim_program INTERFACE)
add_library(ptxsim::program ALIAS ptxsim_program)
set_target_properties(ptxsim_program PROPERTIES EXPORT_NAME program)
target_link_libraries(ptxsim_program INTERFACE ptxsim::common ptxsim::exec_ir)
