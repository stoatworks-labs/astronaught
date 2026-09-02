cmake_minimum_required(VERSION 3.15)

project(Astronaught
    VERSION 0.1.1
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Universal by default so one bundle loads in both the Apple Silicon and the
# Intel build of Resolume. Override with -DCMAKE_OSX_ARCHITECTURES=arm64 for a
# faster dev build.
#
# This must sit immediately after project() and before any target exists. CMake
# latches the architecture list when the first target is created; setting it
# later is silently ignored and you get an arm64-only binary that the build log
# calls a success. Verify with `lipo`, never with the log.
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple deprecated the whole OpenGL API in 10.14 and FFGL's plugin interface is
# still OpenGL, so every SDK call warns. There is no alternative API to move to
# here, so silencing it is not ignoring a problem.
if(APPLE)
    add_compile_definitions(GL_SILENCE_DEPRECATION)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# windows.h defines min and max as macros, which turns every std::min into
# `std::(...)` and fails with an error naming neither min nor windows.h. Set
# globally rather than per-file: the next file to include windows.h hits it too,
# and the error gives no hint what happened.
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(ASTRONAUGHT_BUILD_TOOLS "Build the offline render and measurement harness" ON)

# ---------------------------------------------------------------------------
# The Resolume FFGL SDK, as a git submodule pinned to b1afaf9 -- the rev the
# rest of the fleet pins. See AGENTS.md for what is wrong with it.
# ---------------------------------------------------------------------------
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl/CMakeLists.txt")
    message(FATAL_ERROR
        "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

set(FFGL_BUILD_EXAMPLE_PLUGINS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/ffgl EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Everything the plugin and the harness share, as an OBJECT library.
#
# OBJECT and not STATIC on purpose. `CFFGLPluginInfo` registers itself from a
# file-scope constructor and nothing ever references that object by name, so in
# an archive the linker is entitled to drop the whole translation unit -- and the
# result is a bundle that loads, exports plugMain, and reports that it contains
# no plugins. tools/verify.sh checks both failure directions.
# ---------------------------------------------------------------------------
add_library(astronaught_core OBJECT
    source/Diag.h
    source/Diag.cpp
    source/Heads.h
    source/Heads.cpp
    source/Transport.h
    source/Transport.cpp
    source/Tape.h
    source/Tape.cpp
    source/Spring.h
    source/Spring.cpp
    source/Controls.h
    source/Controls.cpp
    source/PassBuffer.h
    source/PassBuffer.cpp
    source/Store.h
    source/Store.cpp
    source/Presets.h
    source/Shaders.h
    source/shaders/Vertex.cpp
    source/shaders/Tape.cpp
    source/shaders/Medium.cpp
    source/shaders/Passes.cpp
    source/Astronaught.h
    source/Astronaught.cpp
)

target_include_directories(astronaught_core PUBLIC source)
target_link_libraries(astronaught_core PUBLIC ffgl::sdk)
target_compile_definitions(astronaught_core PUBLIC ASTRONAUGHT_VERSION="${PROJECT_VERSION}")
set_target_properties(astronaught_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# On Windows/Linux the SDK headers we include pull in <GL/glew.h>. The SDK links
# GLEW privately, so consumers need it too -- for the include path, and for the
# GLEW_STATIC define the static vcpkg triplet propagates. macOS uses the system
# OpenGL framework and needs none of this.
#
# GLEW arrives through vcpkg.json, which nothing in this file mentions. Deleting
# that manifest breaks the Windows job at CONFIGURE time while every macOS build
# stays green.
if(NOT APPLE)
    find_package(GLEW REQUIRED)
    target_link_libraries(astronaught_core PUBLIC GLEW::GLEW)
endif()

# ---------------------------------------------------------------------------
# The plugin. One FFGL effect; PluginEntry.cpp is listed directly rather than in
# the core library so the harness does not carry the registration.
# ---------------------------------------------------------------------------
add_library(AstronaughtPlugin MODULE source/PluginEntry.cpp)
target_link_libraries(AstronaughtPlugin PRIVATE astronaught_core)

if(APPLE)
    set_target_properties(AstronaughtPlugin PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "bundle"
        OUTPUT_NAME "Astronaught"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Astronaught"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.stoatworks.ffgl.astronaught"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    # FFGL hosts resolve plugMain by symbol name, so keep default visibility.
    target_link_libraries(AstronaughtPlugin PRIVATE
        "-framework OpenGL"
        "-framework Carbon"
        "-framework AppKit"
    )
else()
    set_target_properties(AstronaughtPlugin PROPERTIES OUTPUT_NAME "Astronaught")
endif()

# ---------------------------------------------------------------------------
# Offline harness. Where the plugin is actually verified rather than merely
# previewed: it drives the real plugin class through the real FFGL sequence in a
# headless core-profile context, including SetTime and a synthetic spectrum. See
# tools/astest.
# ---------------------------------------------------------------------------
if(ASTRONAUGHT_BUILD_TOOLS AND APPLE)
    add_executable(astest tools/astest/main.cpp)
    # zlib ships with the OS, which is why the PNG writer is fifty lines here
    # rather than a vendored dependency.
    target_link_libraries(astest PRIVATE astronaught_core z
        "-framework OpenGL"
        "-framework AppKit"
    )
endif()

# ---------------------------------------------------------------------------
# `cmake --install` drops the bundle straight into the user's Resolume plugin
# folder. Override with -DCMAKE_INSTALL_PREFIX=...
# ---------------------------------------------------------------------------
if(APPLE AND NOT CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    # honour an explicit prefix
elseif(APPLE)
    set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/Documents/Resolume Arena/Extra Effects"
        CACHE PATH "" FORCE)
endif()

install(TARGETS AstronaughtPlugin
    LIBRARY DESTINATION "."
    BUNDLE  DESTINATION "."
)

# ---------------------------------------------------------------------------
# There is no OpenFX target, and unlike the rest of the fleet that is not
# "not yet" -- it is a structural problem with this plugin in particular.
#
# An OFX host asks for a render of an arbitrary frame in an arbitrary order. A
# tape loop cannot answer that: what is on the tape at frame 900 depends on what
# was written at frames 1 through 899, including everything Intensity fed back
# round. Afterglow gets an OFX build out of a similar-looking effect because its
# queue is a pure function of time -- slot k is simply the source at t - k*hold
# -- and that trick stops working the moment there is feedback.
#
# Escapement deferred an OFX build for the same reason and the reasoning is
# written out there. Anything here that is NOT the loop -- Heads.cpp,
# Controls.cpp, Transport.cpp, Spring.cpp -- already has no GL and no host in
# it, so if a route is ever found it will not be blocked by this side.
# ---------------------------------------------------------------------------
