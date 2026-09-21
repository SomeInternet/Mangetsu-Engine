cmake_minimum_required(VERSION 3.22)

project(Mangetsu-Engine
    VERSION 0.1.0
    DESCRIPTION "Mangetsu Engine"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep binaries out of the per-config maze so shaders/models resolve the same
# way under both the Visual Studio and Ninja generators.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_BINARY_DIR}/bin)
endforeach()

# Multi-config generators (Visual Studio) ignore CMAKE_BUILD_TYPE; single-config
# ones need a sane default.
get_property(MANGETSU_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT MANGETSU_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # SDL is built against the DLL CRT by default; leave it alone and just make
    # the compiler behave.
    add_compile_options(/MP /utf-8 /permissive-)
endif()

# ---------------------------------------------------------------------------
# Visual Studio solution layout
# ---------------------------------------------------------------------------
#
# Everything the Solution Explorer shows is generated, so none of it can be
# arranged by hand: re-running CMake overwrites the .sln and .vcxproj files and
# any manual grouping goes with them. The layout therefore lives here, and every
# configure reproduces it exactly.
#
# USE_FOLDERS is what makes solution folders exist at all. Without it CMake
# silently ignores every FOLDER target property and drops all nine projects flat
# at the solution root. It defaults ON only from cmake_minimum_required(3.26)
# (policy CMP0143); we ask for 3.22, so it has to be set explicitly.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ALL_BUILD and ZERO_CHECK are CMake's own bookkeeping targets, not ours.
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMake")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# Vulkan comes from the installed SDK (FindVulkan reads $ENV{VULKAN_SDK}).
find_package(Vulkan REQUIRED)

# glm, SDL, imgui, vk-bootstrap, tinygltf and stb live under third-party/.
add_subdirectory(third-party)

# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------

# CONFIGURE_DEPENDS makes CMake re-glob when the source list changes, so new
# files added in Visual Studio get picked up without re-running CMake by hand.
file(GLOB_RECURSE MANGETSU_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
)

if(NOT MANGETSU_SOURCES)
    message(FATAL_ERROR "No sources found in ${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

add_executable(Mangetsu-Engine ${MANGETSU_SOURCES})

target_include_directories(Mangetsu-Engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(Mangetsu-Engine
    PRIVATE
        Vulkan::Vulkan
        SDL3::SDL3
        imgui
        glm::glm
        vk-bootstrap::vk-bootstrap
        vma
        tinygltf
        stb
)

target_compile_definitions(Mangetsu-Engine
    PRIVATE
        GLM_FORCE_DEPTH_ZERO_TO_ONE      # Vulkan clip space, not OpenGL's
        GLM_FORCE_RADIANS
        GLM_ENABLE_EXPERIMENTAL
        MANGETSU_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
        $<$<CONFIG:Debug>:MANGETSU_DEBUG>
)

if(MSVC)
    target_compile_options(Mangetsu-Engine PRIVATE /W4)
else()
    target_compile_options(Mangetsu-Engine PRIVATE -Wall -Wextra)
endif()

# Mirror the on-disk layout in the Visual Studio solution explorer.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${MANGETSU_SOURCES})

# Make F5 work out of the box: Mangetsu-Engine is the startup project and it
# runs from the repo root, so "shaders/..." and "models/..." resolve.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Mangetsu-Engine)
set_target_properties(Mangetsu-Engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
