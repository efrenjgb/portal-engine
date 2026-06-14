# Cross-platform build (Linux / macOS / Windows, GCC / Clang / MSVC / MinGW).
# The hand-rolled Makefile is still the quick macOS/Linux path; this exists for
# Windows + the CI that publishes release binaries. Typical use:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   cmake --install build --prefix dist/portal-engine   # self-contained folder
# Toggle the in-engine editor off (the `make play` equivalent) with
#   -DPORTAL_EDITOR=OFF
cmake_minimum_required(VERSION 3.16)
project(portal_engine LANGUAGES CXX
        DESCRIPTION "BUILD-style sector/portal software renderer")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to an optimized build for single-config generators (Makefiles/Ninja).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PORTAL_EDITOR "Build the in-engine editor + pick buffer (Tab to toggle)" ON)

find_package(SDL2 REQUIRED)
# Packagings disagree on what find_package exposes: modern SDL2 ships config-file
# targets (SDL2::SDL2 / SDL2::SDL2main); older ones only set SDL2_INCLUDE_DIRS /
# SDL2_LIBRARIES. Synthesize the target when it's missing so the rest is uniform.
if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}")
endif()

add_executable(portal_engine
    main.cpp Map.cpp Renderer.cpp Player.cpp Texture.cpp GrpExtract.cpp stb_image_impl.cpp)

if(NOT PORTAL_EDITOR)
    target_compile_definitions(portal_engine PRIVATE EDITOR=0)
endif()

# SDL2main supplies the WinMain shim on Windows; link it first when present.
if(TARGET SDL2::SDL2main)
    target_link_libraries(portal_engine PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(portal_engine PRIVATE SDL2::SDL2)

if(MSVC)
    target_compile_options(portal_engine PRIVATE /W4)
else()
    target_compile_options(portal_engine PRIVATE -Wall -Wextra)
endif()

# Look for the SDL2 runtime library next to the executable, so a release zip that
# bundles it (see the CI workflow) runs without a system-wide SDL2 install.
if(APPLE)
    set_target_properties(portal_engine PROPERTIES INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(portal_engine PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

# Install a runnable layout: binary + the default map + the bundled base textures.
# (textures/duke/ is gitignored Duke art — not redistributed; only *.png at the
# top of textures/ ship.)
install(TARGETS portal_engine RUNTIME DESTINATION .)
install(FILES map.txt README.md LICENSE DESTINATION .)
# Only the top-level base textures (non-recursive glob): textures/duke/ is the
# gitignored, non-redistributable Duke art and must never end up in a release.
file(GLOB PORTAL_BASE_TEXTURES "${CMAKE_CURRENT_SOURCE_DIR}/textures/*.png")
install(FILES ${PORTAL_BASE_TEXTURES} DESTINATION textures)
