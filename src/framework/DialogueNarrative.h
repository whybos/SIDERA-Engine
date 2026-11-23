#pragma once

#include "runtime/SaveAndLocalization.h"
#include "runtime/ScriptingAndEvents.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::framework {

// Persistent narrative state variables (flags/values) with SaveGameData hooks.
class NarrativeState {
public:
    void setFlag(const std::string& key, bool value) { m_vars[key] = value ? "1" : "0"; }
    [[nodiscard]] bool getFlag(const std::string& key) const {
        auto it = m_vars.find(key);
        return (it != m_vars.end()) && (it->second == "1" || it->second == "true");
    }

    void setValue(const std::string& key, std::string value) { m_vars[key] = std::move(value); }
    [[nodiscard]] std::string getValue(const std::string& key, const std::string& def = "") const {
        auto it = m_vars.find(key);
        return (it != m_vars.end()) ? it->second : def;
    }

    void saveTo(sidera::runtime::SaveGameData& save) const {
        for (const auto& [k, v] : m_vars) {
            save.variables["narrative." + k] = v;
        }
    }

    void loadFrom(const sidera::runtime::SaveGameData& save) {
        m_vars.clear();
        constexpr const char* kPrefix = "narrative.";
        const size_t prefixLen = 10;
        for (const auto& [k, v] : save.variables) {
            if (k.compare(0, prefixLen, kPrefix) == 0) {
                m_vars[k.substr(prefixLen)] = v;
            }
        }
    }

    void clear() noexcept { m_vars.clear(); }

private:
    std::map<std::string, std::string> m_vars;
};

struct DialogueCondition {
    std::string variableKey;
    std::string expectedValue = "1";
    bool negate = false;

    [[nodiscard]] bool evaluate(const NarrativeState& state) const {
        const bool match = (state.getValue(variableKey, "0") == expectedValue);
        return negate ? !match : match;
    }
};

struct DialogueChoice {
    std::string id;
    std::string textKey;        // localization key
    std::string nextNodeId;     // empty ends dialogue
    std::vector<DialogueCondition> conditions;
    std::string setVariableKey; // optional narrative mutation
    std::string setVariableValue = "1";
    std::string emitEventType;  // optional RuntimeEventBus event
    std::string emitEventPayload;
};

struct DialogueNode {
    std::string id;
    std::string speakerKey;     // localization key
    std::string bodyKey;        // localization key
    std::vector<DialogueChoice> choices;
};

struct DialogueGraph {
    std::string id;
    std::string entryNodeId;
    std::unordered_map<std::string, DialogueNode> nodes;
};

struct DialogueSession {
    std::string graphId;
    std::string currentNodeId;
    bool active = false;
};

// Data-driven Dialogue & Narrative System integrating LocalizationManager and RuntimeEventBus.
class DialogueSystem {
public:
    void registerGraph(DialogueGraph graph) {
        const std::string id = graph.id;
        m_graphs[id] = std::move(graph);
    }

    bool startDialogue(const std::string& graphId,
                       sidera::runtime::RuntimeEventBus* eventBus = nullptr) {
        auto it = m_graphs.find(graphId);
        if (it == m_graphs.end() || it->second.nodes.count(it->second.entryNodeId) == 0) return false;
        m_session.graphId = graphId;
        m_session.currentNodeId = it->second.entryNodeId;
        m_session.active = true;
        if (eventBus) {
            eventBus->publish({"dialogue.started", kInvalidEntityID, graphId});
        }
        return true;
    }

    [[nodiscard]] bool isActive() const noexcept { return m_session.active; }

    [[nodiscard]] const DialogueNode* getCurrentNode() const {
        if (!m_session.active) return nullptr;
        auto git = m_graphs.find(m_session.graphId);
        if (git == m_graphs.end()) return nullptr;
        auto nit = git->second.nodes.find(m_session.currentNodeId);
        return (nit != git->second.nodes.end()) ? &nit->second : nullptr;
    }

    [[nodiscard]] std::string getLocalizedBody(const sidera::runtime::LocalizationManager& loc) const {
        const auto* node = getCurrentNode();
        return node ? loc.lookup(node->bodyKey) : "";
    }

    [[nodiscard]] std::vector<DialogueChoice> getAvailableChoices(const NarrativeState& state) const {
        std::vector<DialogueChoice> out;
        const auto* node = getCurrentNode();
        if (!node) return out;
        for (const auto& c : node->choices) {
            bool ok = true;
            for (const auto& cond : c.conditions) {
                if (!cond.evaluate(state)) { ok = false; break; }
            }
            if (ok) out.push_back(c);
        }
        return out;
    }

    bool selectChoice(const std::string& choiceId,
                      NarrativeState& state,
                      sidera::runtime::RuntimeEventBus* eventBus = nullptr) {
        if (!m_session.active) return false;
        const auto avail = getAvailableChoices(state);
        for (const auto& c : avail) {
            if (c.id == choiceId) {
                if (!c.setVariableKey.empty()) {
                    state.setValue(c.setVariableKey, c.setVariableValue);
                }
                if (!c.emitEventType.empty() && eventBus) {
                    eventBus->publish({c.emitEventType, kInvalidEntityID, c.emitEventPayload});
                }
                if (c.nextNodeId.empty()) {
                    m_session.active = false;
                    if (eventBus) eventBus->publish({"dialogue.ended", kInvalidEntityID, m_session.graphId});
                } else {
                    m_session.currentNodeId = c.nextNodeId;
                }
                return true;
            }
        }
        return false;
    }

private:
    std::unordered_map<std::string, DialogueGraph> m_graphs;
    DialogueSession m_session{};
};

} // namespace sidera::framework
