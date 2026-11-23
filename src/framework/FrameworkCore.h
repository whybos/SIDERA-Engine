#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sidera::framework {

struct FrameworkModuleDescriptor {
    std::string id;
    std::string displayName;
    std::vector<std::string> dependencies;
};

// Base interface for optional, modular gameplay framework subsystems.
// Engine Core never depends on IGameFrameworkModule.
class IGameFrameworkModule {
public:
    virtual ~IGameFrameworkModule() = default;

    [[nodiscard]] virtual FrameworkModuleDescriptor getDescriptor() const = 0;
    virtual bool onInitialize() { return true; }
    virtual void onUpdate(float /*dt*/) {}
    virtual void onShutdown() {}
};

// Manages optional gameplay framework modules with duplicate rejection,
// dependency validation, cycle detection, topological init, reverse shutdown,
// and per-module failure isolation.
class FrameworkRegistry {
public:
    bool registerModule(std::unique_ptr<IGameFrameworkModule> mod) {
        if (!mod) return false;
        const auto desc = mod->getDescriptor();
        if (desc.id.empty() || m_modules.count(desc.id) > 0) return false;
        m_descriptors[desc.id] = desc;
        m_modules[desc.id] = std::move(mod);
        m_registrationOrder.push_back(desc.id);
        return true;
    }

    [[nodiscard]] bool hasModule(const std::string& id) const {
        return m_modules.count(id) > 0;
    }

    [[nodiscard]] IGameFrameworkModule* getModule(const std::string& id) const {
        auto it = m_modules.find(id);
        return (it != m_modules.end()) ? it->second.get() : nullptr;
    }

    // Validates dependencies and detects cycles via DFS.
    [[nodiscard]] bool validateDependencies(std::string* outError = nullptr) const {
        for (const auto& [id, desc] : m_descriptors) {
            for (const auto& dep : desc.dependencies) {
                if (m_modules.count(dep) == 0) {
                    if (outError) *outError = "Module '" + id + "' missing dependency '" + dep + "'";
                    return false;
                }
            }
        }
        // Cycle detection (0 = unvisited, 1 = visiting, 2 = visited)
        std::unordered_map<std::string, uint8_t> state;
        std::function<bool(const std::string&)> dfs = [&](const std::string& id) -> bool {
            state[id] = 1;
            for (const auto& dep : m_descriptors.at(id).dependencies) {
                if (state[dep] == 1) return false; // cycle
                if (state[dep] == 0 && !dfs(dep)) return false;
            }
            state[id] = 2;
            return true;
        };
        for (const auto& id : m_registrationOrder) {
            if (state[id] == 0 && !dfs(id)) {
                if (outError) *outError = "Cyclic dependency detected involving module '" + id + "'";
                return false;
            }
        }
        return true;
    }

    bool initializeAll(std::string* outError = nullptr) {
        if (!validateDependencies(outError)) return false;
        m_initOrder.clear();

        // Topological sort (dependencies first)
        std::unordered_set<std::string> visited;
        std::function<void(const std::string&)> topo = [&](const std::string& id) {
            if (visited.count(id) > 0) return;
            visited.insert(id);
            for (const auto& dep : m_descriptors.at(id).dependencies) {
                topo(dep);
            }
            m_initOrder.push_back(id);
        };
        for (const auto& id : m_registrationOrder) topo(id);

        std::vector<std::string> initialized;
        for (const auto& id : m_initOrder) {
            bool ok = false;
            try {
                ok = m_modules.at(id)->onInitialize();
            } catch (...) {
                ++m_failureCount;
                ok = false;
            }
            if (!ok) {
                if (outError) *outError = "Module '" + id + "' failed to initialize";
                // Roll back already initialized modules in reverse order
                for (auto it = initialized.rbegin(); it != initialized.rend(); ++it) {
                    try { m_modules.at(*it)->onShutdown(); } catch (...) { ++m_failureCount; }
                }
                m_initOrder.clear();
                m_initialized = false;
                return false;
            }
            initialized.push_back(id);
        }
        m_initialized = true;
        return true;
    }

    void updateAll(float dt) {
        if (!m_initialized) return;
        for (const auto& id : m_initOrder) {
            try {
                m_modules.at(id)->onUpdate(dt);
            } catch (...) {
                ++m_failureCount; // isolate module failure
            }
        }
    }

    void shutdownAll() {
        if (!m_initialized) return;
        for (auto it = m_initOrder.rbegin(); it != m_initOrder.rend(); ++it) {
            try {
                m_modules.at(*it)->onShutdown();
            } catch (...) {
                ++m_failureCount;
            }
        }
        m_initialized = false;
        m_initOrder.clear();
    }

    [[nodiscard]] const std::vector<std::string>& getInitOrder() const noexcept { return m_initOrder; }
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] uint32_t getFailureCount() const noexcept { return m_failureCount; }
    [[nodiscard]] size_t getModuleCount() const noexcept { return m_modules.size(); }

private:
    std::unordered_map<std::string, std::unique_ptr<IGameFrameworkModule>> m_modules;
    std::unordered_map<std::string, FrameworkModuleDescriptor> m_descriptors;
    std::vector<std::string> m_registrationOrder;
    std::vector<std::string> m_initOrder;
    bool m_initialized = false;
    uint32_t m_failureCount = 0;
};

} // namespace sidera::framework
