cmake_minimum_required(VERSION 3.24)

project(SIDERA
    VERSION 1.0.0
    DESCRIPTION "SIDERA Engine 1.0 - High-Performance Vulkan Game Engine"
    LANGUAGES CXX C
)

option(SIDERA_BUILD_TESTS "Build SIDERA Engine automated test targets" ON)
option(SIDERA_BUILD_EXAMPLES "Include SIDERA Engine reusable example projects in install packages" ON)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vt_feedback_resolve.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/light_culling.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/deferred_lighting.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/deferred_lighting.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_bloom.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_ssao.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_tonemap.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_inject.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_temporal.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_integrate.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_composite.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssgi_trace.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssgi_temporal.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssgi_denoise.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssgi_composite.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_trace.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_temporal.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr_denoise.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/reflection_composite.comp
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
    src/virtualtexturing/VirtualTextureTypes.h
    src/virtualtexturing/VirtualTexturePageTable.h
    src/virtualtexturing/VirtualTexturePageTable.cpp
    src/virtualtexturing/VirtualTexturePhysicalCache.h
    src/virtualtexturing/VirtualTexturePhysicalCache.cpp
    src/virtualtexturing/VirtualTextureSystem.h
    src/virtualtexturing/VirtualTextureSystem.cpp
    src/renderer/lighting/RenderPath.h
    src/renderer/lighting/LightTypes.h
    src/renderer/lighting/GBuffer.h
    src/renderer/lighting/GBuffer.cpp
    src/renderer/lighting/TiledLightCuller.h
    src/renderer/lighting/TiledLightCuller.cpp
    src/renderer/postprocess/PostProcessSettings.h
    src/renderer/postprocess/PostProcessPipeline.h
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/volumetric/VolumetricSettings.h
    src/renderer/volumetric/VolumetricPipeline.h
    src/renderer/volumetric/VolumetricPipeline.cpp
    src/renderer/gi/GlobalIlluminationSettings.h
    src/renderer/gi/GlobalIlluminationPipeline.h
    src/renderer/gi/GlobalIlluminationPipeline.cpp
    src/renderer/reflection/ReflectionSettings.h
    src/renderer/reflection/ReflectionPipeline.h
    src/renderer/reflection/ReflectionPipeline.cpp
    src/profiling/GPUProfileTypes.h
    src/profiling/GPUProfiler.h
    src/profiling/GPUProfiler.cpp
    src/memory/MemoryTypes.h
    src/memory/MemoryBudgetManager.h
    src/memory/MemoryBudgetManager.cpp
    src/assets/AssetTypes.h
    src/assets/AssetImporter.h
    src/assets/AssetDatabase.h
    src/assets/AssetDatabase.cpp
    src/scene/SceneTypes.h
    src/scene/Scene.h
    src/scene/Scene.cpp
    src/scene/SceneSerializer.h
    src/scene/SceneSerializer.cpp
    src/physics/PhysicsTypes.h
    src/physics/PhysicsWorld.h
    src/physics/PhysicsWorld.cpp
    src/audio/AudioTypes.h
    src/audio/AudioSystem.h
    src/audio/AudioSystem.cpp
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
    src/virtualtexturing/VirtualTextureTypes.h
    src/virtualtexturing/VirtualTexturePageTable.h
    src/virtualtexturing/VirtualTexturePageTable.cpp
    src/virtualtexturing/VirtualTexturePhysicalCache.h
    src/virtualtexturing/VirtualTexturePhysicalCache.cpp
    src/virtualtexturing/VirtualTextureSystem.h
    src/virtualtexturing/VirtualTextureSystem.cpp
    src/renderer/lighting/RenderPath.h
    src/renderer/lighting/LightTypes.h
    src/renderer/lighting/GBuffer.h
    src/renderer/lighting/GBuffer.cpp
    src/renderer/lighting/TiledLightCuller.h
    src/renderer/lighting/TiledLightCuller.cpp
    src/renderer/postprocess/PostProcessSettings.h
    src/renderer/postprocess/PostProcessPipeline.h
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/volumetric/VolumetricSettings.h
    src/renderer/volumetric/VolumetricPipeline.h
    src/renderer/volumetric/VolumetricPipeline.cpp
    src/renderer/gi/GlobalIlluminationSettings.h
    src/renderer/gi/GlobalIlluminationPipeline.h
    src/renderer/gi/GlobalIlluminationPipeline.cpp
    src/renderer/reflection/ReflectionSettings.h
    src/renderer/reflection/ReflectionPipeline.h
    src/renderer/reflection/ReflectionPipeline.cpp
    src/profiling/GPUProfileTypes.h
    src/profiling/GPUProfiler.h
    src/profiling/GPUProfiler.cpp
    src/memory/MemoryTypes.h
    src/memory/MemoryBudgetManager.h
    src/memory/MemoryBudgetManager.cpp
    src/assets/AssetTypes.h
    src/assets/AssetImporter.h
    src/assets/AssetDatabase.h
    src/assets/AssetDatabase.cpp
    src/scene/SceneTypes.h
    src/scene/Scene.h
    src/scene/Scene.cpp
    src/scene/SceneSerializer.h
    src/scene/SceneSerializer.cpp
    src/physics/PhysicsTypes.h
    src/physics/PhysicsWorld.h
    src/physics/PhysicsWorld.cpp
    src/audio/AudioTypes.h
    src/audio/AudioSystem.h
    src/audio/AudioSystem.cpp
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

