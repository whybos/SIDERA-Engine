#pragma once

#include "renderer/Viewport.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace sidera {

class ViewportManager {
public:
    ViewportManager();
    ~ViewportManager() = default;

    void setLayout(ViewportLayout layout);
    void cycleLayout();
    void toggleSplit();

    [[nodiscard]] ViewportLayout getLayout() const noexcept { return m_layout; }
    [[nodiscard]] uint32_t getActiveViewportIndex() const noexcept { return m_activeViewportIndex; }
    void setActiveViewportIndex(uint32_t index) noexcept;

    [[nodiscard]] Viewport& getActiveViewport() noexcept;
    [[nodiscard]] const Viewport& getActiveViewport() const noexcept;

    void cycleActiveViewportMode();
    void setActiveViewportMode(ViewportMode mode);
    void toggleNormalSpace();

    void updateExtents(uint32_t fbWidth, uint32_t fbHeight);

    [[nodiscard]] const std::vector<Viewport>& getViewports() const noexcept { return m_viewports; }
    [[nodiscard]] size_t getViewportCount() const noexcept { return m_viewports.size(); }

private:
    void rebuildLayout();

    ViewportLayout m_layout = ViewportLayout::Single;
    uint32_t m_activeViewportIndex = 0;
    std::vector<Viewport> m_viewports;
    uint32_t m_lastWidth = 1920;
    uint32_t m_lastHeight = 1080;
};

} // namespace sidera
