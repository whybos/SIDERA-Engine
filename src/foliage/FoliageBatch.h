#pragma once

#include "foliage/FoliageInstance.h"
#include "renderer/BoundingVolume.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"

#include <vector>

namespace sidera {

// Groups foliage instances that share the exact same Mesh geometry and PBR Material
class FoliageBatch {
public:
    FoliageBatch(uint32_t batchId, const Mesh* mesh, const Material& material, bool castShadows);

    void clear() noexcept;
    void addInstance(const FoliageInstance& instance);

    [[nodiscard]] uint32_t getBatchId() const noexcept { return m_batchId; }
    [[nodiscard]] const Mesh* getMesh() const noexcept { return m_mesh; }
    [[nodiscard]] const Material& getMaterial() const noexcept { return m_material; }
    [[nodiscard]] bool getCastShadows() const noexcept { return m_castShadows; }
    [[nodiscard]] uint32_t getInstanceCount() const noexcept { return static_cast<uint32_t>(m_instances.size()); }
    [[nodiscard]] const std::vector<const FoliageInstance*>& getInstances() const noexcept { return m_instances; }
    [[nodiscard]] const AABB& getBounds() const noexcept { return m_bounds; }

private:
    uint32_t m_batchId = 0;
    const Mesh* m_mesh = nullptr;
    Material m_material{};
    bool m_castShadows = true;
    std::vector<const FoliageInstance*> m_instances;
    AABB m_bounds{};
};

} // namespace sidera
