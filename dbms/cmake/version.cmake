# This strings autochanged from release_lib.sh:
set(VERSION_DESCRIBE v1.1.54381-testing)
set(VERSION_REVISION 54381)
set(VERSION_GITHASH af82c78a45b6a6f136e10bb2e7ca9b936d09a46c)
# end of autochange

set (VERSION_MAJOR 1)
set (VERSION_MINOR 1)
set (VERSION_PATCH ${VERSION_REVISION})
set (VERSION_EXTRA "")
set (VERSION_TWEAK "")

set (VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
if (VERSION_TWEAK)
    set(VERSION_STRING "${VERSION_STRING}.${VERSION_TWEAK}")
endif ()
if (VERSION_EXTRA)
    set(VERSION_STRING "${VERSION_STRING}${VERSION_EXTRA}")
endif ()

set (VERSION_FULL "${PROJECT_NAME} ${VERSION_STRING}")

if (APPLE)
    # dirty hack: ld: malformed 64-bit a.b.c.d.e version number: 1.1.54160
    math (EXPR VERSION_SO1 "${VERSION_REVISION}/255")
    math (EXPR VERSION_SO2 "${VERSION_REVISION}%255")
    set (VERSION_SO "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_SO1}.${VERSION_SO2}")
else ()
    set (VERSION_SO "${VERSION_STRING}")
endif ()

set (TIFLASH_NAME "TiFlash")

# Semantic version.
set (TIFLASH_VERSION_MAJOR 4)
set (TIFLASH_VERSION_MINOR 1)
set (TIFLASH_VERSION_REVISION 0)
set (TIFLASH_VERSION "${TIFLASH_VERSION_MAJOR}.${TIFLASH_VERSION_MINOR}.${TIFLASH_VERSION_REVISION}")

# Release version that follows PD/TiKV/TiDB convention.
execute_process(
  COMMAND git describe --tags --dirty --always
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TIFLASH_RELEASE_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )

set (TIFLASH_EDITION $ENV{TIFLASH_EDITION})
if (NOT TIFLASH_EDITION)
    set (TIFLASH_EDITION Community)
endif ()

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TIFLASH_GIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )

execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TIFLASH_GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  )

execute_process(
  COMMAND date -u "+%Y-%m-%d %H:%M:%S"
  OUTPUT_VARIABLE TIFLASH_UTC_BUILD_TIME
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

set (TIFLASH_PROFILE ${CMAKE_BUILD_TYPE})
