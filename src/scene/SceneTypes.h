#pragma once

#include "assets/AssetTypes.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace sidera {

using EntityID = uint64_t;
inline constexpr EntityID kInvalidEntityID = 0;

struct TransformComponent {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEulerDeg{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] glm::mat4 computeLocalMatrix() const noexcept {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position);
        m = glm::rotate(m, glm::radians(rotationEulerDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotationEulerDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotationEulerDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::scale(m, scale);
        return m;
    }
};

// Extensible component record: supports built-in and future components (Physics, Audio, AI, Quest)
// while preserving unknown components safely across save/load round trips.
struct ComponentRecord {
    std::string typeName;
    uint32_t version = 1;
    std::map<std::string, std::string> properties;
};

struct PrefabOverride {
    EntityID targetLocalEntityId = kInvalidEntityID;
    std::string componentType;
    std::string propertyKey;
    std::string value;
};

struct SceneEntity {
    EntityID id = kInvalidEntityID;
    std::string name;
    bool active = true;
    EntityID parentId = kInvalidEntityID;
    TransformComponent transform{};
    AssetGUID meshAssetGuid{};
    AssetGUID materialAssetGuid{};
    AssetGUID prefabSourceGuid{};
    std::vector<ComponentRecord> components;
    std::vector<PrefabOverride> prefabOverrides;
};

struct SceneValidationIssue {
    EntityID entityId = kInvalidEntityID;
    bool isError = true;
    std::string message;
};

} // namespace sidera
