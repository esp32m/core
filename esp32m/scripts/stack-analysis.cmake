include_guard(GLOBAL)

function(esp32m_enable_stack_report component_dir)
    if(NOT CONFIG_ESP32M_STACK_REPORT OR TARGET esp32m_stack_report)
        return()
    endif()

    # The application report must never combine sidecars from independent
    # bootloader/TEE builds with the application's compiler invocations.
    idf_build_get_property(bootloader_build BOOTLOADER_BUILD)
    idf_build_get_property(esp_tee_build ESP_TEE_BUILD)
    if(BOOTLOADER_BUILD OR ESP_TEE_BUILD OR bootloader_build OR esp_tee_build)
        return()
    endif()

    set(has_stack_usage FALSE)
    foreach(language C CXX)
        if(CMAKE_${language}_COMPILER_ID STREQUAL "GNU")
            idf_build_set_property(${language}_COMPILE_OPTIONS "-fstack-usage" APPEND)
            set(has_stack_usage TRUE)
        endif()
    endforeach()
    if(NOT has_stack_usage)
        message(STATUS "esp32m stack report: skipped (requires a GNU C/C++ compiler)")
        return()
    endif()

    idf_build_get_property(build_dir BUILD_DIR)
    idf_build_get_property(python PYTHON)
    idf_build_get_property(elf EXECUTABLE GENERATOR_EXPRESSION)
    if(NOT build_dir OR NOT python)
        message(WARNING "esp32m stack report: unavailable without IDF BUILD_DIR/PYTHON properties")
        return()
    endif()

    # EXECUTABLE is populated later by idf_build_executable(). Resolve it at
    # generation time, exactly as ESP-IDF's binary-image target does. The file
    # dependency orders analysis after linking, without making it a link input.
    # An always-run target also refreshes reports after incremental/no-op builds
    # and script changes, and detects missing sidecars instead of retaining an
    # apparently current report. Its outputs are deliberately not ELF inputs.
    add_custom_target(esp32m_stack_report ALL
        COMMAND "${python}" "${component_dir}/scripts/stack-report.py"
            --build-dir "${build_dir}"
        DEPENDS "$<TARGET_FILE:$<GENEX_EVAL:${elf}>>"
            "${component_dir}/scripts/stack-report.py"
        BYPRODUCTS "${build_dir}/stack-usage.txt" "${build_dir}/stack-usage.json"
        WORKING_DIRECTORY "${build_dir}"
        COMMENT "Generating esp32m stack usage report"
        VERBATIM
    )

    # IDF 6.1 exposes the post-ELF stage explicitly. Older IDF builds still get
    # the ALL target above, ordered after the ELF; no SDK patch is required.
    if(COMMAND idf_build_add_post_elf_dependency)
        idf_build_add_post_elf_dependency("${CMAKE_PROJECT_NAME}.elf" esp32m_stack_report)
    endif()
    message(STATUS "esp32m stack report: ${build_dir}/stack-usage.txt")
endfunction()
