#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace sidera::runtime {

enum class InputContext : uint32_t {
    Gameplay = 0,
    UI = 1,
    Menu = 2,
    Dialogue = 3
};

struct InputActionBinding {
    std::string actionName;
    InputContext context = InputContext::Gameplay;
    int32_t keyCode = 0;
    float deadZone = 0.15f;
    float sensitivity = 1.0f;
    bool invertAxis = false;
    bool toggleMode = false;
};

// Configurable Input Action Mapping, Context Isolation, Analog Deadzone & Accessibility Rebinding.
class InputManager {
public:
    bool registerAction(const InputActionBinding& binding) {
        if (binding.actionName.empty()) return false;
        m_bindings[binding.actionName] = binding;
        return true;
    }

    // Rebind key with conflict detection within the same InputContext
    bool rebindAction(const std::string& actionName, int32_t newKeyCode, bool allowConflict = false) {
        auto it = m_bindings.find(actionName);
        if (it == m_bindings.end()) return false;
        if (!allowConflict) {
            for (const auto& [name, b] : m_bindings) {
                if (name != actionName && b.context == it->second.context && b.keyCode == newKeyCode) {
                    return false; // conflict detected
                }
            }
        }
        it->second.keyCode = newKeyCode;
        return true;
    }

    void setContextActive(InputContext ctx, bool active) {
        if (active) m_activeContexts.insert(ctx);
        else m_activeContexts.erase(ctx);
    }

    [[nodiscard]] bool isContextActive(InputContext ctx) const noexcept {
        return m_activeContexts.count(ctx) > 0;
    }

    void setKeyState(int32_t keyCode, bool pressed) {
        m_prevKeys[keyCode] = m_currKeys[keyCode];
        m_currKeys[keyCode] = pressed;
    }

    [[nodiscard]] bool isActionHeld(const std::string& actionName) const noexcept {
        auto it = m_bindings.find(actionName);
        if (it == m_bindings.end() || !isContextActive(it->second.context)) return false;
        auto kit = m_currKeys.find(it->second.keyCode);
        return kit != m_currKeys.end() && kit->second;
    }

    [[nodiscard]] static float shapeAnalogAxis(float rawValue, float deadZone, float sensitivity, bool invert) noexcept {
        const float dz = std::clamp(deadZone, 0.0f, 0.9f);
        const float mag = std::abs(rawValue);
        if (mag <= dz) return 0.0f;
        const float norm = std::clamp((mag - dz) / (1.0f - dz), 0.0f, 1.0f);
        float out = std::copysign(norm, rawValue) * std::clamp(sensitivity, 0.1f, 5.0f);
        if (invert) out = -out;
        return std::clamp(out, -1.0f, 1.0f);
    }

private:
    std::unordered_map<std::string, InputActionBinding> m_bindings;
    std::unordered_set<InputContext> m_activeContexts{InputContext::Gameplay};
    std::unordered_map<int32_t, bool> m_currKeys;
    std::unordered_map<int32_t, bool> m_prevKeys;
};

} // namespace sidera::runtime
