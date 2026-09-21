# Third-party dependencies.
#
# glm, SDL, imgui, vk-bootstrap and tinygltf are git submodules; stb is a submodule of
# loose headers. Vulkan itself is not vendored -- it comes from the installed SDK
# and is located by find_package(Vulkan) in the root CMakeLists.

foreach(dep IN ITEMS glm SDL imgui vk-bootstrap VulkanMemoryAllocator tinygltf stb)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dep}/.git)
        message(FATAL_ERROR
            "third-party/${dep} is empty. The dependencies are git submodules; run:\n"
            "    git submodule update --init --recursive")
    endif()
endforeach()

# Third-party code is not ours to lint.
if(MSVC)
    add_compile_options(/W0)
else()
    add_compile_options(-w)
endif()

# --- glm --------------------------------------------------------------------
# Header-only: GLM_BUILD_LIBRARY OFF makes glm::glm an INTERFACE target.
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glm)

# --- SDL --------------------------------------------------------------------
# Pinned to release-3.4.16. Built static so the engine stays one self-contained
# executable like every other dependency here: no SDL3.dll to copy next to the
# exe, so F5 works straight out of the build tree. Flip SDL_SHARED back on (and
# SDL_STATIC off) to let users swap SDL underneath a shipped build -- SDL3::SDL3
# aliases whichever of the two is enabled.
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL    OFF CACHE BOOL "" FORCE)
# Vulkan surface creation is the whole reason SDL is here.
set(SDL_VULKAN       ON  CACHE BOOL "" FORCE)
add_subdirectory(SDL)

# --- imgui ------------------------------------------------------------------
# Pinned to v1.92.9b. Dear ImGui ships no build system by design: you compile the
# core plus exactly the backends you use. Ours are SDL3 (windowing/input) and
# Vulkan (rendering), which is why this target has to come after SDL above.
#
# To switch to the docking/multi-viewport branch, check the submodule out at
# v1.92.9b-docking instead -- the file list below is identical, so nothing here
# needs to change.
set(MANGETSU_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
add_library(imgui STATIC
    ${MANGETSU_IMGUI_DIR}/imgui.cpp
    ${MANGETSU_IMGUI_DIR}/imgui_draw.cpp
    ${MANGETSU_IMGUI_DIR}/imgui_tables.cpp
    ${MANGETSU_IMGUI_DIR}/imgui_widgets.cpp
    # The demo window is the reference for every widget and the fastest way to
    # confirm the backends are wired up; drop it once that stops being useful.
    ${MANGETSU_IMGUI_DIR}/imgui_demo.cpp
    ${MANGETSU_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${MANGETSU_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
    ${MANGETSU_IMGUI_DIR}
    ${MANGETSU_IMGUI_DIR}/backends
)
# imgui_impl_vulkan.cpp calls Vulkan directly and imgui_impl_sdl3.cpp calls SDL;
# both backend headers are included by our own code, so these stay PUBLIC.
target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL3::SDL3)

# --- vk-bootstrap -----------------------------------------------------------
# Picks up Vulkan::Headers from the find_package(Vulkan) already done at the
# root, so it will not reach for the network via FetchContent.
set(VK_BOOTSTRAP_TEST    OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(vk-bootstrap)

# --- VulkanMemoryAllocator --------------------------------------------------
# Header-only upstream: its CMakeLists exposes the headers as an INTERFACE
# target, and we compile the implementation ourselves in one TU, as with stb.
set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(VulkanMemoryAllocator)

add_library(vma STATIC impl/vma_impl.cpp)
target_link_libraries(vma PUBLIC GPUOpen::VulkanMemoryAllocator Vulkan::Vulkan)

# --- stb --------------------------------------------------------------------
# stb ships no build system: its headers are both declaration and definition, so
# the implementations get compiled exactly once, here.
add_library(stb STATIC impl/stb_impl.cpp)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb)

# --- tinygltf ---------------------------------------------------------------
# Pinned to v2.9.7, the last release of the header-only C++ API (upstream HEAD is
# now the unrelated pure-C v3 rewrite).
#
# tinygltf's own CMakeLists is deliberately not used. Its compiled target builds
# tiny_gltf.cc, which defines STB_IMAGE_IMPLEMENTATION against tinygltf's own
# vendored copies of the stb headers -- that would duplicate every stbi_* symbol
# the stb target above already defines, and link two different versions of stb
# into one binary. We compile the implementation ourselves instead.
#
# For the same reason the tinygltf source directory is not placed on the include
# path directly: it holds a vendored stb_image.h (v2.28) that would shadow the
# stb submodule's (v2.30), leaving us with one version's declarations and the
# other's definitions. Copying only the headers tinygltf actually needs into a
# clean directory keeps exactly one stb reachable.
set(MANGETSU_TINYGLTF_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/tinygltf-include)
configure_file(tinygltf/tiny_gltf.h ${MANGETSU_TINYGLTF_INCLUDE}/tiny_gltf.h COPYONLY)
configure_file(tinygltf/json.hpp    ${MANGETSU_TINYGLTF_INCLUDE}/json.hpp    COPYONLY)

add_library(tinygltf STATIC impl/tinygltf_impl.cpp)
target_include_directories(tinygltf PUBLIC ${MANGETSU_TINYGLTF_INCLUDE})
# Only the implementation TU pulls in stb; the declarations in tiny_gltf.h do
# not, so consumers are unaffected and these stay PRIVATE.
target_compile_definitions(tinygltf PRIVATE
    TINYGLTF_NO_INCLUDE_STB_IMAGE
    TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
)
target_link_libraries(tinygltf PUBLIC stb)

# ---------------------------------------------------------------------------
# Solution Explorer grouping
# ---------------------------------------------------------------------------
#
# Every target under third-party/ goes into a "third-party" solution folder, so
# the Solution Explorer shows our engine first and the dependencies tucked away
# beneath it. Targets a dependency defines in its own CMakeLists get a nested
# folder named after that dependency -- SDL alone contributes several.
#
# This walks the directory tree rather than listing target names: a hardcoded
# list silently stops covering a dependency the moment it renames or adds a
# target, and the only symptom is a project quietly reappearing at the solution
# root. Adding a new submodule needs no change here.
function(mangetsu_group_targets dir folder)
    get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        set_target_properties(${target} PROPERTIES FOLDER "${folder}")
    endforeach()

    get_property(subdirs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        get_filename_component(name ${subdir} NAME)
        mangetsu_group_targets(${subdir} "${folder}/${name}")
    endforeach()
endfunction()

# Runs last so that every add_subdirectory() above has already defined its
# targets; BUILDSYSTEM_TARGETS only reports what exists at the time of the call.
mangetsu_group_targets(${CMAKE_CURRENT_SOURCE_DIR} "third-party")
