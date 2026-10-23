set(PRISON_DATA_DESTINATION "share/prisoncpp")
set_target_properties(prisoncpp PROPERTIES OUTPUT_NAME prisontest)
if(APPLE)
    set_target_properties(prisoncpp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME prisontest
        MACOSX_BUNDLE_GUI_IDENTIFIER dev.prisontest.client
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(PRISON_DATA_DESTINATION "prisontest.app/Contents/Resources")
    install(TARGETS prisoncpp BUNDLE DESTINATION .)
    pkg_check_modules(SDL2_COMPAT QUIET sdl2-compat)
    if(SDL2_COMPAT_FOUND)
        # sdl2-compat loads sdl3 with dlopen, so bundle dependency scanning misses it.
        pkg_check_modules(SDL3 REQUIRED sdl3)
        find_file(PRISON_SDL3_LIBRARY NAMES libSDL3.dylib
            HINTS ${SDL3_LIBRARY_DIRS} REQUIRED)
        file(REAL_PATH "${PRISON_SDL3_LIBRARY}" PRISON_SDL3_RUNTIME)
        install(FILES "${PRISON_SDL3_RUNTIME}"
            DESTINATION "prisontest.app/Contents/Frameworks" RENAME libSDL3.dylib)
        install(FILES packaging/licenses/SDL2-compat-LICENSE.txt packaging/licenses/SDL3-LICENSE.txt
            DESTINATION "${PRISON_DATA_DESTINATION}/licenses")
    endif()
elseif(WIN32)
    install(TARGETS prisoncpp RUNTIME_DEPENDENCY_SET prison_runtime RUNTIME DESTINATION bin)
    install(RUNTIME_DEPENDENCY_SET prison_runtime
        DIRECTORIES "${SDL2_RUNTIME_PREFIX}/bin"
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*"
        RUNTIME DESTINATION bin)
    install(PROGRAMS packaging/prisontest.cmd DESTINATION .)
else()
    # jsoncpp's soname differs between supported debian and ubuntu releases.
    set_target_properties(prisoncpp PROPERTIES INSTALL_RPATH "$ORIGIN/../lib/prisoncpp")
    install(TARGETS prisoncpp RUNTIME_DEPENDENCY_SET prison_runtime RUNTIME DESTINATION bin)
    install(RUNTIME_DEPENDENCY_SET prison_runtime
        PRE_INCLUDE_REGEXES "^libjsoncpp[.]so"
        PRE_EXCLUDE_REGEXES ".*"
        LIBRARY DESTINATION lib/prisoncpp)
    install(FILES packaging/prisontest.desktop DESTINATION share/applications)
    install(FILES ui/prisontest_icon.png
        DESTINATION share/icons/hicolor/16x16/apps RENAME prisontest.png)
endif()

set(PRISON_RESOURCE_ROOT "$<TARGET_FILE_DIR:prisoncpp>")
if(APPLE)
    set(PRISON_RESOURCE_ROOT "$<TARGET_BUNDLE_CONTENT_DIR:prisoncpp>/Resources")
endif()
add_custom_target(prison_resources
    COMMAND "${CMAKE_COMMAND}"
        "-DPRISON_SOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DPRISON_RESOURCE_ROOT=${PRISON_RESOURCE_ROOT}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyResources.cmake"
    COMMENT "copying client resource list and icon"
    VERBATIM)
add_dependencies(prisoncpp prison_resources)
if(TARGET ui_test)
    add_dependencies(ui_test prison_resources)
    target_compile_definitions(ui_test PRIVATE PRISON_RESOURCE_DIR="${PRISON_TEST_ASSETS}")
endif()

install(FILES README.md LICENSE DESTINATION "${PRISON_DATA_DESTINATION}")
install(FILES ui/prisontest_icon.png DESTINATION "${PRISON_DATA_DESTINATION}/ui")
install(FILES fonts/LICENSE-DejaVu.txt DESTINATION "${PRISON_DATA_DESTINATION}/fonts")
install(FILES sounds/README.md DESTINATION "${PRISON_DATA_DESTINATION}/sounds")
install(FILES sounds/prison/README.md DESTINATION "${PRISON_DATA_DESTINATION}/sounds/prison")
install(FILES data/README.md DESTINATION "${PRISON_DATA_DESTINATION}/data")
install(FILES packaging/licenses/jsoncpp-LICENSE DESTINATION "${PRISON_DATA_DESTINATION}/licenses")
install(FILES "${PRISON_RESOURCE_ROOT}/ASSET_CREDITS.md" "${PRISON_RESOURCE_ROOT}/resources.txt"
    DESTINATION "${PRISON_DATA_DESTINATION}")
install(DIRECTORY docs/ DESTINATION "${PRISON_DATA_DESTINATION}/docs")

if(WIN32 OR APPLE)
    find_file(PRISON_CA_BUNDLE NAMES ca-bundle.crt cacert.pem
        HINTS "${SDL2_RUNTIME_PREFIX}/etc/ssl/certs"
            /opt/homebrew/opt/ca-certificates/share/ca-certificates
            /usr/local/opt/ca-certificates/share/ca-certificates
        REQUIRED)
    install(FILES "${PRISON_CA_BUNDLE}" DESTINATION "${PRISON_DATA_DESTINATION}/certs" RENAME ca-bundle.crt)
endif()

if(APPLE)
    configure_file(cmake/FixMacBundle.cmake.in FixMacBundle.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/FixMacBundle.cmake")
endif()

set(CPACK_PACKAGE_NAME prisontest)
set(CPACK_PACKAGE_VENDOR prisontest)
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "native singleplayer prison mining game")
set(CPACK_PACKAGE_FILE_NAME "prisontest-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32 OR APPLE)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
else()
    set(CPACK_GENERATOR "TGZ;DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "prisontest contributors")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
endif()
include(CPack)
