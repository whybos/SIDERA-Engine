#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::runtime {

enum class UIWidgetType : uint32_t {
    Canvas = 0,
    Panel = 1,
    Text = 2,
    Image = 3,
    Button = 4,
    Checkbox = 5,
    Slider = 6,
    ProgressBar = 7
};

struct UIRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 100.0f;
    float height = 32.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }
};

struct UIDrawCommand {
    uint64_t widgetId = 0;
    UIWidgetType type = UIWidgetType::Panel;
    UIRect bounds{};
    std::string text;
    float value = 0.0f;
};

struct UIWidget {
    uint64_t id = 0;
    uint64_t parentId = 0;
    UIWidgetType type = UIWidgetType::Panel;
    std::string name;
    std::string text;
    std::string localizationKey;
    UIRect localRect{};
    UIRect computedRect{};
    float anchorX = 0.0f; // [0..1]
    float anchorY = 0.0f; // [0..1]
    bool visible = true;
    bool enabled = true;
    bool focusable = false;
    float value = 0.0f; // slider / progress / checkbox (0 or 1)
    std::function<void()> onClick;
    std::function<void(float)> onValueChanged;
};

// Retained-mode Runtime UI System (strictly independent of Editor UI / Dear ImGui).
class UISystem {
public:
    uint64_t createWidget(const UIWidget& desc) {
        UIWidget w = desc;
        w.id = m_nextId++;
        m_order.push_back(w.id);
        m_widgets[w.id] = std::move(w);
        return m_order.back();
    }

    bool destroyWidget(uint64_t id) {
        if (m_widgets.erase(id) == 0) return false;
        m_order.erase(std::remove(m_order.begin(), m_order.end(), id), m_order.end());
        if (m_focusedId == id) m_focusedId = 0;
        return true;
    }

    [[nodiscard]] UIWidget* findWidget(uint64_t id) noexcept {
        auto it = m_widgets.find(id);
        return (it != m_widgets.end()) ? &it->second : nullptr;
    }

    void computeLayout(float viewportWidth, float viewportHeight, float dpiScale = 1.0f) noexcept {
        const float scale = std::clamp(dpiScale, 0.5f, 3.0f);
        for (uint64_t id : m_order) {
            auto& w = m_widgets[id];
            UIRect parentBounds{0.0f, 0.0f, viewportWidth, viewportHeight};
            if (w.parentId != 0) {
                if (const auto* p = findWidget(w.parentId)) parentBounds = p->computedRect;
            }
            w.computedRect.x = parentBounds.x + parentBounds.width * w.anchorX + w.localRect.x * scale;
            w.computedRect.y = parentBounds.y + parentBounds.height * w.anchorY + w.localRect.y * scale;
            w.computedRect.width = w.localRect.width * scale;
            w.computedRect.height = w.localRect.height * scale;
        }
    }

    bool dispatchPointerClick(float x, float y) {
        for (auto it = m_order.rbegin(); it != m_order.rend(); ++it) {
            auto& w = m_widgets[*it];
            if (!w.visible || !w.enabled) continue;
            if (w.computedRect.contains(x, y)) {
                if (w.focusable) m_focusedId = w.id;
                if (w.type == UIWidgetType::Checkbox) {
                    w.value = (w.value > 0.5f) ? 0.0f : 1.0f;
                    if (w.onValueChanged) w.onValueChanged(w.value);
                }
                if (w.onClick) w.onClick();
                return true;
            }
        }
        return false;
    }

    uint64_t focusNext() {
        std::vector<uint64_t> focusables;
        for (uint64_t id : m_order) {
            const auto& w = m_widgets[id];
            if (w.visible && w.enabled && w.focusable) focusables.push_back(id);
        }
        if (focusables.empty()) { m_focusedId = 0; return 0; }
        auto it = std::find(focusables.begin(), focusables.end(), m_focusedId);
        if (it == focusables.end() || (it + 1) == focusables.end()) m_focusedId = focusables.front();
        else m_focusedId = *(it + 1);
        return m_focusedId;
    }

    [[nodiscard]] std::vector<UIDrawCommand> buildDrawList() const {
        std::vector<UIDrawCommand> list;
        list.reserve(m_order.size());
        for (uint64_t id : m_order) {
            const auto& w = m_widgets.at(id);
            if (!w.visible) continue;
            list.push_back({w.id, w.type, w.computedRect, w.text, w.value});
        }
        return list;
    }

    [[nodiscard]] uint64_t getFocusedWidgetId() const noexcept { return m_focusedId; }
    [[nodiscard]] size_t getWidgetCount() const noexcept { return m_widgets.size(); }
    void clear() noexcept { m_widgets.clear(); m_order.clear(); m_focusedId = 0; }

private:
    uint64_t m_nextId = 1;
    uint64_t m_focusedId = 0;
    std::vector<uint64_t> m_order;
    std::unordered_map<uint64_t, UIWidget> m_widgets;
};

} // namespace sidera::runtime
