# Schneller, isolierter macOS-Dev-Build für Better Scenes Dock (Ninja, ohne volles Xcode).
#
# Dieser Build existiert NEBEN dem offiziellen Template-Build (Root-CMakeLists.txt,
# Xcode-Generator) und lässt diesen unberührt. Er dient nur der schnellen lokalen
# Iteration auf Apple Silicon: kompiliert die Quellen aus ../src gegen die
# libobs-Header und linkt mit "-undefined dynamic_lookup" (Symbole werden erst beim
# Laden durch OBS aufgelöst, daher kein kompiliertes libobs nötig).
#
# Voraussetzung: obs-studio-Quellen unter ../.deps/obs-studio-<OBS_VERSION>
# (siehe dev/README.md). Build: ./dev/build.sh

cmake_minimum_required(VERSION 3.28)

# macOS-SDK explizit setzen (CLT-Clang findet sonst die C++-Standardbibliothek
# libc++, z. B. <type_traits>, nicht). Muss vor project() stehen.
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE _macos_sdk
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  set(CMAKE_OSX_SYSROOT "${_macos_sdk}" CACHE PATH "macOS SDK" FORCE)
endif()

project(better-scenes-dock VERSION 0.2.0 LANGUAGES C CXX)

set(OBS_VERSION "32.1.2" CACHE STRING "obs-studio Header-Version (= laufende OBS)")
set(LIBOBS_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/obs-studio-${OBS_VERSION}/libobs"
    CACHE PATH "Pfad zu den libobs-Headern")

if(NOT EXISTS "${LIBOBS_INCLUDE}/obs-module.h")
  message(
    FATAL_ERROR
    "libobs-Header nicht gefunden unter:\n  ${LIBOBS_INCLUDE}\n"
    "Bitte zuerst die obs-studio-Quellen holen – siehe dev/README.md."
  )
endif()

# simde (header-only SIMD-Shim): obs-studio 32 bündelt es nicht mehr im Tarball
# (Submodule/obs-deps), libobs' sse-intrin.h braucht aber <simde/x86/sse2.h>.
set(SIMDE_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/simde-include"
    CACHE PATH "Pfad mit simde/-Headerbaum")
if(NOT EXISTS "${SIMDE_INCLUDE}/simde/x86/sse2.h")
  message(
    FATAL_ERROR
    "simde-Header nicht gefunden unter:\n  ${SIMDE_INCLUDE}/simde\n"
    "Bitte holen – siehe dev/README.md."
  )
endif()

# Qt6 (obs-deps, passend zur OBS-Laufzeit 6.8.3) + obs-frontend-api-Header
# (in obs-studio 32 liegt die Frontend-API unter frontend/api, nicht mehr UI/).
set(QT6_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/obs-deps-qt6-2025-07-11-universal"
    CACHE PATH "Qt6-Prefix (obs-deps)")
set(FRONTEND_API_INCLUDE
    "${CMAKE_CURRENT_SOURCE_DIR}/../.deps/obs-studio-${OBS_VERSION}/frontend/api"
    CACHE PATH "Pfad zum obs-frontend-api-Header")
list(PREPEND CMAKE_PREFIX_PATH "${QT6_PREFIX}")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
message(STATUS "Better Scenes Dock: Qt6 ${Qt6_VERSION} aus ${QT6_PREFIX}")

# CLT-Workaround: auf dieser Maschine liegen die libc++-Header nur im SDK, nicht
# im Toolchain-Pfad (CommandLineTools/usr/include/c++/v1 fehlt) -> für C++ ergänzen.
set(_libcxx "${CMAKE_OSX_SYSROOT}/usr/include/c++/v1")
if(APPLE
   AND EXISTS "${_libcxx}/type_traits"
   AND NOT EXISTS "/Library/Developer/CommandLineTools/usr/include/c++/v1/type_traits")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-isystem>" "$<$<COMPILE_LANGUAGE:CXX>:${_libcxx}>")
  message(STATUS "Better Scenes Dock: libc++-Header aus SDK ergänzt (${_libcxx})")
endif()

# obsconfig.h wird normalerweise vom OBS-Build generiert (obsconfig.h.in) und
# fehlt im Quell-Tarball -> minimale Variante für die Plugin-Kompilierung erzeugen.
file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/obsconfig/obsconfig.h"
  "#pragma once\n"
  "#define OBS_DATA_PATH \"../../data\"\n"
  "#define OBS_PLUGIN_PATH \"../obs-plugins\"\n"
  "#define OBS_PLUGIN_DESTINATION \"obs-plugins\"\n"
  "#define OBS_RELEASE_CANDIDATE 0\n"
  "#define OBS_BETA 0\n"
)

# plugin-support.c aus der Template-Vorlage generieren (@CMAKE_PROJECT_NAME@ etc.)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin-support.c.in"
  "${CMAKE_CURRENT_BINARY_DIR}/plugin-support.c"
  @ONLY
)

add_library(
  ${PROJECT_NAME} MODULE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/plugin-main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/bsd-model.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/bsd-dock.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/plugin-support.c"
)

target_include_directories(
  ${PROJECT_NAME}
  PRIVATE
    "${LIBOBS_INCLUDE}"
    "${FRONTEND_API_INCLUDE}"
    "${SIMDE_INCLUDE}"
    "${CMAKE_CURRENT_BINARY_DIR}/obsconfig"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

# Qt NUR zum Kompilieren einbinden (Header/MOC), NICHT linken: das vermeidet
# transitive Frameworks (u. a. das in neuen macOS-SDKs entfernte AGL). Die
# Qt-Symbole löst OBS zur Laufzeit via dynamic_lookup auf (identische Qt 6.8.3).
set(_qtlib "${QT6_PREFIX}/lib")
target_include_directories(
  ${PROJECT_NAME}
  SYSTEM
  PRIVATE
    "${QT6_PREFIX}/include"
    "${QT6_PREFIX}/mkspecs/macx-clang"
    "${_qtlib}/QtCore.framework/Headers"
    "${_qtlib}/QtGui.framework/Headers"
    "${_qtlib}/QtWidgets.framework/Headers"
)
target_compile_options(${PROJECT_NAME} PRIVATE "-iframework" "${_qtlib}")
target_compile_definitions(${PROJECT_NAME} PRIVATE QT_CORE_LIB QT_GUI_LIB QT_WIDGETS_LIB QT_NO_DEBUG)

# libobs-, frontend-api- UND Qt-Symbole erst beim Laden durch OBS auflösen
target_link_options(${PROJECT_NAME} PRIVATE "-undefined" "dynamic_lookup")

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    PREFIX ""
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.diyobsplugins.better-scenes-dock"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Nach dem Build: data/ (Locale) ins Bundle packen, ad-hoc signieren und ins
# OBS-Plugin-Verzeichnis installieren. (2ME-Learning: ohne data/-Bundling
# meckert OBS mit einer Locale-Warnung.)
set(OBS_PLUGINS_DIR "$ENV{HOME}/Library/Application Support/obs-studio/plugins")
add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../data"
    "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>/Contents/Resources"
  COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${OBS_PLUGINS_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${OBS_PLUGINS_DIR}/${PROJECT_NAME}.plugin"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:${PROJECT_NAME}>"
    "${OBS_PLUGINS_DIR}/${PROJECT_NAME}.plugin"
  COMMENT "data/ bundeln, Ad-hoc-Signatur + Installation nach ${OBS_PLUGINS_DIR}"
  VERBATIM
)
