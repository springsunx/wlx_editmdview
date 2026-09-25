file(GLOB bundled_config RELATIVE "${CONFIG_DIR}" "${CONFIG_DIR}/*")
list(SORT bundled_config)
set(expected_config
    "SciTEGlobal.properties;SciTEUser.properties;conf.properties;cpp.properties;html.properties;lisp.properties")
if(NOT bundled_config STREQUAL expected_config)
    message(FATAL_ERROR "Bundled config must contain only ${expected_config}; found ${bundled_config}")
endif()

set(unsupported_pattern
    "(^|\\n)[ \\t]*(command\\.|ext\\.lua|output\\.|menu\\.|toolbar\\.|statusbar\\.|tabbar\\.|title\\.|export\\.|print\\.|api\\.|calltip\\.|code\\.page|locale\\.properties|save\\.session|position\\.)")
foreach(config_file IN LISTS bundled_config)
    file(READ "${CONFIG_DIR}/${config_file}" contents)
    if(contents MATCHES "${unsupported_pattern}")
        message(FATAL_ERROR "Unsupported SciTE setting remains in ${config_file}: ${CMAKE_MATCH_2}")
    endif()
endforeach()
