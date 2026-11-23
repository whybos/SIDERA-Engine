#pragma once

#include "runtime/SaveAndLocalization.h"
#include "runtime/ScriptingAndEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::framework {

// ---- Optional Combat System ----

struct AbilityDef {
    std::string id;
    float baseDamage = 10.0f;
    std::string damageType = "Physical";
    float cooldownSeconds = 1.0f;
};

struct StatusEffect {
    std::string id;
    float remainingSeconds = 0.0f;
    float damagePerSecond = 0.0f;
};

struct CombatantState {
    float health = 100.0f;
    float maxHealth = 100.0f;
    std::unordered_map<std::string, float> resistances; // 0.0 = none, 0.5 = 50% reduction
    std::unordered_map<std::string, float> cooldowns;
    std::vector<StatusEffect> activeEffects;
};

// Optional modular combat system (can be omitted by non-combat adventure games).
class CombatSystem {
public:
    void registerAbility(AbilityDef def) {
        const std::string id = def.id;
        m_abilities[id] = std::move(def);
    }

    bool useAbility(CombatantState& attacker, CombatantState& target,
                    const std::string& abilityId, float& outDamageDealt) {
        auto it = m_abilities.find(abilityId);
        if (it == m_abilities.end()) return false;
        if (attacker.cooldowns[abilityId] > 0.0f) return false;

        const float res = std::clamp(target.resistances[it->second.damageType], 0.0f, 0.95f);
        outDamageDealt = it->second.baseDamage * (1.0f - res);
        target.health = std::max(0.0f, target.health - outDamageDealt);
        attacker.cooldowns[abilityId] = it->second.cooldownSeconds;
        return true;
    }

    void applyStatusEffect(CombatantState& target, StatusEffect eff) {
        target.activeEffects.push_back(std::move(eff));
    }

    void update(CombatantState& c, float dt) {
        for (auto& [id, cd] : c.cooldowns) {
            cd = std::max(0.0f, cd - dt);
        }
        for (auto& eff : c.activeEffects) {
            const float step = std::min(dt, eff.remainingSeconds);
            c.health = std::max(0.0f, c.health - eff.damagePerSecond * step);
            eff.remainingSeconds -= dt;
        }
        c.activeEffects.erase(
            std::remove_if(c.activeEffects.begin(), c.activeEffects.end(),
                           [](const StatusEffect& e) { return e.remainingSeconds <= 0.0f; }),
            c.activeEffects.end());
    }

private:
    std::unordered_map<std::string, AbilityDef> m_abilities;
};

// ---- World Time & Scheduled World Events ----

enum class DayPhase : uint8_t {
    Dawn = 0,
    Day,
    Dusk,
    Night
};

struct ScheduledWorldEvent {
    std::string id;
    float triggerHour = 12.0f;
    std::string eventType;
    std::string payload;
    bool repeatingDaily = false;
    uint32_t lastTriggeredDay = 0;
};

class WorldTimeSystem {
public:
    void setTime(uint32_t day, float hour) noexcept {
        m_day = std::max<uint32_t>(1, day);
        m_hour = std::fmod(std::max(0.0f, hour), 24.0f);
    }

    void setTimeScale(float hoursPerRealSecond) noexcept { m_hoursPerRealSecond = hoursPerRealSecond; }

    void scheduleEvent(ScheduledWorldEvent ev) { m_events.push_back(std::move(ev)); }

    void update(float dt, sidera::runtime::RuntimeEventBus* eventBus = nullptr) {
        const float prevHour = m_hour;
        m_hour += dt * m_hoursPerRealSecond;
        while (m_hour >= 24.0f) {
            m_hour -= 24.0f;
            ++m_day;
        }

        for (auto& ev : m_events) {
            const bool alreadyRanToday = (ev.lastTriggeredDay == m_day);
            const bool alreadyRanEver  = (!ev.repeatingDaily && ev.lastTriggeredDay > 0);
            if (alreadyRanToday || alreadyRanEver) continue;

            if (m_hour >= ev.triggerHour && (prevHour <= ev.triggerHour || m_hour < prevHour)) {
                ev.lastTriggeredDay = m_day;
                if (eventBus) {
                    eventBus->publish({ev.eventType, kInvalidEntityID, ev.payload});
                }
            }
        }
    }

    [[nodiscard]] uint32_t getDay() const noexcept { return m_day; }
    [[nodiscard]] float getHour() const noexcept { return m_hour; }

    [[nodiscard]] DayPhase getDayPhase() const noexcept {
        if (m_hour >= 5.0f && m_hour < 8.0f) return DayPhase::Dawn;
        if (m_hour >= 8.0f && m_hour < 18.0f) return DayPhase::Day;
        if (m_hour >= 18.0f && m_hour < 21.0f) return DayPhase::Dusk;
        return DayPhase::Night;
    }

    void saveTo(sidera::runtime::SaveGameData& save) const {
        save.variables["world.day"]  = std::to_string(m_day);
        save.variables["world.hour"] = std::to_string(m_hour);
    }

    void loadFrom(const sidera::runtime::SaveGameData& save) {
        auto dit = save.variables.find("world.day");
        auto hit = save.variables.find("world.hour");
        m_day  = (dit != save.variables.end()) ? static_cast<uint32_t>(std::stoul(dit->second)) : 1;
        m_hour = (hit != save.variables.end()) ? std::stof(hit->second) : 8.0f;
    }

private:
    uint32_t m_day = 1;
    float m_hour = 8.0f;
    float m_hoursPerRealSecond = 1.0f;
    std::vector<ScheduledWorldEvent> m_events;
};

} // namespace sidera::framework
