set(web_ui_build_dir "${CMAKE_BINARY_DIR}/web-ui")


set(srcs "")

if(CONFIG_ESP32M_UI)
  list(APPEND srcs 
  "${web_ui_build_dir}/dist/index.html.S"
  "${web_ui_build_dir}/dist/main.js.S"
  )
endif()

idf_component_register(SRCS ${srcs} REQUIRES esp32m)

if(CONFIG_ESP32M_UI)
    set_source_files_properties(${srcs} PROPERTIES GENERATED TRUE)
    target_include_directories(${COMPONENT_LIB} PUBLIC ${web_ui_build_dir}) 
    add_dependencies(${COMPONENT_LIB} esp32m_web_ui)
endif()
