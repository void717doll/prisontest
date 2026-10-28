cmake_minimum_required(VERSION 3.21)
if(NOT DEFINED PRISON_VERSION)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PRISON_VERSION)
    string(STRIP "${PRISON_VERSION}" PRISON_VERSION)
endif()
project(prisoncpp VERSION "${PRISON_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(PRISON_SANITIZE "enable address and undefined behavior sanitizers" OFF)
option(PRISON_CLIENT "build graphical SDL/OpenGL client" ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
add_library(prison_core
    src/common.cpp src/game.cpp src/items.cpp src/inventory.cpp src/filter.cpp
    src/commands.cpp src/development.cpp src/tutorial.cpp
    src/save.cpp src/world.cpp src/world_scenery.cpp src/world_collision.cpp
    src/bombs.cpp src/companion.cpp src/rewards.cpp src/lore.cpp src/progression.cpp src/shaft.cpp
    src/endgame.cpp src/equipment_bonuses.cpp src/guardians.cpp)
target_include_directories(prison_core PUBLIC include)
target_link_libraries(prison_core PUBLIC PkgConfig::JSONCPP Threads::Threads)
target_compile_options(prison_core PRIVATE -Wall -Wextra -Wpedantic)
if(PRISON_SANITIZE)
    target_compile_options(prison_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(prison_core PUBLIC -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
    set(PRISON_TEST_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "asset directory for checks")
    foreach(name game world items save integration rewards lore)
        add_executable(${name}_test tests/${name}_test.cpp)
        target_link_libraries(${name}_test PRIVATE prison_core)
        target_compile_definitions(${name}_test PRIVATE PRISON_SOURCE_DIR="${PRISON_TEST_ASSETS}")
        add_test(NAME ${name} COMMAND ${name}_test "${PRISON_TEST_ASSETS}")
    endforeach()
endif()

if(PRISON_CLIENT)
    find_package(SDL2 REQUIRED CONFIG)
    pkg_get_variable(SDL2_RUNTIME_PREFIX sdl2 prefix)
    find_package(OpenGL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(Freetype REQUIRED)
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl>=7.62)
    set(PRISON_UI_SOURCES
        src/ui.cpp src/ui_menus.cpp src/ui_development.cpp src/ui_hud.cpp src/ui_input.cpp src/ui_tutorial.cpp)
    if(BUILD_TESTING)
        add_executable(help_test tests/help_test.cpp src/help.cpp)
        target_link_libraries(help_test PRIVATE prison_core PkgConfig::CURL)
        add_test(NAME help COMMAND help_test "${PRISON_TEST_ASSETS}")
        add_executable(ui_test tests/ui_test.cpp ${PRISON_UI_SOURCES} src/render.cpp src/settings.cpp)
        target_link_libraries(ui_test PRIVATE prison_core SDL2::SDL2 OpenGL::GL PNG::PNG Freetype::Freetype)
        target_compile_definitions(ui_test PRIVATE SDL_MAIN_HANDLED PRISON_VERSION="${PROJECT_VERSION}")
        target_compile_options(ui_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE PRISON_HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT PRISON_HASH)
        set(PRISON_HASH "unknown")
    endif()
    add_executable(prisoncpp src/main.cpp src/render.cpp ${PRISON_UI_SOURCES} src/help.cpp
        src/settings.cpp src/sound.cpp)
    target_link_libraries(prisoncpp PRIVATE prison_core SDL2::SDL2 OpenGL::GL
        PNG::PNG Freetype::Freetype PkgConfig::CURL)
    target_compile_definitions(prisoncpp PRIVATE SDL_MAIN_HANDLED
        PRISON_BUILD_HASH="${PRISON_HASH}" PRISON_VERSION="${PROJECT_VERSION}")
    target_compile_options(prisoncpp PRIVATE -Wall -Wextra -Wpedantic)
    include(cmake/Packaging.cmake)
endif()
