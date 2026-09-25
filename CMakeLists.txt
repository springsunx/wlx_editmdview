cmake_minimum_required(VERSION 3.24)

project(EditMdView VERSION 0.5.9 LANGUAGES C CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "EditMdView is a Windows-only Total Commander WLX plugin")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "The first release is built and tested with MSVC")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(EDITMDVIEW_DEPS_ROOT "${CMAKE_SOURCE_DIR}/.deps/src" CACHE PATH "Extracted dependency root")
set(SCINTILLA_ROOT "${EDITMDVIEW_DEPS_ROOT}/scintilla/scintilla")
set(LEXILLA_ROOT "${EDITMDVIEW_DEPS_ROOT}/lexilla/lexilla")
set(MD4C_ROOT "${EDITMDVIEW_DEPS_ROOT}/md4c/md4c-release-0.5.3")
set(WEBVIEW2_ROOT "${EDITMDVIEW_DEPS_ROOT}/webview2")

foreach(required_path IN ITEMS
        "${SCINTILLA_ROOT}/include/Scintilla.h"
        "${LEXILLA_ROOT}/lexers/LexMarkdown.cxx"
        "${MD4C_ROOT}/src/md4c-html.c"
        "${WEBVIEW2_ROOT}/build/native/include/WebView2.h")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Dependencies are missing. Run scripts/build.ps1 once. Missing: ${required_path}")
    endif()
endforeach()

file(GLOB SCINTILLA_CORE_SOURCES CONFIGURE_DEPENDS "${SCINTILLA_ROOT}/src/*.cxx")
set(SCINTILLA_WIN_SOURCES
    "${SCINTILLA_ROOT}/win32/HanjaDic.cxx"
    "${SCINTILLA_ROOT}/win32/ListBox.cxx"
    "${SCINTILLA_ROOT}/win32/PlatWin.cxx"
    "${SCINTILLA_ROOT}/win32/ScintillaWin.cxx"
    "${SCINTILLA_ROOT}/win32/SurfaceD2D.cxx"
    "${SCINTILLA_ROOT}/win32/SurfaceGDI.cxx")

add_library(scintilla_static STATIC ${SCINTILLA_CORE_SOURCES} ${SCINTILLA_WIN_SOURCES})
target_include_directories(scintilla_static PUBLIC "${SCINTILLA_ROOT}/include" PRIVATE "${SCINTILLA_ROOT}/src")
target_compile_definitions(scintilla_static PRIVATE UNICODE _UNICODE NOMINMAX)
target_compile_options(scintilla_static PRIVATE /utf-8 /W3)

file(GLOB LEXILLA_CORE_SOURCES CONFIGURE_DEPENDS "${LEXILLA_ROOT}/lexlib/*.cxx")
add_library(lexilla_markdown STATIC
    ${LEXILLA_CORE_SOURCES}
    "${LEXILLA_ROOT}/lexers/LexBash.cxx"
    "${LEXILLA_ROOT}/lexers/LexBatch.cxx"
    "${LEXILLA_ROOT}/lexers/LexCmake.cxx"
    "${LEXILLA_ROOT}/lexers/LexConf.cxx"
    "${LEXILLA_ROOT}/lexers/LexCPP.cxx"
    "${LEXILLA_ROOT}/lexers/LexCSS.cxx"
    "${LEXILLA_ROOT}/lexers/LexHTML.cxx"
    "${LEXILLA_ROOT}/lexers/LexJSON.cxx"
    "${LEXILLA_ROOT}/lexers/LexLisp.cxx"
    "${LEXILLA_ROOT}/lexers/LexLua.cxx"
    "${LEXILLA_ROOT}/lexers/LexMake.cxx"
    "${LEXILLA_ROOT}/lexers/LexMarkdown.cxx"
    "${LEXILLA_ROOT}/lexers/LexPowerShell.cxx"
    "${LEXILLA_ROOT}/lexers/LexProps.cxx"
    "${LEXILLA_ROOT}/lexers/LexPython.cxx"
    "${LEXILLA_ROOT}/lexers/LexRust.cxx"
    "${LEXILLA_ROOT}/lexers/LexSQL.cxx"
    "${LEXILLA_ROOT}/lexers/LexYAML.cxx")
target_include_directories(lexilla_markdown PUBLIC
    "${LEXILLA_ROOT}/include"
    PRIVATE "${LEXILLA_ROOT}/lexlib" "${SCINTILLA_ROOT}/include")
target_compile_definitions(lexilla_markdown PRIVATE LEXILLA_NO_EXPORT NOMINMAX)
target_compile_options(lexilla_markdown PRIVATE /utf-8 /W3)

add_library(md4c_static STATIC
    "${MD4C_ROOT}/src/md4c.c"
    "${MD4C_ROOT}/src/md4c-html.c"
    "${MD4C_ROOT}/src/entity.c")
target_include_directories(md4c_static PUBLIC "${MD4C_ROOT}/src")
target_compile_definitions(md4c_static PRIVATE MD4C_USE_UTF8)

