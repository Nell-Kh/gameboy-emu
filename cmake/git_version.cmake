# Writes OUT, a header defining GB_VERSION as `git describe --tags --dirty`
# of SOURCE_DIR (for example "v1.0.0" on a tagged commit, or
# "v1.0.0-3-g1a2b3c4-dirty" after it), or FALLBACK (the project version) when
# git or the history is not there. Run at every build, not only at configure,
# so the version never lags behind the commit; the header is rewritten only
# when the version changes, so nothing recompiles otherwise.
execute_process(
  COMMAND git describe --tags --dirty --always
  WORKING_DIRECTORY "${SOURCE_DIR}"
  OUTPUT_VARIABLE version
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0 OR version STREQUAL "")
  set(version "${FALLBACK}")
endif()
set(content "#pragma once\n#define GB_VERSION \"${version}\"\n")
if(EXISTS "${OUT}")
  file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
  file(WRITE "${OUT}" "${content}")
endif()
