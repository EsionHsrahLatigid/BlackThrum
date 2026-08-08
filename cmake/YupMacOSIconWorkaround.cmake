# YUP commit 9a1c9bc generates an iconset that iconutil rejects with
# "Invalid Iconset" on macOS 26.4. The same source PNG converts successfully
# when sips writes ICNS directly, so override only the private conversion hook.

find_program(BLACKTHRUM_SIPS_EXECUTABLE sips REQUIRED)

function(_yup_convert_png_to_icns png_path icons_path output_variable)
    if(NOT EXISTS "${png_path}")
        message(FATAL_ERROR "Standalone icon source does not exist: ${png_path}")
    endif()

    set(output_icon_path "${icons_path}.icns")
    file(REMOVE "${output_icon_path}")

    execute_process(
        COMMAND "${BLACKTHRUM_SIPS_EXECUTABLE}"
                -s format icns "${png_path}" --out "${output_icon_path}"
        RESULT_VARIABLE conversion_result
        OUTPUT_VARIABLE conversion_output
        ERROR_VARIABLE conversion_error)

    if(NOT conversion_result EQUAL 0 OR NOT EXISTS "${output_icon_path}")
        message(FATAL_ERROR
            "Failed to convert standalone icon with sips (exit ${conversion_result}).\n"
            "stdout: ${conversion_output}\n"
            "stderr: ${conversion_error}")
    endif()

    set(${output_variable} "${output_icon_path}" PARENT_SCOPE)
endfunction()
