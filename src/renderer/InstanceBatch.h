#pragma once

#include "renderer/InstanceData.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"

#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace sidera {

// Unique identifier key for an instance batch: identical Mesh + identical PBR Material
struct InstanceBatchKey {
    const Mesh* mesh = nullptr;
    Material material{};

    [[nodiscard]] constexpr bool operator==(const InstanceBatchKey& o) const noexcept {
        return mesh == o.mesh && material == o.material;
    }
};

} // namespace sidera

namespace std {
template <>
struct hash<sidera::InstanceBatchKey> {
    size_t operator()(const sidera::InstanceBatchKey& k) const noexcept {
        return std::hash<const void*>()(k.mesh) ^ (std::hash<sidera::Material>()(k.material) << 1);
    }
};
} // namespace std

namespace sidera {

class InstanceBatch {
public:
    InstanceBatch(uint32_t batchId, const Mesh* mesh, const Material& material);

    uint32_t addInstance(const glm::mat4& transform, uint32_t customId = 0);
    uint32_t addInstance(const Instance& instance);
    void updateInstanceTransform(uint32_t index, const glm::mat4& transform);
    void removeInstance(uint32_t index);
    void clear();

    [[nodiscard]] uint32_t getBatchId() const noexcept { return m_batchId; }
    [[nodiscard]] const Mesh* getMesh() const noexcept { return m_mesh; }
    [[nodiscard]] const Material& getMaterial() const noexcept { return m_material; }
    [[nodiscard]] uint32_t getInstanceCount() const noexcept { return static_cast<uint32_t>(m_instances.size()); }
    [[nodiscard]] const std::vector<Instance>& getInstances() const noexcept { return m_instances; }
    [[nodiscard]] std::vector<Instance>& getInstances() noexcept { return m_instances; }
    [[nodiscard]] uint32_t getFirstGlobalInstance() const noexcept { return m_firstGlobalInstance; }
    void setFirstGlobalInstance(uint32_t offset) noexcept { m_firstGlobalInstance = offset; }

private:
    uint32_t m_batchId = 0;
    const Mesh* m_mesh = nullptr;
    Material m_material{};
    uint32_t m_firstGlobalInstance = 0;
    std::vector<Instance> m_instances;
};

class InstanceBatchManager {
public:
    InstanceBatchManager() = default;

    uint32_t getOrCreateBatch(const Mesh* mesh, const Material& material);
    uint32_t addInstance(const Mesh* mesh, const Material& material, const glm::mat4& transform, uint32_t customId = 0);
    uint32_t addInstance(const Mesh* mesh, const Material& material, const Instance& instance);
    void updateInstanceTransform(uint32_t batchId, uint32_t instanceIndex, const glm::mat4& transform);
    void clear();

    void rebuildGlobalOffsets();

    [[nodiscard]] const std::vector<std::unique_ptr<InstanceBatch>>& getBatches() const noexcept { return m_batches; }
    [[nodiscard]] uint32_t getBatchCount() const noexcept { return static_cast<uint32_t>(m_batches.size()); }
    [[nodiscard]] uint32_t getTotalInstanceCount() const noexcept { return m_totalInstanceCount; }

    void populateGpuData(std::vector<GpuInstanceData>& outInstances, std::vector<GpuBatchInfo>& outBatches) const;

    // Procedural instancing generators for testing
    void generateInstancedRing(const Mesh* mesh, const Material& material, uint32_t count, float radius, float y = 0.0f, float scale = 1.0f);
    void generateInstancedField(const Mesh* mesh, const Material& material, uint32_t count, float halfExtent, float y = 0.0f, float scale = 1.0f);

private:
    std::vector<std::unique_ptr<InstanceBatch>> m_batches;
    std::unordered_map<InstanceBatchKey, uint32_t> m_batchMap;
    uint32_t m_totalInstanceCount = 0;
};

} // namespace sidera