set(WEBVIEW2_INCLUDE "${WEBVIEW2_ROOT}/build/native/include")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(EDITMDVIEW_ARCH "x64")
    set(EDITMDVIEW_WLX_SUFFIX ".wlx64")
else()
    set(EDITMDVIEW_ARCH "x86")
    set(EDITMDVIEW_WLX_SUFFIX ".wlx")
endif()
set(WEBVIEW2_STATIC_LIB
    "${WEBVIEW2_ROOT}/build/native/${EDITMDVIEW_ARCH}/WebView2LoaderStatic.lib")
if(NOT EXISTS "${WEBVIEW2_STATIC_LIB}")
    message(FATAL_ERROR "WebView2 static loader is missing for ${EDITMDVIEW_ARCH}: ${WEBVIEW2_STATIC_LIB}")
endif()

add_library(editmdview SHARED
    src/app_window.cpp
    src/document.cpp
    src/editor.cpp
    src/html_preview.cpp
    src/markdown.cpp
    src/plugin.cpp
    src/preview.cpp
    src/scite_properties.cpp
    src/session_state.cpp
    src/text_detection.cpp
    src/resources.rc
    src/editmdview.def)

target_include_directories(editmdview PRIVATE src "${WEBVIEW2_INCLUDE}" "${LEXILLA_ROOT}/lexlib")
target_compile_definitions(editmdview PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(editmdview PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
target_link_options(editmdview PRIVATE /INCREMENTAL:NO)
target_link_libraries(editmdview PRIVATE
    scintilla_static
    lexilla_markdown
    md4c_static
    "${WEBVIEW2_STATIC_LIB}"
    advapi32 comctl32 comdlg32 d2d1 dwrite gdi32 imm32 ole32 oleaut32 shell32 shlwapi user32 version windowscodecs)

set_target_properties(editmdview PROPERTIES
    OUTPUT_NAME "EditMdView"
    PREFIX ""
    SUFFIX "${EDITMDVIEW_WLX_SUFFIX}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(editmdview_save_helper WIN32
    src/save_helper.cpp
    src/save_helper.rc)
target_compile_definitions(editmdview_save_helper PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(editmdview_save_helper PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus)
target_link_options(editmdview_save_helper PRIVATE /INCREMENTAL:NO)
target_link_libraries(editmdview_save_helper PRIVATE shell32)
set_target_properties(editmdview_save_helper PROPERTIES
    OUTPUT_NAME "EditMdViewSave"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(wlx_harness WIN32 tools/wlx_harness.cpp)
target_include_directories(wlx_harness PRIVATE "${SCINTILLA_ROOT}/include" "${LEXILLA_ROOT}/include")
target_compile_definitions(wlx_harness PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(wlx_harness PRIVATE /utf-8 /W4 /permissive-)
target_link_libraries(wlx_harness PRIVATE comctl32 ole32 shell32 user32)
set_target_properties(wlx_harness PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET wlx_harness POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/samples/demo.md" "$<TARGET_FILE_DIR:wlx_harness>/demo.md"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/samples/demo.html" "$<TARGET_FILE_DIR:wlx_harness>/demo.html"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/samples/demo.md" "$<TARGET_FILE_DIR:wlx_harness>/save-caret.txt")
add_custom_command(TARGET wlx_harness POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_SOURCE_DIR}/samples/demo.md" "$<TARGET_FILE_DIR:wlx_harness>/external-reload.md")

include(CTest)
if(BUILD_TESTING)
    add_executable(core_tests tests/core_tests.cpp src/document.cpp src/html_preview.cpp src/markdown.cpp
        src/scite_properties.cpp src/session_state.cpp src/text_detection.cpp)
    target_include_directories(core_tests PRIVATE src)
    target_compile_definitions(core_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        EDITMDVIEW_CONFIG_DIR=L"${CMAKE_SOURCE_DIR}/config")
    target_compile_options(core_tests PRIVATE /utf-8 /W4 /permissive-)
    target_link_libraries(core_tests PRIVATE md4c_static user32)
    add_test(NAME core_tests COMMAND core_tests)
    add_test(NAME bundled_config_scope
        COMMAND "${CMAKE_COMMAND}" -DCONFIG_DIR=${CMAKE_SOURCE_DIR}/config
            -P "${CMAKE_SOURCE_DIR}/tests/check_bundled_config.cmake")
    add_test(NAME preview_assets
        COMMAND "${CMAKE_COMMAND}" -DASSET_DIR=${CMAKE_SOURCE_DIR}/src/assets
            -P "${CMAKE_SOURCE_DIR}/tests/check_preview_assets.cmake")
    add_test(NAME repeated_open
        COMMAND wlx_harness "$<TARGET_FILE:editmdview>" "${CMAKE_SOURCE_DIR}/CMakeLists.txt" 10)
    add_test(NAME list_load_next_keeps_position
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_STATE_SECOND=${CMAKE_SOURCE_DIR}/src/editor.cpp"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/src/app_window.cpp" 1)
    add_test(NAME list_load_next_restores_preview_mode
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_STATE_SECOND=${CMAKE_SOURCE_DIR}/src/editor.cpp"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/samples/demo.md" 1)
    add_test(NAME markdown_default_preview
        COMMAND wlx_harness "$<TARGET_FILE:editmdview>" "${CMAKE_SOURCE_DIR}/samples/demo.md" 2)
    add_test(NAME html_default_preview
        COMMAND wlx_harness "$<TARGET_FILE:editmdview>" "${CMAKE_SOURCE_DIR}/samples/demo.html" 2)
    add_test(NAME cpp_highlighting
        COMMAND wlx_harness "$<TARGET_FILE:editmdview>" "${CMAKE_SOURCE_DIR}/src/editor.cpp" 2)
    add_test(NAME scite_properties_integration
        COMMAND "${CMAKE_COMMAND}" -E env
            "SciTE_HOME=${CMAKE_SOURCE_DIR}/tests/scite"
            "SciTE_USERHOME=${CMAKE_SOURCE_DIR}/tests/scite/user"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/tests/scite/config_test.foo" 2)
    add_test(NAME scite_full_editor_config
        COMMAND "${CMAKE_COMMAND}" -E env
            "SciTE_HOME=${CMAKE_SOURCE_DIR}/config"
            "SciTE_USERHOME=${CMAKE_SOURCE_DIR}/config"
            "EDITMDVIEW_EXPECT_FONT=Consolas"
            "EDITMDVIEW_EXPECT_SIZE=13"
            "EDITMDVIEW_EXPECT_FULL_CONFIG=1"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/src/editor.cpp" 2)
    add_test(NAME save_cleanup_keeps_caret
        COMMAND "${CMAKE_COMMAND}" -E env
            "SciTE_HOME=${CMAKE_SOURCE_DIR}/config"
            "SciTE_USERHOME=${CMAKE_SOURCE_DIR}/config"
            "EDITMDVIEW_EXPECT_SAVE_CARET=1"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "$<TARGET_FILE_DIR:wlx_harness>/save-caret.txt" 2)
    add_test(NAME external_file_auto_reload
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_EXPECT_EXTERNAL_RELOAD=1"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "$<TARGET_FILE_DIR:wlx_harness>/external-reload.md" 1)
    add_test(NAME background_large_markdown_preview
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_EXPECT_BACKGROUND_PREVIEW=1"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "$<TARGET_FILE_DIR:wlx_harness>/background-preview.md" 1)
    add_test(NAME automatic_recovery_snapshot
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_EXPECT_RECOVERY_SNAPSHOT=1"
            "EDITMDVIEW_DATA_DIR=${CMAKE_BINARY_DIR}/recovery-test-data"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/samples/demo.md" 1)
    add_test(NAME persistent_document_view_state
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_EXPECT_PERSISTENT_STATE=1"
            "EDITMDVIEW_DATA_DIR=${CMAKE_BINARY_DIR}/persistent-state-test-data"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/samples/demo.md" 1)
    add_test(NAME automatic_configuration_reload
        COMMAND "${CMAKE_COMMAND}" -E env
            "EDITMDVIEW_EXPECT_CONFIG_RELOAD=1"
            "EDITMDVIEW_DATA_DIR=${CMAKE_BINARY_DIR}/configuration-reload-test-data"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/CMakeLists.txt" 1)
    add_test(NAME scite_markdown_visual_config
        COMMAND "${CMAKE_COMMAND}" -E env
            "SciTE_HOME=${CMAKE_SOURCE_DIR}/config"
            "SciTE_USERHOME=${CMAKE_SOURCE_DIR}/config"
            "EDITMDVIEW_EXPECT_SCITE_VISUALS=1"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/samples/demo.md" 2)
    add_test(NAME same_process_plugin_reload
        COMMAND wlx_harness "$<TARGET_FILE:editmdview>" "${CMAKE_SOURCE_DIR}/samples/demo.lsp" 2)
    add_test(NAME scite_lisp_palette
        COMMAND "${CMAKE_COMMAND}" -E env
            "SciTE_HOME=${CMAKE_SOURCE_DIR}/config"
            "SciTE_USERHOME=${CMAKE_SOURCE_DIR}/config"
            "EDITMDVIEW_EXPECT_LISP_PALETTE=1"
            "$<TARGET_FILE:wlx_harness>" "$<TARGET_FILE:editmdview>"
            "${CMAKE_SOURCE_DIR}/samples/demo.lsp" 2)
    add_test(NAME save_helper_replace
        COMMAND "${CMAKE_COMMAND}"
            -DHELPER=$<TARGET_FILE:editmdview_save_helper>
            -DWORK_DIR=${CMAKE_BINARY_DIR}/save-helper-test
            -P "${CMAKE_SOURCE_DIR}/tests/check_save_helper.cmake")
endif()
