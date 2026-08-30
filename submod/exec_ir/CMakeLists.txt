add_library(ptxsim_exec_ir INTERFACE)
add_library(ptxsim::exec_ir ALIAS ptxsim_exec_ir)
set_target_properties(ptxsim_exec_ir PROPERTIES EXPORT_NAME exec_ir)
target_link_libraries(ptxsim_exec_ir INTERFACE ptxsim::common)
