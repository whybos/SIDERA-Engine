#include "renderer/ViewportManager.h"
#include "core/Log.h"

#include <algorithm>

namespace sidera {

ViewportManager::ViewportManager() {
    rebuildLayout();
}

void ViewportManager::setLayout(ViewportLayout layout) {
    if (m_layout == layout) return;
    m_layout = layout;
    rebuildLayout();
    updateExtents(m_lastWidth, m_lastHeight);
    SIDERA_LOG_INFO("ViewportManager", "Switched viewport layout to '{}' (Active viewports: {}).",
                    getViewportLayoutName(m_layout), m_viewports.size());
}

void ViewportManager::cycleLayout() {
    auto next = static_cast<uint32_t>(m_layout) + 1;
    if (next > static_cast<uint32_t>(ViewportLayout::Quad)) {
        next = 0;
    }
    setLayout(static_cast<ViewportLayout>(next));
}

void ViewportManager::toggleSplit() {
    if (m_layout == ViewportLayout::Single) {
        setLayout(ViewportLayout::SplitHorizontal);
    } else {
        setLayout(ViewportLayout::Single);
    }
}

void ViewportManager::setActiveViewportIndex(uint32_t index) noexcept {
    if (index < m_viewports.size()) {
        m_activeViewportIndex = index;
    }
}

Viewport& ViewportManager::getActiveViewport() noexcept {
    if (m_activeViewportIndex >= m_viewports.size()) {
        m_activeViewportIndex = 0;
    }
    return m_viewports[m_activeViewportIndex];
}

const Viewport& ViewportManager::getActiveViewport() const noexcept {
    if (m_activeViewportIndex >= m_viewports.size()) {
        return m_viewports[0];
    }
    return m_viewports[m_activeViewportIndex];
}

void ViewportManager::cycleActiveViewportMode() {
    auto& vp = getActiveViewport();
    auto nextMode = static_cast<uint32_t>(vp.mode) + 1;
    if (nextMode >= static_cast<uint32_t>(ViewportMode::Count)) {
        nextMode = 0;
    }
    vp.mode = static_cast<ViewportMode>(nextMode);
    SIDERA_LOG_INFO("ViewportManager", "Viewport '{}' (#{}) mode changed to '{}'.",
                    vp.name, vp.id, getViewportModeName(vp.mode));
}

void ViewportManager::setActiveViewportMode(ViewportMode mode) {
    auto& vp = getActiveViewport();
    vp.mode = mode;
    SIDERA_LOG_INFO("ViewportManager", "Viewport '{}' (#{}) mode set to '{}'.",
                    vp.name, vp.id, getViewportModeName(vp.mode));
}

void ViewportManager::toggleNormalSpace() {
    auto& vp = getActiveViewport();
    vp.normalSpace = (vp.normalSpace == 0) ? 1 : 0;
    SIDERA_LOG_INFO("ViewportManager", "Viewport '{}' (#{}) Normal space set to: {}.",
                    vp.name, vp.id, (vp.normalSpace == 0) ? "WORLD SPACE" : "VIEW SPACE");
}

void ViewportManager::rebuildLayout() {
    // Preserve current modes if available
    ViewportMode mode0 = ViewportMode::Lit;
    ViewportMode mode1 = ViewportMode::Wireframe;
    ViewportMode mode2 = ViewportMode::Normals;
    ViewportMode mode3 = ViewportMode::Roughness;

    if (!m_viewports.empty()) mode0 = m_viewports[0].mode;
    if (m_viewports.size() > 1) mode1 = m_viewports[1].mode;
    if (m_viewports.size() > 2) mode2 = m_viewports[2].mode;
    if (m_viewports.size() > 3) mode3 = m_viewports[3].mode;

    m_viewports.clear();

    switch (m_layout) {
        case ViewportLayout::Single: {
            Viewport v{};
            v.id = 0;
            v.name = "Viewport 1 (Main)";
            v.mode = mode0;
            v.relativeX = 0.0f;
            v.relativeY = 0.0f;
            v.relativeWidth = 1.0f;
            v.relativeHeight = 1.0f;
            m_viewports.push_back(v);
            break;
        }
        case ViewportLayout::SplitHorizontal: {
            Viewport v0{};
            v0.id = 0;
            v0.name = "Viewport Left";
            v0.mode = mode0;
            v0.relativeX = 0.0f;
            v0.relativeY = 0.0f;
            v0.relativeWidth = 0.5f;
            v0.relativeHeight = 1.0f;
            m_viewports.push_back(v0);

            Viewport v1{};
            v1.id = 1;
            v1.name = "Viewport Right";
            v1.mode = mode1;
            v1.relativeX = 0.5f;
            v1.relativeY = 0.0f;
            v1.relativeWidth = 0.5f;
            v1.relativeHeight = 1.0f;
            m_viewports.push_back(v1);
            break;
        }
        case ViewportLayout::SplitVertical: {
            Viewport v0{};
            v0.id = 0;
            v0.name = "Viewport Top";
            v0.mode = mode0;
            v0.relativeX = 0.0f;
            v0.relativeY = 0.0f;
            v0.relativeWidth = 1.0f;
            v0.relativeHeight = 0.5f;
            m_viewports.push_back(v0);

            Viewport v1{};
            v1.id = 1;
            v1.name = "Viewport Bottom";
            v1.mode = mode1;
            v1.relativeX = 0.0f;
            v1.relativeY = 0.5f;
            v1.relativeWidth = 1.0f;
            v1.relativeHeight = 0.5f;
            m_viewports.push_back(v1);
            break;
        }
        case ViewportLayout::Quad: {
            Viewport v0{};
            v0.id = 0;
            v0.name = "Top-Left (Lit)";
            v0.mode = mode0;
            v0.relativeX = 0.0f;
            v0.relativeY = 0.0f;
            v0.relativeWidth = 0.5f;
            v0.relativeHeight = 0.5f;
            m_viewports.push_back(v0);

            Viewport v1{};
            v1.id = 1;
            v1.name = "Top-Right (Wireframe)";
            v1.mode = mode1;
            v1.relativeX = 0.5f;
            v1.relativeY = 0.0f;
            v1.relativeWidth = 0.5f;
            v1.relativeHeight = 0.5f;
            m_viewports.push_back(v1);

            Viewport v2{};
            v2.id = 2;
            v2.name = "Bottom-Left (Normals)";
            v2.mode = mode2;
            v2.relativeX = 0.0f;
            v2.relativeY = 0.5f;
            v2.relativeWidth = 0.5f;
            v2.relativeHeight = 0.5f;
            m_viewports.push_back(v2);

            Viewport v3{};
            v3.id = 3;
            v3.name = "Bottom-Right (Roughness)";
            v3.mode = mode3;
            v3.relativeX = 0.5f;
            v3.relativeY = 0.5f;
            v3.relativeWidth = 0.5f;
            v3.relativeHeight = 0.5f;
            m_viewports.push_back(v3);
            break;
        }
    }

    if (m_activeViewportIndex >= m_viewports.size()) {
        m_activeViewportIndex = 0;
    }
}

void ViewportManager::updateExtents(uint32_t fbWidth, uint32_t fbHeight) {
    if (fbWidth == 0 || fbHeight == 0) return;
    m_lastWidth = fbWidth;
    m_lastHeight = fbHeight;

    const float fW = static_cast<float>(fbWidth);
    const float fH = static_cast<float>(fbHeight);

    for (auto& vp : m_viewports) {
        vp.vkViewport.x = vp.relativeX * fW;
        vp.vkViewport.y = vp.relativeY * fH;
        vp.vkViewport.width = vp.relativeWidth * fW;
        vp.vkViewport.height = vp.relativeHeight * fH;
        vp.vkViewport.minDepth = 0.0f;
        vp.vkViewport.maxDepth = 1.0f;

        vp.vkScissor.offset.x = static_cast<int32_t>(vp.vkViewport.x);
        vp.vkScissor.offset.y = static_cast<int32_t>(vp.vkViewport.y);
        vp.vkScissor.extent.width = static_cast<uint32_t>(vp.vkViewport.width);
        vp.vkScissor.extent.height = static_cast<uint32_t>(vp.vkViewport.height);
    }
}

} // namespace sidera
