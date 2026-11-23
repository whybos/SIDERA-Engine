#pragma once

#include "runtime/SaveAndLocalization.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace sidera::framework {

// ---- Attributes & Modifiers ----

enum class ModifierOp : uint8_t {
    Flat = 0,
    AdditivePercent,
    Multiplicative
};

struct AttributeModifier {
    std::string sourceId;
    std::string attributeId;
    ModifierOp op = ModifierOp::Flat;
    float value = 0.0f;
};

struct AttributeDef {
    float baseValue = 0.0f;
    float minValue  = -1e9f;
    float maxValue  = 1e9f;
};

class AttributeSet {
public:
    void defineAttribute(const std::string& id, float baseValue, float minVal = -1e9f, float maxVal = 1e9f) {
        m_defs[id] = {baseValue, minVal, maxVal};
    }

    void addModifier(AttributeModifier mod) { m_modifiers.push_back(std::move(mod)); }

    void removeModifiersBySource(const std::string& sourceId) {
        m_modifiers.erase(
            std::remove_if(m_modifiers.begin(), m_modifiers.end(),
                           [&](const AttributeModifier& m) { return m.sourceId == sourceId; }),
            m_modifiers.end());
    }

    [[nodiscard]] float getValue(const std::string& id) const {
        auto it = m_defs.find(id);
        if (it == m_defs.end()) return 0.0f;
        float flat = 0.0f;
        float addPct = 0.0f;
        float mult = 1.0f;
        for (const auto& m : m_modifiers) {
            if (m.attributeId != id) continue;
            if (m.op == ModifierOp::Flat) flat += m.value;
            else if (m.op == ModifierOp::AdditivePercent) addPct += m.value;
            else if (m.op == ModifierOp::Multiplicative) mult *= m.value;
        }
        const float val = (it->second.baseValue + flat) * (1.0f + addPct) * mult;
        return std::clamp(val, it->second.minValue, it->second.maxValue);
    }

private:
    std::map<std::string, AttributeDef> m_defs;
    std::vector<AttributeModifier> m_modifiers;
};

// ---- Inventory & Equipment ----

struct ItemDefinition {
    std::string id;
    std::string nameKey;
    uint32_t maxStack = 1;
    std::string equipSlot; // empty if not equippable
    std::vector<AttributeModifier> equipModifiers;
};

struct ItemInstance {
    std::string itemId;
    uint32_t quantity = 0;
};

class ItemRegistry {
public:
    void registerItem(ItemDefinition def) {
        const std::string id = def.id;
        m_defs[id] = std::move(def);
    }
    [[nodiscard]] const ItemDefinition* find(const std::string& id) const {
        auto it = m_defs.find(id);
        return (it != m_defs.end()) ? &it->second : nullptr;
    }
private:
    std::unordered_map<std::string, ItemDefinition> m_defs;
};

// Transactional slot/stack InventoryContainer.
class InventoryContainer {
public:
    explicit InventoryContainer(size_t capacity = 24) : m_slots(capacity) {}

    [[nodiscard]] size_t getCapacity() const noexcept { return m_slots.size(); }

    bool addItem(const ItemRegistry& reg, const std::string& itemId, uint32_t count) {
        if (count == 0) return true;
        const auto* def = reg.find(itemId);
        if (!def || def->maxStack == 0) return false;

        // Transactional copy
        auto copy = m_slots;
        uint32_t remaining = count;

        // 1. Fill existing stacks
        for (auto& s : copy) {
            if (remaining == 0) break;
            if (s.itemId == itemId && s.quantity < def->maxStack) {
                const uint32_t take = std::min(remaining, def->maxStack - s.quantity);
                s.quantity += take;
                remaining -= take;
            }
        }
        // 2. Fill empty slots
        for (auto& s : copy) {
            if (remaining == 0) break;
            if (s.quantity == 0) {
                const uint32_t take = std::min(remaining, def->maxStack);
                s.itemId = itemId;
                s.quantity = take;
                remaining -= take;
            }
        }
        if (remaining > 0) return false; // transactional rollback
        m_slots = std::move(copy);
        return true;
    }

    bool removeItem(const std::string& itemId, uint32_t count) {
        if (count == 0) return true;
        if (getItemCount(itemId) < count) return false;
        uint32_t remaining = count;
        for (auto& s : m_slots) {
            if (remaining == 0) break;
            if (s.itemId == itemId && s.quantity > 0) {
                const uint32_t take = std::min(remaining, s.quantity);
                s.quantity -= take;
                if (s.quantity == 0) s.itemId.clear();
                remaining -= take;
            }
        }
        return true;
    }

    bool splitStack(size_t srcSlot, size_t dstSlot, uint32_t amount) {
        if (srcSlot >= m_slots.size() || dstSlot >= m_slots.size() || srcSlot == dstSlot) return false;
        if (amount == 0 || m_slots[srcSlot].quantity <= amount || m_slots[dstSlot].quantity != 0) return false;
        m_slots[dstSlot].itemId = m_slots[srcSlot].itemId;
        m_slots[dstSlot].quantity = amount;
        m_slots[srcSlot].quantity -= amount;
        return true;
    }