# -----------------------------------------------------------------------------
# Test Target: test_virtual_texturing (Milestone 27)
# -----------------------------------------------------------------------------
add_executable(test_virtual_texturing
    tests/test_virtual_texturing.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/virtualtexturing/VirtualTexturePageTable.cpp
    src/virtualtexturing/VirtualTexturePhysicalCache.cpp
    src/virtualtexturing/VirtualTextureSystem.cpp
)

target_include_directories(test_virtual_texturing
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_virtual_texturing
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_virtual_texturing PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_virtual_texturing PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_virtual_texturing PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_virtual_texturing
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# -----------------------------------------------------------------------------
# Test Target: test_deferred_forward_plus (Milestone 28)
# -----------------------------------------------------------------------------
add_executable(test_deferred_forward_plus
    tests/test_deferred_forward_plus.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/lighting/GBuffer.cpp
    src/renderer/lighting/TiledLightCuller.cpp
)

target_include_directories(test_deferred_forward_plus
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_deferred_forward_plus
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_deferred_forward_plus PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_deferred_forward_plus PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_deferred_forward_plus PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_deferred_forward_plus
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)

# -----------------------------------------------------------------------------
# Test Target: test_advanced_post_processing (Milestone 29)
# -----------------------------------------------------------------------------
add_executable(test_advanced_post_processing
    tests/test_advanced_post_processing.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
)

target_include_directories(test_advanced_post_processing
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_advanced_post_processing
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_advanced_post_processing PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_advanced_post_processing PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_advanced_post_processing PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_advanced_post_processing
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_volumetric_rendering (Milestone 30)
# -----------------------------------------------------------------------------
add_executable(test_volumetric_rendering
    tests/test_volumetric_rendering.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/volumetric/VolumetricPipeline.cpp
)

target_include_directories(test_volumetric_rendering
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_volumetric_rendering
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_volumetric_rendering PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_volumetric_rendering PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_volumetric_rendering PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_volumetric_rendering
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_global_illumination (Milestone 31)
# -----------------------------------------------------------------------------
add_executable(test_global_illumination
    tests/test_global_illumination.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/gi/GlobalIlluminationPipeline.cpp
)

target_include_directories(test_global_illumination
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_global_illumination
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_global_illumination PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_global_illumination PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_global_illumination PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_global_illumination
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_reflection_system (Milestone 32)
# -----------------------------------------------------------------------------
add_executable(test_reflection_system
    tests/test_reflection_system.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/Camera.cpp
    src/renderer/vulkan/VulkanBuffer.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/renderer/vulkan/VulkanShader.cpp
    src/renderer/postprocess/PostProcessPipeline.cpp
    src/renderer/reflection/ReflectionPipeline.cpp
)

target_include_directories(test_reflection_system
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_reflection_system
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_reflection_system PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_reflection_system PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_reflection_system PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_reflection_system
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_gpu_profiling (Milestone 33)
# -----------------------------------------------------------------------------
add_executable(test_gpu_profiling
    tests/test_gpu_profiling.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/profiling/GPUProfiler.cpp
)

target_include_directories(test_gpu_profiling
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_gpu_profiling
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_gpu_profiling PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_gpu_profiling PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_gpu_profiling PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_gpu_profiling
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_memory_streaming (Milestone 34)
# -----------------------------------------------------------------------------
add_executable(test_memory_streaming
    tests/test_memory_streaming.cpp
    src/core/Log.cpp
    src/core/Window.cpp
    src/renderer/vulkan/VulkanDevice.cpp
    src/memory/MemoryBudgetManager.cpp
)

target_include_directories(test_memory_streaming
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(test_memory_streaming
    PRIVATE
        GLFW_INCLUDE_VULKAN
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

if(MSVC)
    target_compile_options(test_memory_streaming PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_memory_streaming PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_memory_streaming PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_memory_streaming
    PRIVATE
        Vulkan::Vulkan
        glfw
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_asset_pipeline (Milestone 35)
# -----------------------------------------------------------------------------
add_executable(test_asset_pipeline
    tests/test_asset_pipeline.cpp
    src/assets/AssetDatabase.cpp
)

target_include_directories(test_asset_pipeline
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(test_asset_pipeline PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_asset_pipeline PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_asset_pipeline PRIVATE -Wall -Wextra -Wpedantic)
endif()


# -----------------------------------------------------------------------------
# Test Target: test_scene_serialization (Milestone 36)
# -----------------------------------------------------------------------------
add_executable(test_scene_serialization
    tests/test_scene_serialization.cpp
    src/assets/AssetDatabase.cpp
    src/scene/Scene.cpp
    src/scene/SceneSerializer.cpp
)

target_include_directories(test_scene_serialization
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(test_scene_serialization PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_scene_serialization PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_scene_serialization PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_scene_serialization
    PRIVATE
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_physics_integration (Milestone 37)
# -----------------------------------------------------------------------------
add_executable(test_physics_integration
    tests/test_physics_integration.cpp
    src/physics/PhysicsWorld.cpp
)

target_include_directories(test_physics_integration
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(test_physics_integration PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_physics_integration PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_physics_integration PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_physics_integration
    PRIVATE
        glm::glm
)


# -----------------------------------------------------------------------------
# Test Target: test_audio_system (Milestone 38)
# -----------------------------------------------------------------------------
add_executable(test_audio_system
    tests/test_audio_system.cpp
    src/physics/PhysicsWorld.cpp
    src/audio/AudioSystem.cpp
)

target_include_directories(test_audio_system
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(test_audio_system PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_audio_system PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_audio_system PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_audio_system
    PRIVATE
        glm::glm
)


# -----------------------------------------------------------------------------
# SIDERA Editor Core & Application (Milestone 39.1) — Strictly decoupled from Runtime
# -----------------------------------------------------------------------------
option(SIDERA_BUILD_EDITOR "Build SIDERA Editor Core & Application" ON)

if(SIDERA_BUILD_EDITOR)
    add_library(sidera_editor_core STATIC
        src/editor/EditorTypes.h
        src/editor/EditorCommand.h
        src/editor/EditorSelection.h
        src/editor/EditorPanel.h
        src/editor/EditorApplication.h
        src/editor/EditorApplication.cpp
        src/editor/content/SceneEditCommands.h
        src/editor/content/ContentEditorPanels.h
        src/assets/AssetDatabase.cpp
        src/scene/Scene.cpp
        src/scene/SceneSerializer.cpp
        src/physics/PhysicsWorld.cpp
        src/audio/AudioSystem.cpp
    )

    target_include_directories(sidera_editor_core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    if(MSVC)
        target_compile_options(sidera_editor_core PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
        target_compile_definitions(sidera_editor_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(sidera_editor_core PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    target_link_libraries(sidera_editor_core
        PUBLIC
            glm::glm
    )

    add_executable(sidera_editor
        src/editor/editor_main.cpp
    )
    target_link_libraries(sidera_editor PRIVATE sidera_editor_core)

    add_executable(test_editor_core
        tests/test_editor_core.cpp
    )
    target_link_libraries(test_editor_core PRIVATE sidera_editor_core)

    add_executable(test_editor_content
        tests/test_editor_content.cpp
    )
    target_link_libraries(test_editor_content PRIVATE sidera_editor_core)
endif()


# -----------------------------------------------------------------------------
# Test Target: test_runtime_systems (Milestone 40)
# -----------------------------------------------------------------------------
add_executable(test_runtime_systems
    tests/test_runtime_systems.cpp
)

target_include_directories(test_runtime_systems
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(test_runtime_systems PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
    target_compile_definitions(test_runtime_systems PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(test_runtime_systems PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(test_runtime_systems
    PRIVATE
        glm::glm
)

# -----------------------------------------------------------------------------
# Reusable Game Frameworks (Milestone 41) — Optional Layer on Top of Engine Core
# -----------------------------------------------------------------------------
option(SIDERA_BUILD_FRAMEWORKS "Build reusable SIDERA gameplay frameworks" ON)

if(SIDERA_BUILD_FRAMEWORKS)
    add_library(sidera_framework INTERFACE)
    target_include_directories(sidera_framework
        INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(sidera_framework
        INTERFACE
            glm::glm
    )

    add_executable(test_rpg_framework
        tests/test_rpg_framework.cpp
        src/physics/PhysicsWorld.cpp
    )
    target_link_libraries(test_rpg_framework PRIVATE sidera_framework)

    if(MSVC)
        target_compile_options(test_rpg_framework PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
        target_compile_definitions(test_rpg_framework PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(test_rpg_framework PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# -----------------------------------------------------------------------------
# Milestone 42 — SIDERA Engine 1.0 Release Validation Target & Packaging (CPack)
# -----------------------------------------------------------------------------
if(SIDERA_BUILD_TESTS AND SIDERA_BUILD_EDITOR)
    add_executable(test_engine_1_0_release
        tests/test_engine_1_0_release.cpp
    )
    target_link_libraries(test_engine_1_0_release PRIVATE sidera_editor_core)
    if(MSVC)
        target_compile_options(test_engine_1_0_release PRIVATE /W4 /permissive- /Zc:__cplusplus /EHsc)
        target_compile_definitions(test_engine_1_0_release PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(test_engine_1_0_release PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# Distribution install layout (clean end-user package without build intermediates)
install(TARGETS sidera RUNTIME DESTINATION bin)
if(SIDERA_BUILD_EDITOR)
    install(TARGETS sidera_editor RUNTIME DESTINATION bin)
endif()
if(TARGET vulkan)
    install(FILES $<TARGET_FILE:vulkan> DESTINATION bin)
endif()
install(DIRECTORY $<TARGET_FILE_DIR:sidera>/shaders/ DESTINATION bin/shaders FILES_MATCHING PATTERN "*.spv")
if(SIDERA_BUILD_EXAMPLES)
    install(DIRECTORY examples/rpg_reference_project DESTINATION examples)
endif()
install(FILES
    README.md
    docs/THIRD_PARTY_LICENSES.md
    docs/releases/SIDERA_ENGINE_1_0_RC1.md
    docs/guides/SIDERA_BUILD_RUN_AND_PUBLISH_GUIDE.md
    DESTINATION docs
)
install(FILES
    scripts/Install-SIDERA.ps1
    scripts/Uninstall-SIDERA.ps1
    DESTINATION scripts
)

set(CPACK_PACKAGE_NAME "SIDERA")
set(CPACK_PACKAGE_VENDOR "SIDERA Engine")
set(CPACK_PACKAGE_VERSION "1.0.0-rc1")
set(CPACK_PACKAGE_FILE_NAME "SIDERA-1.0.0-rc1-win64")
set(CPACK_GENERATOR "ZIP")
include(CPack)
