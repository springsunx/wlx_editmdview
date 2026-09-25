if(NOT DEFINED HELPER OR NOT EXISTS "${HELPER}")
    message(FATAL_ERROR "Save helper not found: ${HELPER}")
endif()
if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(source "${WORK_DIR}/staged content.txt")
set(target "${WORK_DIR}/protected target.txt")
file(WRITE "${source}" "new content\n")
file(WRITE "${target}" "old content\n")

execute_process(
    COMMAND "${HELPER}" --replace "${source}" "${target}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Save helper failed with exit code ${result}")
endif()

file(READ "${target}" content)
if(NOT content STREQUAL "new content\n")
    message(FATAL_ERROR "Save helper did not replace the target content")
endif()

file(REMOVE "${source}" "${target}")
