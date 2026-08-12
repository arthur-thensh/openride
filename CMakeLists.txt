cmake_minimum_required(VERSION 3.20)
project(OpenRide VERSION 0.14.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(OPENRIDE_BUILD_TESTS "Build OpenRide core tests" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL3 is missing. Run: ./scripts/bootstrap_sdl.sh\n"
        "Expected: vendor/SDL/CMakeLists.txt")
endif()

find_package(SQLite3 REQUIRED)
find_package(ZLIB REQUIRED)
add_subdirectory(vendor/SDL EXCLUDE_FROM_ALL)

add_library(openride_core STATIC
    src/core/map_camera.c
    src/core/map_selection.c
    src/core/routing_graph.c
    src/core/routing_spatial_index.c
    src/core/routing_segment_index.c
    src/core/pathfinder.c
    src/core/routing_engine.c
    src/core/loop_generator.c
    src/core/navigation_engine.c
    src/core/navigation_instructions.c
    src/core/gps_simulator.c
    src/osm/osm_import.c
    src/map/mbtiles.c
    src/map/mvt.c
    src/map/map_style.c
)

target_include_directories(openride_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(openride_core
    PUBLIC
        SQLite::SQLite3
        ZLIB::ZLIB
)

if(MSVC)
    target_compile_options(openride_core PRIVATE /W4)
else()
    target_compile_options(openride_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openride
    src/main.c
    src/map/map_renderer.c
    src/map/vector_map_renderer.c
)

target_include_directories(openride
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(openride
    PRIVATE
        openride_core
        SDL3::SDL3
        ZLIB::ZLIB
)

if(MSVC)
    target_compile_options(openride PRIVATE /W4)
else()
    target_compile_options(openride PRIVATE -Wall -Wextra -Wpedantic)
endif()


add_executable(openride_osm_import
    src/tools/osm_import_main.c
)

target_link_libraries(openride_osm_import
    PRIVATE
        openride_core
)

if(MSVC)
    target_compile_options(openride_osm_import PRIVATE /W4)
else()
    target_compile_options(openride_osm_import PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openride_spatial_benchmark
    src/tools/spatial_benchmark_main.c
)

target_link_libraries(openride_spatial_benchmark
    PRIVATE
        openride_core
)

if(MSVC)
    target_compile_options(openride_spatial_benchmark PRIVATE /W4)
else()
    target_compile_options(openride_spatial_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openride_segment_benchmark
    src/tools/segment_benchmark_main.c
)

target_link_libraries(openride_segment_benchmark
    PRIVATE
        openride_core
)

if(MSVC)
    target_compile_options(openride_segment_benchmark PRIVATE /W4)
else()
    target_compile_options(openride_segment_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openride_loop_benchmark
    src/tools/loop_benchmark_main.c
)

target_link_libraries(openride_loop_benchmark
    PRIVATE
        openride_core
)

if(MSVC)
    target_compile_options(openride_loop_benchmark PRIVATE /W4)
else()
    target_compile_options(openride_loop_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPENRIDE_BUILD_TESTS)
    enable_testing()

    add_executable(test_map_camera
        tests/test_map_camera.c
    )
    target_link_libraries(test_map_camera PRIVATE openride_core)
    add_test(NAME map_camera COMMAND test_map_camera)

    add_executable(test_mbtiles
        tests/test_mbtiles.c
    )
    target_link_libraries(test_mbtiles PRIVATE openride_core)
    add_test(
        NAME mbtiles
        COMMAND test_mbtiles "${CMAKE_CURRENT_SOURCE_DIR}/data/maps/demo.mbtiles"
    )

    add_executable(test_mvt
        tests/test_mvt.c
    )
    target_link_libraries(test_mvt PRIVATE openride_core)
    add_test(NAME mvt COMMAND test_mvt)

    add_executable(test_map_style
        tests/test_map_style.c
    )
    target_link_libraries(test_map_style PRIVATE openride_core)
    add_test(NAME map_style COMMAND test_map_style)

    add_executable(test_map_selection
        tests/test_map_selection.c
    )
    target_link_libraries(test_map_selection PRIVATE openride_core)
    add_test(NAME map_selection COMMAND test_map_selection)

    add_executable(test_routing_graph
        tests/test_routing_graph.c
    )
    target_link_libraries(test_routing_graph PRIVATE openride_core)
    add_test(NAME routing_graph COMMAND test_routing_graph)

    add_executable(test_routing_engine
        tests/test_routing_engine.c
    )
    target_link_libraries(test_routing_engine PRIVATE openride_core)
    add_test(NAME routing_engine COMMAND test_routing_engine)

    add_executable(test_loop_generator
        tests/test_loop_generator.c
    )
    target_link_libraries(test_loop_generator PRIVATE openride_core)
    add_test(NAME loop_generator COMMAND test_loop_generator)

    add_executable(test_navigation_engine
        tests/test_navigation_engine.c
    )
    target_link_libraries(test_navigation_engine PRIVATE openride_core)
    add_test(NAME navigation_engine COMMAND test_navigation_engine)

    add_executable(test_gps_simulator
        tests/test_gps_simulator.c
    )
    target_link_libraries(test_gps_simulator PRIVATE openride_core)
    add_test(NAME gps_simulator COMMAND test_gps_simulator)

    add_executable(test_navigation_instructions
        tests/test_navigation_instructions.c
    )
    target_link_libraries(test_navigation_instructions PRIVATE openride_core)
    add_test(NAME navigation_instructions COMMAND test_navigation_instructions)

    add_executable(test_osm_import
        tests/test_osm_import.c
    )
    target_link_libraries(test_osm_import PRIVATE openride_core)
    add_test(
        NAME osm_import
        COMMAND test_osm_import "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiny.osm.pbf"
    )
endif()
