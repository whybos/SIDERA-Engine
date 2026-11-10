#include "renderer/InstanceBatch.h"
#include "core/Log.h"

#include <cmath>
#include <numbers>

namespace sidera {

InstanceBatch::InstanceBatch(uint32_t batchId, const Mesh* mesh, const Material& material)
    : m_batchId(batchId)
    , m_mesh(mesh)
    , m_material(material) {
}

uint32_t InstanceBatch::addInstance(const glm::mat4& transform, uint32_t customId) {
    Instance inst;
    inst.transform = transform;
    inst.customId = customId;

    if (m_mesh) {
        inst.worldAabb = m_mesh->getLocalAABB().transform(transform);
        inst.worldSphere = m_mesh->getLocalBoundingSphere().transform(transform);
    }

    const uint32_t idx = static_cast<uint32_t>(m_instances.size());
    m_instances.push_back(inst);
    return idx;
}

uint32_t InstanceBatch::addInstance(const Instance& instance) {
    Instance inst = instance;
    if (m_mesh && (inst.worldSphere.radius == 0.0f)) {
        inst.worldAabb = m_mesh->getLocalAABB().transform(inst.transform);
        inst.worldSphere = m_mesh->getLocalBoundingSphere().transform(inst.transform);
    }
    const uint32_t idx = static_cast<uint32_t>(m_instances.size());
    m_instances.push_back(inst);
    return idx;
}

void InstanceBatch::updateInstanceTransform(uint32_t index, const glm::mat4& transform) {
    if (index < m_instances.size()) {
        m_instances[index].transform = transform;
        if (m_mesh) {
            m_instances[index].worldAabb = m_mesh->getLocalAABB().transform(transform);
            m_instances[index].worldSphere = m_mesh->getLocalBoundingSphere().transform(transform);
        }
    }
}

void InstanceBatch::removeInstance(uint32_t index) {
    if (index < m_instances.size()) {
        m_instances.erase(m_instances.begin() + index);
    }
}

void InstanceBatch::clear() {
    m_instances.clear();
}

uint32_t InstanceBatchManager::getOrCreateBatch(const Mesh* mesh, const Material& material) {
    const InstanceBatchKey key{mesh, material};
    auto it = m_batchMap.find(key);
    if (it != m_batchMap.end()) {
        return it->second;
    }

    const uint32_t newId = static_cast<uint32_t>(m_batches.size());
    m_batches.push_back(std::make_unique<InstanceBatch>(newId, mesh, material));
    m_batchMap[key] = newId;
    return newId;
}

uint32_t InstanceBatchManager::addInstance(const Mesh* mesh, const Material& material, const glm::mat4& transform, uint32_t customId) {
    const uint32_t batchId = getOrCreateBatch(mesh, material);
    m_batches[batchId]->addInstance(transform, customId);
    ++m_totalInstanceCount;
    return batchId;
}

uint32_t InstanceBatchManager::addInstance(const Mesh* mesh, const Material& material, const Instance& instance) {
    const uint32_t batchId = getOrCreateBatch(mesh, material);
    m_batches[batchId]->addInstance(instance);
    ++m_totalInstanceCount;
    return batchId;
}

void InstanceBatchManager::updateInstanceTransform(uint32_t batchId, uint32_t instanceIndex, const glm::mat4& transform) {
    if (batchId < m_batches.size()) {
        m_batches[batchId]->updateInstanceTransform(instanceIndex, transform);
    }
}

void InstanceBatchManager::clear() {
    m_batches.clear();
    m_batchMap.clear();
    m_totalInstanceCount = 0;
}

void InstanceBatchManager::rebuildGlobalOffsets() {
    uint32_t runningOffset = 0;
    for (auto& batch : m_batches) {
        batch->setFirstGlobalInstance(runningOffset);
        runningOffset += batch->getInstanceCount();
    }
    m_totalInstanceCount = runningOffset;
}

void InstanceBatchManager::populateGpuData(std::vector<GpuInstanceData>& outInstances,
                                           std::vector<GpuBatchInfo>& outBatches) const {
    outInstances.clear();
    outInstances.reserve(m_totalInstanceCount);

    outBatches.clear();
    outBatches.reserve(m_batches.size());

    uint32_t runningGlobalIdx = 0;
    for (uint32_t b = 0; b < m_batches.size(); ++b) {
        const auto& batch = m_batches[b];
        const uint32_t firstSlot = runningGlobalIdx;
        const uint32_t count = batch->getInstanceCount();

        GpuBatchInfo bInfo{};
        bInfo.meshIndex = b;
        bInfo.firstInstance = firstSlot;
        bInfo.instanceCount = count;
        outBatches.push_back(bInfo);

        const Material& mat = batch->getMaterial();
        const glm::vec4 albedoMetallic(mat.albedo, mat.metallic);
        const glm::vec4 roughnessAoTex(mat.roughness, mat.ao, mat.textureStrength, 0.0f);

        for (uint32_t i = 0; i < count; ++i) {
            const Instance& inst = batch->getInstances()[i];

            GpuInstanceData gData{};
            gData.model = inst.transform;
            gData.albedoMetallic = albedoMetallic;
            gData.roughnessAoTex = roughnessAoTex;
            gData.meshInfo = glm::uvec4(b, 1u, inst.customId, 0u);
            gData.boundingSphere = glm::vec4(inst.worldSphere.center, inst.worldSphere.radius);
            gData.aabbMin = glm::vec4(inst.worldAabb.min, 0.0f);
            gData.aabbMax = glm::vec4(inst.worldAabb.max, 0.0f);

            outInstances.push_back(gData);
            ++runningGlobalIdx;
        }
    }
}

void InstanceBatchManager::generateInstancedRing(const Mesh* mesh, const Material& material,
                                                 uint32_t count, float radius, float y, float scale) {
    if (count == 0) return;

    for (uint32_t i = 0; i < count; ++i) {
        const float angle = (2.0f * std::numbers::pi_v<float> * static_cast<float>(i)) / static_cast<float>(count);
        const float px = std::cos(angle) * radius;
        const float pz = std::sin(angle) * radius;

        glm::mat4 m(1.0f);
        m = glm::translate(m, glm::vec3(px, y, pz));
        m = glm::rotate(m, angle + std::numbers::pi_v<float> * 0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::scale(m, glm::vec3(scale));

        addInstance(mesh, material, m, i);
    }
    rebuildGlobalOffsets();
}

void InstanceBatchManager::generateInstancedField(const Mesh* mesh, const Material& material,
                                                  uint32_t count, float halfExtent, float y, float scale) {
    if (count == 0) return;

    const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(count))));
    const float step = (2.0f * halfExtent) / std::max(1.0f, static_cast<float>(side - 1));

    uint32_t generated = 0;
    for (uint32_t r = 0; r < side && generated < count; ++r) {
        for (uint32_t c = 0; c < side && generated < count; ++c) {
            const float px = -halfExtent + static_cast<float>(c) * step;
            const float pz = -halfExtent + static_cast<float>(r) * step;

            // Slight pseudo-random height and rotation jitter
            const float pseudoRand = std::sin(static_cast<float>(generated) * 12.9898f) * 43758.5453f;
            const float fractRand = pseudoRand - std::floor(pseudoRand);
            const float rotAngle = fractRand * 2.0f * std::numbers::pi_v<float>;
            const float jitterY = y + (fractRand - 0.5f) * 0.15f;

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(px, jitterY, pz));
            m = glm::rotate(m, rotAngle, glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::scale(m, glm::vec3(scale * (0.85f + 0.3f * fractRand)));

            addInstance(mesh, material, m, generated);
            ++generated;
        }
    }
    rebuildGlobalOffsets();
}

} // namespace sidera
