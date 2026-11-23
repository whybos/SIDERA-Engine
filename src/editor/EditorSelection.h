#pragma once

#include "assets/AssetDatabase.h"
#include "scene/Scene.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace sidera::editor {

enum class SelectionKind : uint32_t {
    Entity = 0,
    Asset = 1
};

struct SelectionItem {
    SelectionKind kind = SelectionKind::Entity;
    EntityID entityId = kInvalidEntityID;
    AssetGUID assetGuid{};

    bool operator==(const SelectionItem& o) const noexcept {
        if (kind != o.kind) return false;
        return (kind == SelectionKind::Entity) ? (entityId == o.entityId) : (assetGuid == o.assetGuid);
    }
};

// Typed, handle-based Editor selection context (never stores raw pointers).
class EditorSelectionContext {
public:
    void select(const SelectionItem& item) {
        m_items.clear();
        m_items.push_back(item);
        notify();
    }

    void addToSelection(const SelectionItem& item) {
        if (!isSelected(item)) {
            m_items.push_back(item);
            notify();
        }
    }

    void toggleSelection(const SelectionItem& item) {
        auto it = std::find(m_items.begin(), m_items.end(), item);
        if (it != m_items.end()) m_items.erase(it);
        else m_items.push_back(item);
        notify();
    }

    void deselect(const SelectionItem& item) {
        auto it = std::find(m_items.begin(), m_items.end(), item);
        if (it != m_items.end()) {
            m_items.erase(it);
            notify();
        }
    }

    void clear() {
        if (!m_items.empty()) {
            m_items.clear();
            notify();
        }
    }

    [[nodiscard]] bool isSelected(const SelectionItem& item) const noexcept {
        return std::find(m_items.begin(), m_items.end(), item) != m_items.end();
    }

    [[nodiscard]] const std::vector<SelectionItem>& getSelection() const noexcept { return m_items; }
    [[nodiscard]] size_t count() const noexcept { return m_items.size(); }

    // Automatically purges stale EntityIDs or deleted AssetGUIDs
    void validate(const Scene* scene, const AssetDatabase* assetDb) {
        const size_t before = m_items.size();
        m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const SelectionItem& item) {
            if (item.kind == SelectionKind::Entity) {
                return !scene || scene->findEntity(item.entityId) == nullptr;
            }
            if (item.kind == SelectionKind::Asset) {
                return !assetDb || assetDb->findByGUID(item.assetGuid) == nullptr;
            }
            return true;
        }), m_items.end());
        if (m_items.size() != before) notify();
    }

    uint64_t subscribe(std::function<void()> cb) {
        const uint64_t token = m_nextToken++;
        m_listeners[token] = std::move(cb);
        return token;
    }

    void unsubscribe(uint64_t token) { m_listeners.erase(token); }

private:
    void notify() {
        for (const auto& [tok, cb] : m_listeners) {
            if (cb) cb();
        }
    }

    std::vector<SelectionItem> m_items;
    uint64_t m_nextToken = 1;
    std::unordered_map<uint64_t, std::function<void()>> m_listeners;
};

} // namespace sidera::editor
