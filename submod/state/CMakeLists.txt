add_library(ptxsim_state INTERFACE)
add_library(ptxsim::state ALIAS ptxsim_state)
set_target_properties(ptxsim_state PROPERTIES EXPORT_NAME state)
target_link_libraries(ptxsim_state INTERFACE ptxsim::common)
