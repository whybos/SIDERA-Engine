#pragma once

#include "scene/SceneTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::runtime {

struct RuntimeEvent {
    std::string type;
    EntityID sourceEntity = kInvalidEntityID;
    std::string payload;
};

// Generic Engine-level Runtime Event Bus (synchronous & deferred queued dispatch).
class RuntimeEventBus {
public:
    uint64_t subscribe(const std::string& eventType, std::function<void(const RuntimeEvent&)> cb) {
        const uint64_t token = m_nextToken++;
        m_subs[eventType][token] = std::move(cb);
        return token;
    }

    void unsubscribe(const std::string& eventType, uint64_t token) {
        auto it = m_subs.find(eventType);
        if (it != m_subs.end()) it->second.erase(token);
    }

    void publish(const RuntimeEvent& ev) {
        auto it = m_subs.find(ev.type);
        if (it == m_subs.end()) return;
        const auto copy = it->second; // safe against unsubscription during callback
        for (const auto& [tok, cb] : copy) {
            if (cb) cb(ev);
        }
    }

    void enqueue(const RuntimeEvent& ev) {
        if (m_queue.size() < 4096u) m_queue.push_back(ev);
    }

    void dispatchQueued() {
        auto pending = std::move(m_queue);
        m_queue.clear();
        for (const auto& ev : pending) publish(ev);
    }

    void clear() noexcept { m_subs.clear(); m_queue.clear(); }

private:
    uint64_t m_nextToken = 1;
    std::unordered_map<std::string, std::unordered_map<uint64_t, std::function<void(const RuntimeEvent&)>>> m_subs;
    std::vector<RuntimeEvent> m_queue;
};

// Native C++ Gameplay Extension Interface (failure-isolated per EntityID).
class IScriptBehavior {
public:
    virtual ~IScriptBehavior() = default;
    virtual void onInit(EntityID /*entityId*/) {}
    virtual void onUpdate(EntityID /*entityId*/, float /*dt*/) {}
    virtual void onShutdown(EntityID /*entityId*/) {}
};

class ScriptRuntime {
public:
    void attachScript(EntityID entityId, std::unique_ptr<IScriptBehavior> script) {
        if (entityId == kInvalidEntityID || !script) return;
        try {
            script->onInit(entityId);
            m_scripts[entityId] = std::move(script);
        } catch (...) {
            ++m_failureCount;
        }
    }

    void detachScript(EntityID entityId) {
        auto it = m_scripts.find(entityId);
        if (it == m_scripts.end()) return;
        try { it->second->onShutdown(entityId); } catch (...) { ++m_failureCount; }
        m_scripts.erase(it);
    }

    void updateAll(float dt) {
        for (auto& [id, s] : m_scripts) {
            try {
                s->onUpdate(id, dt);
            } catch (...) {
                ++m_failureCount; // isolate single script failure
            }
        }
    }

    void shutdownAll() {
        for (auto& [id, s] : m_scripts) {
            try { s->onShutdown(id); } catch (...) {}
        }
        m_scripts.clear();
    }

    [[nodiscard]] uint32_t getFailureCount() const noexcept { return m_failureCount; }
    [[nodiscard]] size_t getScriptCount() const noexcept { return m_scripts.size(); }

private:
    std::unordered_map<EntityID, std::unique_ptr<IScriptBehavior>> m_scripts;
    uint32_t m_failureCount = 0;
};

} // namespace sidera::runtime
