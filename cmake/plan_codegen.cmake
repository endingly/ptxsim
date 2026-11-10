include_guard(GLOBAL)

# Compute complete generated output membership only when this generator's own
# input contents change. A reconfigure caused by another emitter reuses the
# existing plan, preserving independent generator dependency closures.
function(ptxsim_plan_codegen module output_dir plan_file)
    set(plan_inputs ${ARGN})
    list(SORT plan_inputs)
    set(plan_identity "${module}\n${Python3_EXECUTABLE}\n${output_dir}\n")
    foreach(plan_input IN LISTS plan_inputs)
        file(SHA256 "${plan_input}" plan_input_hash)
        string(APPEND plan_identity "${plan_input}:${plan_input_hash}\n")
    endforeach()
    string(SHA256 plan_key "${plan_identity}")
    set(plan_key_file "${plan_file}.sha256")
    set(previous_plan_key "")
    if(EXISTS "${plan_key_file}")
        file(READ "${plan_key_file}" previous_plan_key)
    endif()
    if(NOT EXISTS "${plan_file}" OR NOT previous_plan_key STREQUAL plan_key)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m "${module}"
                    --output-dir "${output_dir}"
                    --cmake-plan-output "${plan_file}"
            RESULT_VARIABLE plan_result
            ERROR_VARIABLE plan_error)
        if(NOT plan_result EQUAL 0)
            message(FATAL_ERROR "Artifact planning failed for ${module}: ${plan_error}")
        endif()
        file(WRITE "${plan_key_file}" "${plan_key}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${plan_inputs})
endfunction()
