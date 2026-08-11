include_guard(GLOBAL)

function(ehl_yup_add_artifact_target)
    set(options)
    set(one_value_args
        TARGET
        PRODUCT
        SLUG
        PROFILE
        OUTPUT_ROOT
        STANDALONE_TARGET
        VST3_TARGET
        AU_TARGET)
    cmake_parse_arguments(EHL "${options}" "${one_value_args}" "" ${ARGN})

    foreach(required_arg TARGET PRODUCT SLUG STANDALONE_TARGET VST3_TARGET)
        if(NOT EHL_${required_arg})
            message(FATAL_ERROR "ehl_yup_add_artifact_target requires ${required_arg}")
        endif()
    endforeach()

    if(NOT EHL_PROFILE)
        set(EHL_PROFILE "plugin-release")
    endif()
    if(NOT EHL_OUTPUT_ROOT)
        set(EHL_OUTPUT_ROOT "${CMAKE_SOURCE_DIR}/artifacts")
    endif()

    set(stage_dependencies
        ${EHL_STANDALONE_TARGET}
        ${EHL_VST3_TARGET})
    set(expect_au OFF)
    if(APPLE AND EHL_AU_TARGET AND TARGET ${EHL_AU_TARGET})
        list(APPEND stage_dependencies ${EHL_AU_TARGET})
        set(expect_au ON)
    endif()

    add_custom_target(${EHL_TARGET}
        COMMAND "${CMAKE_COMMAND}"
            "-DEHL_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DEHL_OUTPUT_ROOT=${EHL_OUTPUT_ROOT}"
            "-DEHL_PRODUCT=${EHL_PRODUCT}"
            "-DEHL_SLUG=${EHL_SLUG}"
            "-DEHL_PROFILE=${EHL_PROFILE}"
            "-DEHL_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}"
            "-DEHL_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
            "-DEHL_CONFIG=$<CONFIG>"
            "-DEHL_EXPECT_AU=${expect_au}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageYupProducts.cmake"
        DEPENDS ${stage_dependencies}
        COMMENT "Staging ${EHL_PRODUCT} products under ${EHL_OUTPUT_ROOT}/${EHL_PROFILE}"
        VERBATIM)
endfunction()
