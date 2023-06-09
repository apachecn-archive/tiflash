if(NOT CURL_FOUND)
  find_package(CURL REQUIRED)
endif()

add_library(push
  ${PROMETHEUS_SRC_DIR}/push/src/gateway.cc
)

add_library(${PROJECT_NAME}::push ALIAS push)

target_link_libraries(push
  PUBLIC
    ${PROJECT_NAME}::core
  PRIVATE
    Threads::Threads
    ${CURL_LIBRARIES}
    $<$<AND:$<BOOL:UNIX>,$<NOT:$<BOOL:APPLE>>>:rt>
)

target_include_directories(push
  PUBLIC
    $<BUILD_INTERFACE:${PROMETHEUS_SRC_DIR}/push/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  PRIVATE
    ${CURL_INCLUDE_DIRS}
)

set_target_properties(push
  PROPERTIES
    OUTPUT_NAME ${PROJECT_NAME}-push
    DEFINE_SYMBOL PROMETHEUS_CPP_PUSH_EXPORTS
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
)

generate_export_header(push
  BASE_NAME ${PROJECT_NAME}-push
  EXPORT_FILE_NAME include/prometheus/detail/push_export.h
)

# install(
#   TARGETS push
#   EXPORT ${PROJECT_NAME}-targets
#   RUNTIME DESTINATION  ${CMAKE_INSTALL_BINDIR}
#   LIBRARY DESTINATION  ${CMAKE_INSTALL_LIBDIR}
#   ARCHIVE DESTINATION  ${CMAKE_INSTALL_LIBDIR}
#   INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# )

# install(
#   DIRECTORY include/ ${CMAKE_CURRENT_BINARY_DIR}/include/
#   DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
# )

if(ENABLE_TESTING)
  add_subdirectory(${PROMETHEUS_SRC_DIR}/push/tests ${CMAKE_CURRENT_BINARY_DIR}/push/tests)
endif()
