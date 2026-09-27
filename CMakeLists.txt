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

# ---------------------------------------------------------------------------
# Shaders
# ---------------------------------------------------------------------------
#
# Every shaders/*.slang is compiled by slangc into one SPIR-V module holding all
# of its entry points; -fvk-use-entrypoint-name keeps their source names (e.g.
# rayGeneration, closestHit) instead of renaming each one to "main", which would
# collide as soon as a file has more than one. The output goes to
# build/shaders/<name>.spv, which the engine sees as MANGETSU_SHADER_DIR.
#
# -fvk-use-scalar-layout makes buffers and pointers use scalar block layout
# (VK_EXT_scalar_block_layout, core in Vulkan 1.2), so the host-side structs
# must be laid out the same way -- no std140/std430 padding.

# slangc ships in the Vulkan SDK's Bin directory.
find_program(MANGETSU_SLANGC slangc
    HINTS $ENV{VULKAN_SDK}/Bin ${Vulkan_INCLUDE_DIR}/../Bin
    REQUIRED
)

set(MANGETSU_SLANG_FLAGS
    -target spirv
    -fvk-use-scalar-layout
    -fvk-use-entrypoint-name
    -capability spvShaderInvocationReorderEXT #Allows for SER
    -g    # Debug info so RenderDoc / Nsight can show Slang source.
)

file(GLOB_RECURSE MANGETSU_SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.slang
)

set(MANGETSU_SHADER_DIR ${CMAKE_BINARY_DIR}/shaders)
set(MANGETSU_SHADER_OUTPUTS "")

foreach(shader IN LISTS MANGETSU_SHADER_SOURCES)
    # Files without a [shader(...)] entry point are modules meant to be
    # imported; slangc refuses to emit SPIR-V for them on their own. They still
    # show up in the Solution Explorer, and their importers' depfiles track them.
    # (Giving a module its first entry point needs a CMake re-configure.)
    file(STRINGS ${shader} entry_points REGEX "\\[shader\\(")
    if(NOT entry_points)
        continue()
    endif()

    get_filename_component(name ${shader} NAME_WE)
    set(spv ${MANGETSU_SHADER_DIR}/${name}.spv)

    # MAIN_DEPENDENCY hangs the rule on the .slang file itself in Visual Studio,
    # so a shader can be recompiled on its own from the Solution Explorer. The
    # depfile tracks imported modules, so editing one rebuilds its importers.
    add_custom_command(
        OUTPUT ${spv}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MANGETSU_SHADER_DIR}
        COMMAND ${MANGETSU_SLANGC} ${shader} ${MANGETSU_SLANG_FLAGS}
                -o ${spv} -depfile ${spv}.d
        MAIN_DEPENDENCY ${shader}
        DEPFILE ${spv}.d
        COMMENT "Compiling shader ${name}.slang"
        VERBATIM
    )

    list(APPEND MANGETSU_SHADER_OUTPUTS ${spv})
endforeach()

# The .spv outputs have to be target sources too: that is what makes Ninja run
# the rules at all (Visual Studio would manage with MAIN_DEPENDENCY alone).
add_executable(Mangetsu-Engine
    ${MANGETSU_SOURCES}
    ${MANGETSU_SHADER_SOURCES}
    ${MANGETSU_SHADER_OUTPUTS}
)

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
        MANGETSU_SHADER_DIR="${MANGETSU_SHADER_DIR}"
        $<$<CONFIG:Debug>:MANGETSU_DEBUG>
)

if(MSVC)
    target_compile_options(Mangetsu-Engine PRIVATE /W4)
else()
    target_compile_options(Mangetsu-Engine PRIVATE -Wall -Wextra)
endif()

# Mirror the on-disk layout in the Visual Studio solution explorer.
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "src" FILES ${MANGETSU_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/shaders PREFIX "shaders" FILES ${MANGETSU_SHADER_SOURCES})
source_group(TREE ${MANGETSU_SHADER_DIR} PREFIX "shaders/spirv" FILES ${MANGETSU_SHADER_OUTPUTS})

# Make F5 work out of the box: Mangetsu-Engine is the startup project and it
# runs from the repo root, so "shaders/..." and "models/..." resolve.
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Mangetsu-Engine)
set_target_properties(Mangetsu-Engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
