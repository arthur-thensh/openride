cmake_minimum_required(VERSION 3.20)
project(OpenRide VERSION 0.23.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(ANDROID)
    option(OPENRIDE_BUILD_TESTS "Build OpenRide core tests" OFF)
else()
    option(OPENRIDE_BUILD_TESTS "Build OpenRide core tests" ON)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL/CMakeLists.txt")
    message(FATAL_ERROR
        "SDL3 is missing. Run: ./scripts/bootstrap_sdl.sh\n"
        "Expected: vendor/SDL/CMakeLists.txt")
endif()

if(ANDROID)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite/sqlite3.c")
        message(FATAL_ERROR
            "SQLite amalgamation is missing for Android. Run: ./scripts/bootstrap_sqlite.sh\n"
            "Expected: vendor/sqlite/sqlite3.c")
    endif()

    add_library(openride_sqlite STATIC vendor/sqlite/sqlite3.c)
    set_target_properties(openride_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(openride_sqlite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite)
    target_compile_definitions(openride_sqlite PRIVATE
        SQLITE_THREADSAFE=1
        SQLITE_OMIT_LOAD_EXTENSION=1
    )
    add_library(SQLite::SQLite3 ALIAS openride_sqlite)

    find_library(OPENRIDE_ANDROID_ZLIB z REQUIRED)
    add_library(openride_android_zlib INTERFACE)
    target_link_libraries(openride_android_zlib INTERFACE ${OPENRIDE_ANDROID_ZLIB})
    add_library(ZLIB::ZLIB ALIAS openride_android_zlib)
else()
    find_package(SQLite3 REQUIRED)
    find_package(ZLIB REQUIRED)
endif()

if(NOT TARGET SDL3::SDL3)
    add_subdirectory(vendor/SDL EXCLUDE_FROM_ALL)
endif()

add_library(openride_core STATIC
    src/core/map_camera.c
    src/core/map_selection.c
    src/core/routing_graph.c
    src/core/routing_spatial_index.c
    src/core/routing_segment_index.c
    src/core/pathfinder.c
    src/core/routing_engine.c
    src/core/routing_gateway_index.c
    src/core/routing_world.c
    src/core/loop_generator.c
    src/core/navigation_engine.c
    src/core/navigation_instructions.c
    src/core/navigation_session.c
    src/core/location_filter.c
    src/core/gps_simulator.c
    src/core/gpx.c
    src/core/place_search.c
    src/core/app_storage.c
    src/core/platform_paths.c
    src/core/region_manager.c
    src/core/region_network.c
    src/core/region_install.c
    src/core/touch_input.c
    src/core/location_provider.c
    src/core/app_toolbar.c
    src/core/drive_mode.c
    src/core/app_lifecycle.c
    src/osm/osm_import.c
    src/map/mbtiles.c
    src/map/ormap.c
    src/map/ormap_builder.c
    src/map/mvt.c
    src/map/map_style.c
)

target_include_directories(openride_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(openride_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(openride_core
    PUBLIC
        SQLite::SQLite3
        ZLIB::ZLIB
)

if(UNIX AND NOT APPLE)
    target_link_libraries(openride_core PUBLIC m)
endif()

if(MSVC)
    target_compile_options(openride_core PRIVATE /W4)
else()
    target_compile_options(openride_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(OPENRIDE_APP_SOURCES
    src/main.c
    src/map/map_renderer.c
    src/map/vector_map_renderer.c
    src/map/ormap_renderer.c
    src/map/map_world.c
)

if(ANDROID)
    add_library(main SHARED
        ${OPENRIDE_APP_SOURCES}
        src/platform/android_location_provider.c
        src/platform/android_region_download.c
    )
    set(OPENRIDE_APP_TARGET main)
else()
    add_executable(openride ${OPENRIDE_APP_SOURCES})
    set(OPENRIDE_APP_TARGET openride)
endif()

target_include_directories(${OPENRIDE_APP_TARGET}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${OPENRIDE_APP_TARGET}
    PRIVATE
        openride_core
        SDL3::SDL3
        ZLIB::ZLIB
)

if(MSVC)
    target_compile_options(${OPENRIDE_APP_TARGET} PRIVATE /W4)
else()
    target_compile_options(${OPENRIDE_APP_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT ANDROID)
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


add_executable(openride_place_import
    src/tools/place_import_main.c
)
target_link_libraries(openride_place_import PRIVATE openride_core)
if(MSVC)
    target_compile_options(openride_place_import PRIVATE /W4)
else()
    target_compile_options(openride_place_import PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openride_place_search
    src/tools/place_search_main.c
)
target_link_libraries(openride_place_search PRIVATE openride_core)
if(MSVC)
    target_compile_options(openride_place_search PRIVATE /W4)
else()
    target_compile_options(openride_place_search PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(openride_ormap_import
    src/tools/ormap_import_main.c
)
target_link_libraries(openride_ormap_import PRIVATE openride_core)
if(MSVC)
    target_compile_options(openride_ormap_import PRIVATE /W4)
else()
    target_compile_options(openride_ormap_import PRIVATE -Wall -Wextra -Wpedantic)
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

endif() # NOT ANDROID desktop tools

if(OPENRIDE_BUILD_TESTS)
    enable_testing()

    add_executable(test_map_camera
        tests/test_map_camera.c
    )
    target_link_libraries(test_map_camera PRIVATE openride_core)
    add_test(NAME map_camera COMMAND test_map_camera)

    add_executable(test_ormap
        tests/test_ormap.c
    )
    target_link_libraries(test_ormap PRIVATE openride_core)
    add_test(NAME ormap COMMAND test_ormap "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiny.osm.pbf")

    add_executable(test_region_install
        tests/test_region_install.c
    )
    target_link_libraries(test_region_install PRIVATE openride_core)
    add_test(NAME region_install COMMAND test_region_install "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiny.osm.pbf")

    add_executable(test_region_network
        tests/test_region_network.c
    )
    target_link_libraries(test_region_network PRIVATE openride_core)
    add_test(NAME region_network COMMAND test_region_network)

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

    add_executable(test_routing_gateway_index
        tests/test_routing_gateway_index.c
    )
    target_link_libraries(test_routing_gateway_index PRIVATE openride_core)
    add_test(NAME routing_gateway_index COMMAND test_routing_gateway_index)

    add_executable(test_routing_world
        tests/test_routing_world.c
    )
    target_link_libraries(test_routing_world PRIVATE openride_core)
    add_test(NAME routing_world COMMAND test_routing_world)

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

    add_executable(test_navigation_session
        tests/test_navigation_session.c
    )
    target_link_libraries(test_navigation_session PRIVATE openride_core)
    add_test(NAME navigation_session COMMAND test_navigation_session)

    add_executable(test_location_filter
        tests/test_location_filter.c
    )
    target_link_libraries(test_location_filter PRIVATE openride_core)
    add_test(NAME location_filter COMMAND test_location_filter)

    add_executable(test_gpx
        tests/test_gpx.c
    )
    target_link_libraries(test_gpx PRIVATE openride_core)
    add_test(NAME gpx COMMAND test_gpx)

    add_executable(test_place_search
        tests/test_place_search.c
    )
    target_link_libraries(test_place_search PRIVATE openride_core)
    add_test(
        NAME place_search
        COMMAND test_place_search "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiny_places.osm.pbf"
    )

    add_executable(test_app_storage
        tests/test_app_storage.c
    )
    target_link_libraries(test_app_storage PRIVATE openride_core)
    add_test(NAME app_storage COMMAND test_app_storage)

    add_executable(test_platform_paths
        tests/test_platform_paths.c
    )
    target_link_libraries(test_platform_paths PRIVATE openride_core)
    add_test(NAME platform_paths COMMAND test_platform_paths)

    add_executable(test_touch_input
        tests/test_touch_input.c
    )
    target_link_libraries(test_touch_input PRIVATE openride_core)
    add_test(NAME touch_input COMMAND test_touch_input)

    add_executable(test_location_provider
        tests/test_location_provider.c
    )
    target_link_libraries(test_location_provider PRIVATE openride_core)
    add_test(NAME location_provider COMMAND test_location_provider)

    add_executable(test_app_toolbar
        tests/test_app_toolbar.c
    )
    target_link_libraries(test_app_toolbar PRIVATE openride_core)
    add_test(NAME app_toolbar COMMAND test_app_toolbar)

    add_executable(test_drive_mode
        tests/test_drive_mode.c
    )
    target_link_libraries(test_drive_mode PRIVATE openride_core)
    add_test(NAME drive_mode COMMAND test_drive_mode)

    add_executable(test_app_lifecycle
        tests/test_app_lifecycle.c
    )
    target_link_libraries(test_app_lifecycle PRIVATE openride_core)
    add_test(NAME app_lifecycle COMMAND test_app_lifecycle)

    add_executable(test_osm_import
        tests/test_osm_import.c
    )
    target_link_libraries(test_osm_import PRIVATE openride_core)
    add_test(
        NAME osm_import
        COMMAND test_osm_import "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/tiny.osm.pbf"
    )
endif()
