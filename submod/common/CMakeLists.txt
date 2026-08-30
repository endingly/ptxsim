add_library(ptxsim_common INTERFACE)
add_library(ptxsim::common ALIAS ptxsim_common)
set_target_properties(ptxsim_common PROPERTIES EXPORT_NAME common)
target_compile_features(ptxsim_common INTERFACE cxx_std_23)
