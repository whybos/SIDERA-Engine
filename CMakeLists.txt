cmake_minimum_required(VERSION 3.24)

project(SIDERA
    VERSION 0.26.0
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

# 4. stb (pinned to v2.30 for stb_image HDR loading)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)


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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_depth.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle_indirect.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow_depth_indirect.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gen_draw_commands.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_generate.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/hiz_downsample.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/equirect_to_cubemap.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/irradiance_convolution.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/prefilter_cubemap.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/brdf_lut.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_shadow.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/veg_cull_compact.comp
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
    src/core/FrameStats.h
    src/core/FrameStats.cpp
    src/core/Application.h
    src/core/Application.cpp
    src/renderer/Vertex.h
    src/renderer/BoundingVolume.h
    src/renderer/Frustum.h
    src/renderer/Mesh.h
    src/renderer/Mesh.cpp
    src/renderer/ProceduralMeshes.h
    src/renderer/ProceduralMeshes.cpp
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
    src/renderer/vulkan/VulkanHiZBuffer.h
    src/renderer/vulkan/VulkanHiZBuffer.cpp
    src/renderer/vulkan/VulkanShadowMap.h
    src/renderer/vulkan/VulkanShadowMap.cpp
    src/renderer/Material.h
    src/renderer/InstanceData.h
    src/renderer/InstanceBatch.h
    src/renderer/InstanceBatch.cpp
    src/renderer/GpuDrivenScene.h
    src/renderer/GpuDrivenScene.cpp
    src/renderer/vulkan/SamplerManager.h
    src/renderer/vulkan/SamplerManager.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.h
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/TextureManager.h
    src/renderer/TextureManager.cpp
    src/renderer/Viewport.h
    src/renderer/ViewportManager.h
    src/renderer/ViewportManager.cpp
    src/renderer/vulkan/VulkanCubemap.h
    src/renderer/vulkan/VulkanCubemap.cpp
    src/renderer/IBLBuilder.h
    src/renderer/IBLBuilder.cpp
    src/renderer/vulkan/VulkanFrameSync.h
    src/renderer/vulkan/VulkanFrameSync.cpp
    src/renderer/vulkan/VulkanShader.h
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/GraphicsPipeline.h
    src/renderer/vulkan/GraphicsPipeline.cpp
    src/terrain/TerrainBounds.h
    src/terrain/TerrainConfig.h
    src/terrain/HeightmapData.h
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainChunk.h
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.h
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.h
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/terrain/TerrainRenderer.h
    src/terrain/TerrainRenderer.cpp
    src/lod/LODLevel.h
    src/lod/LODConfig.h
    src/lod/LODStats.h
    src/lod/LODGroup.h
    src/lod/LODGroup.cpp
    src/lod/LODManager.h
    src/lod/LODManager.cpp
    src/lod/MeshSimplifier.h
    src/lod/MeshSimplifier.cpp
    src/foliage/FoliageConfig.h
    src/foliage/FoliageStats.h
    src/foliage/FoliageType.h
    src/foliage/FoliageInstance.h
    src/foliage/FoliageBatch.h
    src/foliage/FoliageBatch.cpp
    src/foliage/ProceduralFoliage.h
    src/foliage/ProceduralFoliage.cpp
    src/foliage/FoliageSystem.h
    src/foliage/FoliageSystem.cpp
    src/foliage/FoliageRenderer.h
    src/foliage/FoliageRenderer.cpp
    src/streaming/StreamingState.h
    src/streaming/StreamingConfig.h
    src/streaming/StreamingStats.h
    src/streaming/StreamingSource.h
    src/streaming/StreamingRegion.h
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.h
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.h
    src/streaming/WorldStreamingManager.cpp
    src/partition/WorldPartitionConfig.h
    src/partition/WorldPartitionStats.h
    src/partition/WorldPartitionGrid.h
    src/partition/WorldPartitionObject.h
    src/partition/WorldPartitionCell.h
    src/partition/WorldPartitionCell.cpp
    src/partition/WorldPartition.h
    src/partition/WorldPartition.cpp
    src/partition/WorldPartitionManager.h
    src/partition/WorldPartitionManager.cpp
    src/worldgen/WorldSeed.h
    src/worldgen/WorldGenerationConfig.h
    src/worldgen/WorldGenerationStatistics.h
    src/worldgen/WorldNoise.h
    src/worldgen/WorldNoise.cpp
    src/worldgen/BiomeGenerator.h
    src/worldgen/BiomeGenerator.cpp
    src/worldgen/TerrainGenerationPipeline.h
    src/worldgen/TerrainGenerationPipeline.cpp
    src/worldgen/WorldRegionGenerator.h
    src/worldgen/WorldRegionGenerator.cpp
    src/worldgen/WorldGenerator.h
    src/worldgen/WorldGenerator.cpp
    src/vegetation/VegetationSpecies.h
    src/vegetation/VegetationExclusionZone.h
    src/vegetation/VegetationDensityMap.h
    src/vegetation/VegetationDensityMap.cpp
    src/vegetation/VegetationTypes.h
    src/vegetation/VegetationDistributionManager.h
    src/vegetation/VegetationDistributionManager.cpp
    src/vegetation/GPUVegetationData.h
    src/vegetation/GPUVegetationSystem.h
    src/vegetation/GPUVegetationSystem.cpp
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
        ${stb_SOURCE_DIR}
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

# ------------------------------------------------------------------------------
# Milestone 18 Terrain Unit Tests
# ------------------------------------------------------------------------------
add_executable(test_terrain
    tests/test_terrain.cpp
    tests/stb_image_impl.cpp
    src/terrain/HeightmapData.h
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainBounds.h
    src/terrain/TerrainConfig.h
    src/core/Log.h
    src/core/Log.cpp
    src/renderer/TextureManager.h
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/SamplerManager.h
    src/renderer/vulkan/SamplerManager.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.h
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.h
    src/renderer/vulkan/VulkanDevice.cpp
)

target_include_directories(test_terrain
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_terrain
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_terrain PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_terrain PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_terrain PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_terrain
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# ------------------------------------------------------------------------------
# Milestone 19 Level of Detail (LOD) Unit Tests
# ------------------------------------------------------------------------------
add_executable(test_lod
    tests/test_lod.cpp
    src/lod/LODLevel.h
    src/lod/LODConfig.h
    src/lod/LODStats.h
    src/lod/LODGroup.h
    src/lod/LODGroup.cpp
    src/lod/LODManager.h
    src/lod/LODManager.cpp
    src/lod/MeshSimplifier.h
    src/lod/MeshSimplifier.cpp
    src/core/Log.h
    src/core/Log.cpp
)

target_include_directories(test_lod
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_lod
    PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_lod PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_lod PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_lod PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_lod
    PRIVATE
        Vulkan::Vulkan
        glm::glm
)

# ------------------------------------------------------------------------------
# Milestone 20 Foliage System Unit Tests
# ------------------------------------------------------------------------------
add_executable(test_foliage
    tests/test_foliage.cpp
    tests/stb_image_impl.cpp
    src/foliage/FoliageConfig.h
    src/foliage/FoliageStats.h
    src/foliage/FoliageType.h
    src/foliage/FoliageInstance.h
    src/foliage/FoliageBatch.h
    src/foliage/FoliageBatch.cpp
    src/foliage/ProceduralFoliage.h
    src/foliage/ProceduralFoliage.cpp
    src/foliage/FoliageSystem.h
    src/foliage/FoliageSystem.cpp
    src/terrain/HeightmapData.h
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainBounds.h
    src/terrain/TerrainConfig.h
    src/terrain/TerrainChunk.h
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.h
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.h
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/lod/LODLevel.h
    src/lod/LODConfig.h
    src/lod/LODStats.h
    src/lod/LODGroup.h
    src/lod/LODGroup.cpp
    src/lod/LODManager.h
    src/lod/LODManager.cpp
    src/lod/MeshSimplifier.h
    src/lod/MeshSimplifier.cpp
    src/renderer/BoundingVolume.h
    src/renderer/Frustum.h
    src/renderer/Camera.h
    src/renderer/Camera.cpp
    src/renderer/Mesh.h
    src/renderer/Mesh.cpp
    src/renderer/Material.h
    src/renderer/InstanceData.h
    src/renderer/InstanceBatch.h
    src/renderer/InstanceBatch.cpp
    src/renderer/TextureManager.h
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/SamplerManager.h
    src/renderer/vulkan/SamplerManager.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.h
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.h
    src/renderer/vulkan/VulkanDevice.cpp
    src/core/Log.h
    src/core/Log.cpp
    src/core/Window.h
    src/core/Window.cpp
)

target_include_directories(test_foliage
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_foliage
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_foliage PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_foliage PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_foliage PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_foliage
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# ------------------------------------------------------------------------------
# Pre-Milestone 21 Normal Direction & Shader Audit Unit Tests
# ------------------------------------------------------------------------------
add_executable(test_normal_audit
    tests/test_normal_audit.cpp
    tests/stb_image_impl.cpp
    src/core/Log.h
    src/core/Log.cpp
    src/core/Window.h
    src/core/Window.cpp
    src/renderer/Vertex.h
    src/renderer/BoundingVolume.h
    src/renderer/Frustum.h
    src/renderer/Camera.h
    src/renderer/Camera.cpp
    src/renderer/Mesh.h
    src/renderer/Mesh.cpp
    src/renderer/ProceduralMeshes.h
    src/renderer/ProceduralMeshes.cpp
    src/foliage/ProceduralFoliage.h
    src/foliage/ProceduralFoliage.cpp
    src/terrain/TerrainBounds.h
    src/terrain/TerrainConfig.h
    src/terrain/HeightmapData.h
    src/terrain/HeightmapData.cpp
    src/lod/MeshSimplifier.h
    src/lod/MeshSimplifier.cpp
    src/renderer/Material.h
    src/renderer/TextureManager.h
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/SamplerManager.h
    src/renderer/vulkan/SamplerManager.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.h
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.h
    src/renderer/vulkan/VulkanDevice.cpp
)

target_include_directories(test_normal_audit
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_normal_audit
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_normal_audit PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_normal_audit PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_normal_audit PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_normal_audit
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# ------------------------------------------------------------------------------
# Milestone 21 World Streaming Unit Tests
# ------------------------------------------------------------------------------
add_executable(test_world_streaming
    tests/test_world_streaming.cpp
    tests/stb_image_impl.cpp
    src/streaming/StreamingState.h
    src/streaming/StreamingConfig.h
    src/streaming/StreamingStats.h
    src/streaming/StreamingSource.h
    src/streaming/StreamingRegion.h
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.h
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.h
    src/streaming/WorldStreamingManager.cpp
    src/terrain/HeightmapData.h
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainBounds.h
    src/terrain/TerrainConfig.h
    src/terrain/TerrainChunk.h
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.h
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.h
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/core/Window.h
    src/core/Window.cpp
    src/foliage/FoliageConfig.h
    src/foliage/FoliageStats.h
    src/foliage/FoliageType.h
    src/foliage/FoliageInstance.h
    src/foliage/FoliageBatch.h
    src/foliage/FoliageBatch.cpp
    src/foliage/FoliageSystem.h
    src/foliage/FoliageSystem.cpp
    src/renderer/Mesh.h
    src/renderer/Mesh.cpp
    src/renderer/BoundingVolume.h
    src/renderer/Vertex.h
    src/renderer/Camera.h
    src/renderer/Camera.cpp
    src/renderer/Frustum.h
    src/renderer/InstanceBatch.h
    src/renderer/InstanceBatch.cpp
    src/renderer/InstanceData.h
    src/lod/LODGroup.h
    src/lod/LODGroup.cpp
    src/lod/LODManager.h
    src/lod/LODManager.cpp
    src/lod/MeshSimplifier.h
    src/lod/MeshSimplifier.cpp
    src/core/Log.h
    src/core/Log.cpp
    src/renderer/TextureManager.h
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/SamplerManager.h
    src/renderer/vulkan/SamplerManager.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.h
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.h
    src/renderer/vulkan/VulkanDevice.cpp
)

target_include_directories(test_world_streaming
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_world_streaming
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_world_streaming PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_world_streaming PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_world_streaming PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_world_streaming
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# ------------------------------------------------------------------------------
# Test Target: test_world_partition (Milestone 22)
# ------------------------------------------------------------------------------
add_executable(test_world_partition
    tests/test_world_partition.cpp
    tests/stb_image_impl.cpp
    src/partition/WorldPartitionConfig.h
    src/partition/WorldPartitionStats.h
    src/partition/WorldPartitionGrid.h
    src/partition/WorldPartitionObject.h
    src/partition/WorldPartitionCell.h
    src/partition/WorldPartitionCell.cpp
    src/partition/WorldPartition.h
    src/partition/WorldPartition.cpp
    src/partition/WorldPartitionManager.h
    src/partition/WorldPartitionManager.cpp
    src/worldgen/WorldSeed.h
    src/worldgen/WorldGenerationConfig.h
    src/worldgen/WorldGenerationStatistics.h
    src/worldgen/WorldNoise.h
    src/worldgen/WorldNoise.cpp
    src/worldgen/BiomeGenerator.h
    src/worldgen/BiomeGenerator.cpp
    src/worldgen/TerrainGenerationPipeline.h
    src/worldgen/TerrainGenerationPipeline.cpp
    src/worldgen/WorldRegionGenerator.h
    src/worldgen/WorldRegionGenerator.cpp
    src/worldgen/WorldGenerator.h
    src/worldgen/WorldGenerator.cpp
    src/vegetation/VegetationSpecies.h
    src/vegetation/VegetationExclusionZone.h
    src/vegetation/VegetationDensityMap.h
    src/vegetation/VegetationDensityMap.cpp
    src/vegetation/VegetationTypes.h
    src/vegetation/VegetationDistributionManager.h
    src/vegetation/VegetationDistributionManager.cpp
    src/vegetation/GPUVegetationData.h
    src/vegetation/GPUVegetationSystem.h
    src/vegetation/GPUVegetationSystem.cpp
    src/streaming/StreamingState.h
    src/streaming/StreamingConfig.h
    src/streaming/StreamingStats.h
    src/streaming/StreamingSource.h
    src/streaming/StreamingRegion.h
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.h
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.h
    src/streaming/WorldStreamingManager.cpp
    src/terrain/Terrain.h
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/terrain/TerrainChunk.h
    src/terrain/TerrainChunk.cpp
    src/terrain/HeightmapData.h
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainMaterial.h
    src/terrain/TerrainMaterial.cpp
    src/foliage/FoliageSystem.h
    src/foliage/FoliageSystem.cpp
    src/renderer/Mesh.h
    src/renderer/Mesh.cpp
    src/renderer/Camera.h
    src/renderer/Camera.cpp
    src/renderer/Frustum.h
    src/renderer/InstanceBatch.h
    src/renderer/InstanceBatch.cpp
    src/renderer/InstanceData.h
    src/lod/LODGroup.h
    src/lod/LODGroup.cpp
    src/lod/LODManager.h
    src/lod/LODManager.cpp
    src/lod/MeshSimplifier.h
    src/lod/MeshSimplifier.cpp
    src/core/Log.h
    src/core/Log.cpp
    src/core/Window.h
    src/core/Window.cpp
    src/renderer/TextureManager.h
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/SamplerManager.h
    src/renderer/vulkan/SamplerManager.cpp
    src/renderer/vulkan/VulkanTexture.h
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.h
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/VulkanBuffer.h
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.h
    src/renderer/vulkan/VulkanDevice.cpp
)

target_include_directories(test_world_partition
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_world_partition
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_world_partition PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_world_partition PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_world_partition PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_world_partition
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# -----------------------------------------------------------------------------
# Test Target: test_terrain_lod (Milestone 23)
# -----------------------------------------------------------------------------
add_executable(test_terrain_lod
    tests/test_terrain_lod.cpp
    tests/stb_image_impl.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/InstanceBatch.cpp
    src/renderer/Mesh.cpp
    src/lod/MeshSimplifier.cpp
    src/lod/LODGroup.cpp
    src/lod/LODManager.cpp
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/SamplerManager.cpp
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/foliage/FoliageSystem.cpp
    src/foliage/FoliageBatch.cpp
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.cpp
    src/partition/WorldPartitionCell.cpp
    src/partition/WorldPartition.cpp
    src/partition/WorldPartitionManager.cpp
)

target_include_directories(test_terrain_lod
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_terrain_lod
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_terrain_lod PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_terrain_lod PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_terrain_lod PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_terrain_lod
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# -----------------------------------------------------------------------------
# Test Target: test_world_generation (Milestone 24)
# -----------------------------------------------------------------------------
add_executable(test_world_generation
    tests/test_world_generation.cpp
    tests/stb_image_impl.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/InstanceBatch.cpp
    src/renderer/Mesh.cpp
    src/lod/MeshSimplifier.cpp
    src/lod/LODGroup.cpp
    src/lod/LODManager.cpp
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/SamplerManager.cpp
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/foliage/FoliageSystem.cpp
    src/foliage/FoliageBatch.cpp
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.cpp
    src/partition/WorldPartitionCell.cpp
    src/partition/WorldPartition.cpp
    src/partition/WorldPartitionManager.cpp
    src/worldgen/WorldNoise.cpp
    src/worldgen/BiomeGenerator.cpp
    src/worldgen/TerrainGenerationPipeline.cpp
    src/worldgen/WorldRegionGenerator.cpp
    src/worldgen/WorldGenerator.cpp
)

target_include_directories(test_world_generation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_world_generation
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_world_generation PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_world_generation PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_world_generation PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_world_generation
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# -----------------------------------------------------------------------------
# Test Target: test_vegetation_distribution (Milestone 25)
# -----------------------------------------------------------------------------
add_executable(test_vegetation_distribution
    tests/test_vegetation_distribution.cpp
    tests/stb_image_impl.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/InstanceBatch.cpp
    src/renderer/Mesh.cpp
    src/lod/MeshSimplifier.cpp
    src/lod/LODGroup.cpp
    src/lod/LODManager.cpp
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/SamplerManager.cpp
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/foliage/FoliageSystem.cpp
    src/foliage/FoliageBatch.cpp
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.cpp
    src/partition/WorldPartitionCell.cpp
    src/partition/WorldPartition.cpp
    src/partition/WorldPartitionManager.cpp
    src/worldgen/WorldNoise.cpp
    src/worldgen/BiomeGenerator.cpp
    src/worldgen/TerrainGenerationPipeline.cpp
    src/worldgen/WorldRegionGenerator.cpp
    src/worldgen/WorldGenerator.cpp
    src/vegetation/VegetationDensityMap.cpp
    src/vegetation/VegetationDistributionManager.cpp
)

target_include_directories(test_vegetation_distribution
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_vegetation_distribution
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_vegetation_distribution PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_vegetation_distribution PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_vegetation_distribution PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_vegetation_distribution
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# -----------------------------------------------------------------------------
# Test Target: test_gpu_vegetation (Milestone 26)
# -----------------------------------------------------------------------------
add_executable(test_gpu_vegetation
    tests/test_gpu_vegetation.cpp
    tests/stb_image_impl.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/InstanceBatch.cpp
    src/renderer/Mesh.cpp
    src/lod/MeshSimplifier.cpp
    src/lod/LODGroup.cpp
    src/lod/LODManager.cpp
    src/renderer/TextureManager.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/vulkan/VulkanTexture.cpp
    src/renderer/vulkan/VulkanTextureArray.cpp
    src/renderer/vulkan/SamplerManager.cpp
    src/terrain/HeightmapData.cpp
    src/terrain/TerrainChunk.cpp
    src/terrain/TerrainMaterial.cpp
    src/terrain/Terrain.cpp
    src/terrain/TerrainPatchMesh.cpp
    src/terrain/TerrainQuadtree.cpp
    src/terrain/TerrainLODManager.cpp
    src/foliage/FoliageSystem.cpp
    src/foliage/FoliageBatch.cpp
    src/streaming/StreamingRegion.cpp
    src/streaming/StreamingUploadQueue.cpp
    src/streaming/WorldStreamingManager.cpp
    src/partition/WorldPartitionCell.cpp
    src/partition/WorldPartition.cpp
    src/partition/WorldPartitionManager.cpp
    src/worldgen/WorldNoise.cpp
    src/worldgen/BiomeGenerator.cpp
    src/worldgen/TerrainGenerationPipeline.cpp
    src/worldgen/WorldRegionGenerator.cpp
    src/worldgen/WorldGenerator.cpp
    src/vegetation/VegetationDensityMap.cpp
    src/vegetation/VegetationDistributionManager.cpp
    src/vegetation/GPUVegetationSystem.cpp
)

target_include_directories(test_gpu_vegetation
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${stb_SOURCE_DIR}
)

target_compile_definitions(test_gpu_vegetation
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_gpu_vegetation PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_gpu_vegetation PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_gpu_vegetation PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_gpu_vegetation
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)
