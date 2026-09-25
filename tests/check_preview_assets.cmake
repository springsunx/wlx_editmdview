file(SHA256 "${ASSET_DIR}/highlight.min.js" HIGHLIGHT_SHA256)
if(NOT HIGHLIGHT_SHA256 STREQUAL "8ab71eb09c51f501e5e25157d9cff100e46cc29bcbfc744d0b746d451fca7f53")
    message(FATAL_ERROR "Unexpected Highlight.js asset hash: ${HIGHLIGHT_SHA256}")
endif()

set(EXTRA_HIGHLIGHT_ASSETS
    "highlight-cmake.min.js|0cd6dc22b0b99dc53a4288ae30f760dab4da12ddde570f617be30a396b5b97ec"
    "highlight-dos.min.js|e184a6f9cead550b7b39b6114d17cafd08904557317622d7ea488aed01cf31ee"
    "highlight-lisp.min.js|ffdeab1afc214245015ef80500e4831712700d98c81c55e391325f39e01edc98"
    "highlight-powershell.min.js|fc298b3e0db362e531e6d58988b7a78f83da19df6b5d74db75bc961b2bfbfd3d"
    "highlight-properties.min.js|8a987022cc566fa5bfcd79058ec4ba010920d576fff809b92317295827402590")
foreach(ASSET IN LISTS EXTRA_HIGHLIGHT_ASSETS)
    string(REPLACE "|" ";" ASSET_PARTS "${ASSET}")
    list(GET ASSET_PARTS 0 ASSET_NAME)
    list(GET ASSET_PARTS 1 EXPECTED_SHA256)
    file(SHA256 "${ASSET_DIR}/${ASSET_NAME}" ACTUAL_SHA256)
    if(NOT ACTUAL_SHA256 STREQUAL EXPECTED_SHA256)
        message(FATAL_ERROR "Unexpected ${ASSET_NAME} hash: ${ACTUAL_SHA256}")
    endif()
endforeach()

file(READ "${ASSET_DIR}/preview.js" PREVIEW_SCRIPT)
foreach(REQUIRED_TEXT IN ITEMS
        "window.EditMdViewPreview"
        "window.hljs.highlightElement"
        "editmdview-copy-code:"
        "editmdview-find-result:"
        "CSS.highlights.set"
        "editmdview://locate/line/"
        "editmdview-scroll:")
    string(FIND "${PREVIEW_SCRIPT}" "${REQUIRED_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "Preview script is missing required behavior: ${REQUIRED_TEXT}")
    endif()
endforeach()

file(READ "${ASSET_DIR}/preview.css" PREVIEW_STYLE)
foreach(REQUIRED_TEXT IN ITEMS
        ".editmdview-code-tools"
        ".editmdview-copy-button"
        ".hljs-keyword"
        "::highlight(editmdview-find-current)"
        "prefers-color-scheme: dark")
    string(FIND "${PREVIEW_STYLE}" "${REQUIRED_TEXT}" FOUND_AT)
    if(FOUND_AT EQUAL -1)
        message(FATAL_ERROR "Preview style is missing required rule: ${REQUIRED_TEXT}")
    endif()
endforeach()
