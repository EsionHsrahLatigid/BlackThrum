include_guard(GLOBAL)

set(_ehl_copy_plugin_after_build_default OFF)
if(APPLE AND NOT DEFINED ENV{CI})
    set(_ehl_copy_plugin_after_build_default ON)
endif()
option(EHL_COPY_PLUGIN_AFTER_BUILD
    "Copy staged VST3 and Audio Unit bundles into the current user's plugin folders"
    ${_ehl_copy_plugin_after_build_default})

set(EHL_USER_VST3_DIR
    "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
    CACHE PATH
    "Destination for locally built VST3 bundles")
set(EHL_USER_AU_DIR
    "$ENV{HOME}/Library/Audio/Plug-Ins/Components"
    CACHE PATH
    "Destination for locally built Audio Unit bundles")

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
            "-DEHL_COPY_PLUGIN_AFTER_BUILD=${EHL_COPY_PLUGIN_AFTER_BUILD}"
            "-DEHL_USER_VST3_DIR=${EHL_USER_VST3_DIR}"
            "-DEHL_USER_AU_DIR=${EHL_USER_AU_DIR}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StageYupProducts.cmake"
        DEPENDS ${stage_dependencies}
        COMMENT "Staging ${EHL_PRODUCT} products under ${EHL_OUTPUT_ROOT}/${EHL_PROFILE}"
        VERBATIM)
endfunction()
