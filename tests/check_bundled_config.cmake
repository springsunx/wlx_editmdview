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

# Keep every explicit bundled comment style connected to the shared comment
# typography. Runtime defaults cover other lexers; these checks protect the
# curated SciTE-compatible property files from silently dropping the font link.
file(READ "${CONFIG_DIR}/cpp.properties" cpp_properties)
foreach(fragment IN ITEMS
        "style.cpp.1=$(colour.code.comment.box),$(font.code.comment.box)"
        "style.cpp.23=fore:#659900,$(font.code.comment.line)"
        "style.cpp.24=$(colour.code.comment.doc),$(font.code.comment.doc)")
    string(FIND "${cpp_properties}" "${fragment}" fragment_position)
    if(fragment_position EQUAL -1)
        message(FATAL_ERROR "C/C++ comment style lost font.comment linkage: ${fragment}")
    endif()
endforeach()

file(READ "${CONFIG_DIR}/html.properties" html_properties)
foreach(fragment IN ITEMS
        "style.hypertext.9=fore:#808000,$(font.text.comment)"
        "style.hypertext.20=fore:#000000,back:#FFFFD0,$(font.text.comment)"
        "style.hypertext.29=fore:#808000,$(colour.hypertext.sgml.back),$(font.text.comment)"
        "style.hypertext.30=fore:#808000,back:#FF0000,$(font.text.comment)")
    string(FIND "${html_properties}" "${fragment}" fragment_position)
    if(fragment_position EQUAL -1)
        message(FATAL_ERROR "HTML comment style lost font.comment linkage: ${fragment}")
    endif()
endforeach()

file(READ "${CONFIG_DIR}/lisp.properties" lisp_properties)
string(FIND "${lisp_properties}"
    "style.lisp.1=$(colour.code.comment.box),$(font.code.comment.box),back:#C0C0C0"
    lisp_comment_position)
if(lisp_comment_position EQUAL -1)
    message(FATAL_ERROR "Lisp line comments must inherit font.comment")
endif()
