cmake_minimum_required(VERSION 3.20)

get_filename_component(OPENRIDE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../"
    ABSOLUTE)

add_subdirectory("${OPENRIDE_ROOT}"
                 "${CMAKE_CURRENT_BINARY_DIR}/openride-root")
