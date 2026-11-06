# App metadata, install layout and CPack packages (included from the top-level CMakeLists.txt).
# Layout (resDir() in src/main.cpp finds assets/ in each):
#   Linux:   bin/opman, share/opman/assets, share/applications, share/icons
#   Windows: opman.exe and assets/ side by side
#   macOS:   opman.app/Contents/{MacOS/opman, Resources/assets}
# Install with `cmake --install <build> --component opman` (CPack does this already).
set(OPMAN_PKG ${CMAKE_CURRENT_LIST_DIR})

if(WIN32)
  file(TO_CMAKE_PATH "${OPMAN_PKG}/opman.manifest" OPMAN_MANIFEST)
  if(EXISTS ${OPMAN_PKG}/opman.ico)  # ponytail: no .ico yet; the .rc embeds it once it exists
    file(TO_CMAKE_PATH "${OPMAN_PKG}/opman.ico" OPMAN_ICO)
    configure_file(${OPMAN_PKG}/opman.rc.in ${CMAKE_BINARY_DIR}/opman.rc @ONLY)
    target_sources(opman PRIVATE ${CMAKE_BINARY_DIR}/opman.rc)
  endif()
  if(MSVC)
    target_sources(opman PRIVATE ${OPMAN_MANIFEST})
    target_link_options(opman PRIVATE /ENTRY:mainCRTStartup)
  endif()
  # GUI app without a console window, keeping the plain main()
  set_target_properties(opman PROPERTIES WIN32_EXECUTABLE ON)
  set(OPMAN_BIN_DEST .)
  set(OPMAN_RES_DEST .)
elseif(APPLE)
  set_target_properties(opman PROPERTIES
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_INFO_PLIST ${OPMAN_PKG}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME opman
    MACOSX_BUNDLE_GUI_IDENTIFIER org.opman.opman
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "opman contributors")
  set(OPMAN_RES_DEST opman.app/Contents/Resources)
else()
  include(GNUInstallDirs)
  set(OPMAN_BIN_DEST ${CMAKE_INSTALL_BINDIR})
  set(OPMAN_RES_DEST ${CMAKE_INSTALL_DATADIR}/opman)
  install(FILES ${OPMAN_PKG}/opman.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT opman)
  install(FILES ${OPMAN_PKG}/opman-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME opman.xml COMPONENT opman)
  install(FILES ${OPMAN_PKG}/opman.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT opman)
endif()

if(APPLE)  # assets are already in the bundle (POST_BUILD copy)
  install(TARGETS opman BUNDLE DESTINATION . COMPONENT opman)
else()
  install(TARGETS opman RUNTIME DESTINATION ${OPMAN_BIN_DEST} COMPONENT opman)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets DESTINATION ${OPMAN_RES_DEST} COMPONENT opman)
endif()
if(MSVC)  # VC runtime next to opman.exe
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${OPMAN_BIN_DEST})
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT opman)
  include(InstallRequiredSystemLibraries)
endif()
foreach(f LICENSE README.md)
  if(EXISTS ${CMAKE_SOURCE_DIR}/${f})
    install(FILES ${CMAKE_SOURCE_DIR}/${f} DESTINATION ${OPMAN_RES_DEST} COMPONENT opman)
  endif()
endforeach()

# CPack: Windows zip, macOS .dmg, Linux .tar.gz (the Nix flake is the primary Linux package)
set(CPACK_PACKAGE_NAME opman)
set(CPACK_PACKAGE_VENDOR "opman contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "opman - open math notes")
set(CPACK_PACKAGE_INSTALL_DIRECTORY opman)
set(CPACK_PACKAGE_EXECUTABLES opman opman)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};opman;opman;/")
string(TOLOWER "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}" _plat)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  string(REPLACE ";" "-" _arch "${CMAKE_OSX_ARCHITECTURES}")
  set(_plat darwin-${_arch})
endif()
set(CPACK_PACKAGE_FILE_NAME opman-${OPMAN_VERSION_STRING}-${_plat})
if(WIN32)
  set(CPACK_GENERATOR ZIP)
  # cmake --build <build> --config Release --target installer -> <build>/opman-<version>-windows-<arch>-setup.exe, made
  # from the install tree (the zip's contents: opman.exe, assets, LICENSE, MSVC runtime DLLs)
  find_program(OPMAN_MAKENSIS makensis)
  if(OPMAN_MAKENSIS)
    set(_stage ${CMAKE_BINARY_DIR}/installer-stage)
    file(TO_NATIVE_PATH ${_stage} _stage_native)
    add_custom_target(installer
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${_stage}
      COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config $<CONFIG> --component opman --prefix ${_stage}
      COMMAND ${OPMAN_MAKENSIS} -V2 -DVERSION=${OPMAN_VERSION_STRING} -DNUMVERSION=${PROJECT_VERSION} -DSTAGE=${_stage_native}
              -DOUTFILE=${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}-setup.exe ${OPMAN_PKG}/opman.nsi
      VERBATIM)
    add_dependencies(installer opman)
  endif()
elseif(APPLE)
  set(CPACK_GENERATOR DragNDrop)
  set(CPACK_DMG_VOLUME_NAME opman)
  set(CPACK_PRE_BUILD_SCRIPTS ${OPMAN_PKG}/macos-sign.cmake)  # ad-hoc sign the staged .app
else()
  set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
