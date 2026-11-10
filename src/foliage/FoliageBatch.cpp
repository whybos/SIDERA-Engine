#include "foliage/FoliageBatch.h"

namespace sidera {

FoliageBatch::FoliageBatch(uint32_t batchId, const Mesh* mesh, const Material& material, bool castShadows)
    : m_batchId(batchId)
    , m_mesh(mesh)
    , m_material(material)
    , m_castShadows(castShadows) {
}

void FoliageBatch::clear() noexcept {
    m_instances.clear();
    m_bounds = AABB{};
}

void FoliageBatch::addInstance(const FoliageInstance& instance) {
    if (m_instances.empty()) {
        m_bounds = instance.worldBounds;
    } else {
        m_bounds = m_bounds.merge(instance.worldBounds);
    }
    m_instances.push_back(&instance);
}

} // namespace sidera
