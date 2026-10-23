cmake_minimum_required(VERSION 3.21)

if(NOT PRISON_SOURCE_ROOT OR NOT PRISON_RESOURCE_ROOT)
    message(FATAL_ERROR "resource source and destination are required")
endif()

# remove assets staged by older builds; downloads live in the user's cache.
set(manifest "${PRISON_RESOURCE_ROOT}/resources.txt")
if(EXISTS "${manifest}")
    file(STRINGS "${manifest}" previous_files)
    foreach(relative IN LISTS previous_files)
        if(relative MATCHES "^(assets|ui|textures|sounds|fonts|data)/" AND NOT relative MATCHES "(^|/)\\.\\.(/|$)")
            file(REMOVE "${PRISON_RESOURCE_ROOT}/${relative}")
        endif()
    endforeach()
endif()

# remove the empty directory left by the old bundle layout.
file(GLOB_RECURSE legacy_files LIST_DIRECTORIES false "${PRISON_RESOURCE_ROOT}/assets/*")
if(NOT legacy_files)
    file(REMOVE_RECURSE "${PRISON_RESOURCE_ROOT}/assets")
endif()
file(MAKE_DIRECTORY "${PRISON_RESOURCE_ROOT}")
foreach(relative resources.txt ASSET_CREDITS.md ui/prisontest_icon.png)
    configure_file("${PRISON_SOURCE_ROOT}/${relative}" "${PRISON_RESOURCE_ROOT}/${relative}" COPYONLY)
endforeach()
