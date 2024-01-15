set(src_dirs ".")
set(include_dirs ".")
set(priv_requires "")

if(CONFIG_ESP32M_UI)
  list(APPEND priv_requires web-ui)
endif()

idf_component_register(SRC_DIRS ${src_dirs}
                       PRIV_INCLUDE_DIRS ${include_dirs}
                       PRIV_REQUIRES ${priv_requires})

