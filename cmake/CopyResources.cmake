cmake_minimum_required(VERSION 3.21)

if(NOT PRISON_SOURCE_ROOT OR NOT PRISON_RESOURCE_ROOT)
    message(FATAL_ERROR "resource source and destination are required")
endif()

file(STRINGS "${PRISON_SOURCE_ROOT}/resources.txt" resource_files)
file(MAKE_DIRECTORY "${PRISON_RESOURCE_ROOT}")
foreach(relative resources.txt ASSET_CREDITS.md ${resource_files})
    configure_file("${PRISON_SOURCE_ROOT}/${relative}" "${PRISON_RESOURCE_ROOT}/${relative}" COPYONLY)
endforeach()