    [[nodiscard]] uint32_t getItemCount(const std::string& itemId) const {
        uint32_t total = 0;
        for (const auto& s : m_slots) {
            if (s.itemId == itemId) total += s.quantity;
        }
        return total;
    }

    [[nodiscard]] const ItemInstance& getSlot(size_t idx) const { return m_slots.at(idx); }

    void saveTo(sidera::runtime::SaveGameData& save) const {
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].quantity > 0) {
                save.variables["inv.slot." + std::to_string(i)] =
                    m_slots[i].itemId + ":" + std::to_string(m_slots[i].quantity);
            }
        }
    }

    void loadFrom(const sidera::runtime::SaveGameData& save) {
        for (auto& s : m_slots) { s.itemId.clear(); s.quantity = 0; }
        for (size_t i = 0; i < m_slots.size(); ++i) {
            auto it = save.variables.find("inv.slot." + std::to_string(i));
            if (it == save.variables.end()) continue;
            const auto sep = it->second.find(':');
            if (sep != std::string::npos) {
                m_slots[i].itemId = it->second.substr(0, sep);
                m_slots[i].quantity = static_cast<uint32_t>(std::stoul(it->second.substr(sep + 1)));
            }
        }
    }

    void clear() { for (auto& s : m_slots) { s.itemId.clear(); s.quantity = 0; } }

private:
    std::vector<ItemInstance> m_slots;
};

class EquipmentManager {
public:
    bool equip(const ItemRegistry& reg, const std::string& itemId, AttributeSet& attrs) {
        const auto* def = reg.find(itemId);
        if (!def || def->equipSlot.empty()) return false;
        unequip(def->equipSlot, attrs);
        m_equipped[def->equipSlot] = itemId;
        for (auto mod : def->equipModifiers) {
            mod.sourceId = "equip:" + def->equipSlot;
            attrs.addModifier(std::move(mod));
        }
        return true;
    }

    bool unequip(const std::string& slot, AttributeSet& attrs) {
        auto it = m_equipped.find(slot);
        if (it == m_equipped.end()) return false;
        attrs.removeModifiersBySource("equip:" + slot);
        m_equipped.erase(it);
        return true;
    }

    [[nodiscard]] std::string getEquippedItem(const std::string& slot) const {
        auto it = m_equipped.find(slot);
        return (it != m_equipped.end()) ? it->second : "";
    }

    void saveTo(sidera::runtime::SaveGameData& save) const {
        for (const auto& [slot, itemId] : m_equipped) {
            save.variables["equip." + slot] = itemId;
        }
    }

    void loadFrom(const sidera::runtime::SaveGameData& save, const ItemRegistry& reg, AttributeSet& attrs) {
        // Clear existing
        std::vector<std::string> slots;
        for (const auto& [s, _] : m_equipped) slots.push_back(s);
        for (const auto& s : slots) unequip(s, attrs);

        constexpr const char* kPrefix = "equip.";
        for (const auto& [k, v] : save.variables) {
            if (k.compare(0, 6, kPrefix) == 0 && !v.empty()) {
                equip(reg, v, attrs);
            }
        }
    }

private:
    std::map<std::string, std::string> m_equipped;
};

// ---- Progression System ----

class ProgressionSystem {
public:
    void setXpThresholds(std::vector<uint32_t> thresholds) { m_thresholds = std::move(thresholds); }

    uint32_t addXp(uint32_t amount) {
        m_xp += amount;
        uint32_t levelsGained = 0;
        while (m_level < m_thresholds.size() && m_xp >= m_thresholds[m_level]) {
            ++m_level;
            ++levelsGained;
        }
        return levelsGained;
    }

    [[nodiscard]] uint32_t getLevel() const noexcept { return m_level; }
    [[nodiscard]] uint32_t getXp() const noexcept { return m_xp; }

    void saveTo(sidera::runtime::SaveGameData& save) const {
        save.variables["prog.level"] = std::to_string(m_level);
        save.variables["prog.xp"]    = std::to_string(m_xp);
    }

    void loadFrom(const sidera::runtime::SaveGameData& save) {
        auto lit = save.variables.find("prog.level");
        auto xit = save.variables.find("prog.xp");
        m_level = (lit != save.variables.end()) ? static_cast<uint32_t>(std::stoul(lit->second)) : 1;
        m_xp    = (xit != save.variables.end()) ? static_cast<uint32_t>(std::stoul(xit->second)) : 0;
    }

private:
    uint32_t m_level = 1;
    uint32_t m_xp = 0;
    std::vector<uint32_t> m_thresholds{0, 100, 250, 500, 1000};
};

} // namespace sidera::framework
