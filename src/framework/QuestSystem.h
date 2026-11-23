#pragma once

#include "runtime/SaveAndLocalization.h"
#include "runtime/ScriptingAndEvents.h"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::framework {

enum class QuestState : uint8_t {
    Locked = 0,
    Available,
    Active,
    Completed,
    Failed,
    Abandoned
};

struct QuestObjectiveDef {
    std::string id;
    std::string descriptionKey;
    std::string triggerEventType;
    std::string triggerPayload; // empty = match any payload
    uint32_t requiredCount = 1;
    bool optional = false;
};

struct QuestDefinition {
    std::string id;
    std::string titleKey;
    std::vector<std::string> prerequisiteQuestIds;
    std::vector<QuestObjectiveDef> objectives;
    bool sequentialObjectives = false;
    std::string rewardEventType;
    std::string rewardEventPayload;
};

struct QuestObjectiveProgress {
    uint32_t currentCount = 0;
    bool completed = false;
};

struct QuestInstance {
    std::string questId;
    QuestState state = QuestState::Available;
    std::map<std::string, QuestObjectiveProgress> objectives;
};

// Data-driven Quest & Objective Manager with prerequisites, sequential/parallel
// objectives, RuntimeEventBus integration, and SaveGameData persistence.
class QuestManager {
public:
    void registerQuest(QuestDefinition def) {
        const std::string id = def.id;
        m_defs[id] = std::move(def);
    }

    [[nodiscard]] bool arePrerequisitesMet(const std::string& questId) const {
        auto it = m_defs.find(questId);
        if (it == m_defs.end()) return false;
        for (const auto& prereq : it->second.prerequisiteQuestIds) {
            auto pit = m_instances.find(prereq);
            if (pit == m_instances.end() || pit->second.state != QuestState::Completed) return false;
        }
        return true;
    }

    bool activateQuest(const std::string& questId,
                       sidera::runtime::RuntimeEventBus* eventBus = nullptr) {
        auto dit = m_defs.find(questId);
        if (dit == m_defs.end() || !arePrerequisitesMet(questId)) return false;

        QuestInstance inst{};
        inst.questId = questId;
        inst.state = QuestState::Active;
        for (const auto& obj : dit->second.objectives) {
            inst.objectives[obj.id] = {};
        }
        m_instances[questId] = std::move(inst);
        if (eventBus) eventBus->publish({"quest.activated", kInvalidEntityID, questId});
        return true;
    }

    void onRuntimeEvent(const sidera::runtime::RuntimeEvent& ev,
                        sidera::runtime::RuntimeEventBus* eventBus = nullptr) {
        for (auto& [qid, inst] : m_instances) {
            if (inst.state != QuestState::Active) continue;
            const auto& def = m_defs.at(qid);
            for (const auto& obj : def.objectives) {
                auto& prog = inst.objectives[obj.id];
                if (prog.completed) continue;
                if (obj.triggerEventType == ev.type &&
                    (obj.triggerPayload.empty() || obj.triggerPayload == ev.payload)) {
                    prog.currentCount++;
                    if (prog.currentCount >= obj.requiredCount) {
                        prog.completed = true;
                        if (eventBus) {
                            eventBus->publish({"quest.objective_completed", ev.sourceEntity, qid + ":" + obj.id});
                        }
                    }
                }
                if (def.sequentialObjectives && !prog.completed) break;
            }
            // Check completion of all non-optional objectives
            bool allDone = true;
            for (const auto& obj : def.objectives) {
                if (!obj.optional && !inst.objectives[obj.id].completed) {
                    allDone = false;
                    break;
                }
            }
            if (allDone) {
                inst.state = QuestState::Completed;
                if (eventBus) {
                    eventBus->publish({"quest.completed", kInvalidEntityID, qid});
                    if (!def.rewardEventType.empty()) {
                        eventBus->publish({def.rewardEventType, kInvalidEntityID, def.rewardEventPayload});
                    }
                }
            }
        }
    }

    bool failQuest(const std::string& questId) {
        auto it = m_instances.find(questId);
        if (it == m_instances.end() || it->second.state != QuestState::Active) return false;
        it->second.state = QuestState::Failed;
        return true;
    }

    [[nodiscard]] QuestState getQuestState(const std::string& questId) const {
        auto it = m_instances.find(questId);
        if (it != m_instances.end()) return it->second.state;
        return arePrerequisitesMet(questId) ? QuestState::Available : QuestState::Locked;
    }

    [[nodiscard]] const QuestInstance* getInstance(const std::string& questId) const {
        auto it = m_instances.find(questId);
        return (it != m_instances.end()) ? &it->second : nullptr;
    }

    void saveTo(sidera::runtime::SaveGameData& save) const {
        for (const auto& [qid, inst] : m_instances) {
            save.variables["quest." + qid + ".state"] = std::to_string(static_cast<uint32_t>(inst.state));
            for (const auto& [oid, prog] : inst.objectives) {
                save.variables["quest." + qid + ".obj." + oid] =
                    std::to_string(prog.currentCount) + ":" + (prog.completed ? "1" : "0");
            }
        }
    }

    void loadFrom(const sidera::runtime::SaveGameData& save) {
        m_instances.clear();
        for (const auto& [qid, def] : m_defs) {
            auto sit = save.variables.find("quest." + qid + ".state");
            if (sit == save.variables.end()) continue;
            QuestInstance inst{};
            inst.questId = qid;
            inst.state = static_cast<QuestState>(std::stoul(sit->second));
            for (const auto& obj : def.objectives) {
                QuestObjectiveProgress prog{};
                auto oit = save.variables.find("quest." + qid + ".obj." + obj.id);
                if (oit != save.variables.end()) {
                    const auto sep = oit->second.find(':');
                    if (sep != std::string::npos) {
                        prog.currentCount = static_cast<uint32_t>(std::stoul(oit->second.substr(0, sep)));
                        prog.completed = (oit->second.substr(sep + 1) == "1");
                    }
                }
                inst.objectives[obj.id] = prog;
            }
            m_instances[qid] = std::move(inst);
        }
    }

    void clearInstances() noexcept { m_instances.clear(); }

private:
    std::unordered_map<std::string, QuestDefinition> m_defs;
    std::map<std::string, QuestInstance> m_instances;
};

} // namespace sidera::framework
