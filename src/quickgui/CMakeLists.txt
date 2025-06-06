############################################################
# sources
set(QGIS_QUICK_GUI_MOC_HDRS
  qgsquickcoordinatetransformer.h
  qgsquickelevationprofilecanvas.h
  qgsquickmapcanvasmap.h
  qgsquickmapsettings.h
  qgsquickmaptoscreen.h
  qgsquickmaptransform.h
  qgsquickutils.h
)

set(QGIS_QUICK_GUI_SRC
  qgsquickcoordinatetransformer.cpp
  qgsquickelevationprofilecanvas.cpp
  qgsquickmapcanvasmap.cpp
  qgsquickmapsettings.cpp
  qgsquickmaptoscreen.cpp
  qgsquickmaptransform.cpp
  qgsquickutils.cpp
)

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

include_directories(SYSTEM
  ${LIBZIP_INCLUDE_DIRS}
  ${SPATIALINDEX_INCLUDE_DIR}
  ${QCA_INCLUDE_DIR}
  ${QTKEYCHAIN_INCLUDE_DIR}
)

############################################################
# qgis_quick shared library
if(MSVC)
  set_source_files_properties(${QGIS_QUICK_GUI_MOC_SRCS} PROPERTIES COMPILE_FLAGS "/wd4512 /wd4996" )
else()
  set_source_files_properties(${QGIS_QUICK_GUI_MOC_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations" )
endif()

add_library(qgis_quick ${LIBRARY_TYPE}
    ${QGIS_QUICK_GUI_SRC}
    ${QGIS_QUICK_GUI_MOC_HDRS}
    ${QGIS_QUICK_GUI_MOC_SRCS}
    ${QGIS_QUICK_GUI_HDRS})

# require c++17
target_compile_features(qgis_quick PRIVATE cxx_std_17)

target_link_libraries(qgis_quick
    ${QT_VERSION_BASE}::Quick
    ${QT_VERSION_BASE}::Qml
    ${QT_VERSION_BASE}::Xml
    ${QT_VERSION_BASE}::Concurrent
    ${QT_VERSION_BASE}::Positioning
    qgis_core)

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  target_link_libraries(qgis_quick ${QT_VERSION_BASE}::AndroidExtras)
endif()
target_compile_definitions(qgis_quick PRIVATE "-DQT_NO_FOREACH")

GENERATE_EXPORT_HEADER(
  qgis_quick
  BASE_NAME QUICK
  EXPORT_FILE_NAME qgis_quick.h
)

# Installation
if(NOT APPLE OR NOT QGIS_MACAPP_FRAMEWORK)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qgis_quick.h ${QGIS_QUICK_GUI_HDRS} ${QGIS_QUICK_GUI_MOC_HDRS} DESTINATION ${QGIS_INCLUDE_DIR})
else()
  set_target_properties(qgis_quick PROPERTIES
    CLEAN_DIRECT_OUTPUT 1
    FRAMEWORK 1
    FRAMEWORK_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}"
    MACOSX_FRAMEWORK_INFO_PLIST "${CMAKE_SOURCE_DIR}/mac/framework.info.plist.in"
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${COMPLETE_VERSION}
    MACOSX_FRAMEWORK_IDENTIFIER org.qgis.qgis3_quick
    BUILD_WITH_INSTALL_RPATH TRUE
    PUBLIC_HEADER "${QGIS_QUICK_GUI_HDRS};${QGIS_QUICK_GUI_MOC_HDRS}"
    LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}"
  )
  # generated export header does not get copied with PUBLIC_HEADER files
  add_custom_command(TARGET qgis_quick
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy qgis_quick.h
            "${QGIS_OUTPUT_DIRECTORY}/${QGIS_LIB_SUBDIR}/qgis_quick.framework/Headers"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS qgis_quick.h
  )
endif()

install(TARGETS qgis_quick
  RUNTIME DESTINATION ${QGIS_BIN_DIR}
  LIBRARY DESTINATION ${QGIS_LIB_DIR}
  ARCHIVE DESTINATION ${QGIS_LIB_DIR}
  FRAMEWORK DESTINATION ${QGIS_FW_SUBDIR}
  PUBLIC_HEADER DESTINATION ${QGIS_INCLUDE_DIR})

############################################################
# qgis_quick_plugin module (QML) library
add_subdirectory(plugin)

