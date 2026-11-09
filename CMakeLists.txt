cmake_minimum_required(VERSION 3.24)

project(SIDERA
    VERSION 0.8.0
    DESCRIPTION "SIDERA Engine - High-Performance Vulkan Game Engine"
    LANGUAGES CXX C
)

# ------------------------------------------------------------------------------
# C++20 Standard Configuration
# ------------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Dependencies (Vulkan, GLFW, GLM)
# ------------------------------------------------------------------------------
include(FetchContent)

# 1. Vulkan SDK (or Khronos Vulkan-Headers + Vulkan-Loader fallback)
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(STATUS "[SIDERA] System Vulkan SDK not found via find_package(Vulkan). Fetching official Khronos Vulkan-Headers and Vulkan-Loader...")

    FetchContent_Declare(
        Vulkan-Headers
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
        GIT_TAG        v1.3.296
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Vulkan-Headers)

    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LOADER_ENABLE_LINUX_SORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        Vulkan-Loader
        GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader.git
        GIT_TAG        v1.3.296
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Vulkan-Loader)

    if(NOT TARGET Vulkan::Vulkan)
        add_library(Vulkan::Vulkan INTERFACE IMPORTED)
        target_link_libraries(Vulkan::Vulkan INTERFACE vulkan Vulkan::Headers)
    endif()
else()
    message(STATUS "[SIDERA] Found system Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
endif()

# 2. GLFW
find_package(glfw3 3.3 CONFIG QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "[SIDERA] Fetching GLFW 3.4 via FetchContent...")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
else()
    message(STATUS "[SIDERA] Found system GLFW3.")
endif()

# 3. GLM
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    message(STATUS "[SIDERA] Fetching GLM 1.0.1 via FetchContent...")
    set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)
else()
    message(STATUS "[SIDERA] Found system GLM.")
endif()

# ------------------------------------------------------------------------------
# Shader Compilation Pipeline (GLSL -> SPIR-V)
# ------------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin")
find_program(GLSLANG_VALIDATOR_EXECUTABLE NAMES glslangValidator glslang HINTS "$ENV{VULKAN_SDK}/Bin")

if(NOT GLSLC_EXECUTABLE AND NOT GLSLANG_VALIDATOR_EXECUTABLE)
    if(WIN32)
        message(STATUS "[SIDERA] Fetching official Khronos glslang (Windows x64) for SPIR-V compilation...")
        FetchContent_Declare(
            glslang_prebuilt
            URL https://github.com/KhronosGroup/glslang/releases/download/16.6.0/glslang-16.6.0-windows-x86_64-release.zip
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(glslang_prebuilt)
        set(GLSLANG_VALIDATOR_EXECUTABLE "${glslang_prebuilt_SOURCE_DIR}/bin/glslang.exe")
    else()
        message(FATAL_ERROR "[SIDERA] Neither glslc nor glslangValidator found. Please install the Vulkan SDK.")
    endif()
endif()

set(SIDERA_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag
)

set(SIDERA_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(SIDERA_SPV_FILES "")

foreach(SHADER_SRC ${SIDERA_SHADER_SOURCES})
    get_filename_component(SHADER_FILENAME ${SHADER_SRC} NAME)
    set(SPV_OUT "${SIDERA_SHADER_OUTPUT_DIR}/${SHADER_FILENAME}.spv")

    if(GLSLC_EXECUTABLE)
        add_custom_command(
            OUTPUT "${SPV_OUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SIDERA_SHADER_OUTPUT_DIR}"
            COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O "${SHADER_SRC}" -o "${SPV_OUT}"
            DEPENDS "${SHADER_SRC}"
            COMMENT "Compiling GLSL shader ${SHADER_FILENAME} -> ${SHADER_FILENAME}.spv (glslc)"
            VERBATIM
        )
    else()
        add_custom_command(
            OUTPUT "${SPV_OUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${SIDERA_SHADER_OUTPUT_DIR}"
            COMMAND "${GLSLANG_VALIDATOR_EXECUTABLE}" --target-env vulkan1.3 -V "${SHADER_SRC}" -o "${SPV_OUT}"
            DEPENDS "${SHADER_SRC}"
            COMMENT "Compiling GLSL shader ${SHADER_FILENAME} -> ${SHADER_FILENAME}.spv (glslangValidator)"
            VERBATIM
        )
    endif()

    list(APPEND SIDERA_SPV_FILES "${SPV_OUT}")
endforeach()

add_custom_target(sidera_shaders DEPENDS ${SIDERA_SPV_FILES})

# ------------------------------------------------------------------------------
# SIDERA Executable Target
# ------------------------------------------------------------------------------
set(SIDERA_SOURCES
    src/main.cpp
    src/core/Log.h
    src/core/Log.cpp
    src/core/Window.h
    src/core/Window.cpp
    src/core/Application.h
    src/core/Application.cpp
    src/renderer/Vertex.h
    src/renderer/Mesh.h
    src/renderer/Mesh.cpp
    src/renderer/Camera.h
    src/renderer/Camera.cpp
    src/renderer/Renderer.h
    src/renderer/Renderer.cpp
    src/renderer/vulkan/VulkanCheck.h
    src/renderer/vulkan/VulkanInstance.h
    src/renderer/vulkan/VulkanInstance.cpp
    src/renderer/vulkan/VulkanDevice.h
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanContext.h
    src/renderer/vulkan/VulkanContext.cpp
    src/renderer/vulkan/VulkanSwapchain.h
    src/renderer/vulkan/VulkanSwapchain.cpp
    src/renderer/vulkan/VulkanDepthBuffer.h
    src/renderer/vulkan/VulkanDepthBuffer.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanFrameSync.h
    src/renderer/vulkan/VulkanFrameSync.cpp
    src/renderer/vulkan/VulkanShader.h
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/GraphicsPipeline.h
    src/renderer/vulkan/GraphicsPipeline.cpp
)

add_executable(sidera ${SIDERA_SOURCES})
add_dependencies(sidera sidera_shaders)

# Copy compiled SPIR-V shaders alongside the executable for portable runtime loading
add_custom_command(TARGET sidera POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sidera>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SIDERA_SPV_FILES} "$<TARGET_FILE_DIR:sidera>/shaders/"
    COMMENT "Copying compiled SPIR-V shaders to $<TARGET_FILE_DIR:sidera>/shaders"
    VERBATIM
)

target_include_directories(sidera
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(sidera
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        SIDERA_SHADER_BINARY_DIR="${SIDERA_SHADER_OUTPUT_DIR}"
        $<$<NOT:$<CONFIG:Release>>:SIDERA_ENABLE_VALIDATION=1>
)

if(MSVC)
    target_compile_options(sidera PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(sidera PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(sidera PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(sidera
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)
