set(RR_DOC_THEME default CACHE INTERNAL "Theme for documentation.")
set(RR_DOC_THEME_DIR "" CACHE INTERNAL "Theme directory for documentation.")

set(RR_DOC_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/tig/docs/build")
set(RR_DOC_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/tig/docs/doctrees")
set(RR_DOC_TARGET_DIR ${INSTALL_DOC_DIR})

# gets rid of warning
file(MAKE_DIRECTORY "${RR_DOC_BUILD_DIR}/_static")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/conf.py.in"
    "${RR_DOC_BUILD_DIR}/conf.py"
    @ONLY)

add_custom_target(rr_docs
    ${SPHINX_EXECUTABLE}
    -q -b html
    -c "${RR_DOC_BUILD_DIR}"
    -d "${RR_DOC_CACHE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${RR_DOC_TARGET_DIR}"
    COMMENT "Building HTML documentation with Sphinx")






