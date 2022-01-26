
find_program (CCACHE_FOUND ccache)
if (USE_CCACHE AND CCACHE_FOUND AND NOT CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache" AND NOT CMAKE_CXX_COMPILER MATCHES "ccache")
    execute_process (COMMAND ${CCACHE_FOUND} "-V" OUTPUT_VARIABLE CCACHE_VERSION)
    execute_process (COMMAND ${CCACHE_FOUND} "-p" OUTPUT_VARIABLE CCACHE_CONFIG)
    string (REGEX REPLACE "ccache version ([0-9\\.]+).*" "\\1" CCACHE_VERSION ${CCACHE_VERSION})

    message (STATUS "Using ccache: ${CCACHE_FOUND}, version ${CCACHE_VERSION}")
    message (STATUS "Show ccache config:")
    message ("${CCACHE_CONFIG}")
    set_property (GLOBAL PROPERTY RULE_LAUNCH_COMPILE ${CCACHE_FOUND})
    set_property (GLOBAL PROPERTY RULE_LAUNCH_LINK ${CCACHE_FOUND})
else ()
    message (STATUS "Not using ccache ${CCACHE_FOUND}, USE_CCACHE=${USE_CCACHE}")
endif ()
