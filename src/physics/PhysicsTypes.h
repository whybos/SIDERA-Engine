#pragma once

#include "scene/SceneTypes.h"

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sidera {

enum class RigidBodyType : uint32_t {
    Static = 0,
    Dynamic = 1,
    Kinematic = 2
};

enum class ColliderShapeType : uint32_t {
    Box = 0,
    Sphere = 1,
    Capsule = 2,
    Plane = 3
};

struct PhysicsMaterial {
    float staticFriction = 0.5f;
    float dynamicFriction = 0.4f;
    float restitution = 0.2f;

    void validate() noexcept {
        staticFriction = std::clamp(staticFriction, 0.0f, 2.0f);
        dynamicFriction = std::clamp(dynamicFriction, 0.0f, 2.0f);
        restitution = std::clamp(restitution, 0.0f, 1.0f);
    }
};

struct CollisionFilter {
    uint32_t layer = 0x00000001u;
    uint32_t mask  = 0xFFFFFFFFu;

    [[nodiscard]] bool matches(const CollisionFilter& other) const noexcept {
        return ((layer & other.mask) != 0u) && ((other.layer & mask) != 0u);
    }
};

struct ColliderDesc {
    ColliderShapeType shape = ColliderShapeType::Box;
    glm::vec3 halfExtents{0.5f, 0.5f, 0.5f}; // Box
    float radius = 0.5f;                     // Sphere / Capsule
    float height = 1.8f;                     // Capsule total height
    glm::vec3 planeNormal{0.0f, 1.0f, 0.0f}; // Plane
    float planeOffset = 0.0f;                // Plane (dot(N, p) == planeOffset)
    glm::vec3 localOffset{0.0f};
    bool isTrigger = false;
    CollisionFilter filter{};
    PhysicsMaterial material{};

    [[nodiscard]] bool isValid() const noexcept {
        if (shape == ColliderShapeType::Box) {
            return halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f;
        }
        if (shape == ColliderShapeType::Sphere) return radius > 0.0f;
        if (shape == ColliderShapeType::Capsule) return radius > 0.0f && height >= 2.0f * radius;
        if (shape == ColliderShapeType::Plane) return glm::length(planeNormal) > 1e-4f;
        return false;
    }
};

struct RigidBodyDesc {
    EntityID entityId = kInvalidEntityID;
    RigidBodyType type = RigidBodyType::Dynamic;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float mass = 1.0f;
    float linearDamping = 0.02f;
    bool useGravity = true;
    ColliderDesc collider{};

    void validate() noexcept {
        mass = std::max(mass, 0.001f);
        linearDamping = std::clamp(linearDamping, 0.0f, 0.99f);
        collider.material.validate();
        if (collider.shape == ColliderShapeType::Plane && glm::length(collider.planeNormal) > 1e-4f) {
            collider.planeNormal = glm::normalize(collider.planeNormal);
        }
    }
};

struct PhysicsSettings {
    bool enabled = true;
    bool paused = false;
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float fixedTimestep = 1.0f / 60.0f;
    uint32_t maxSubsteps = 4u;
    float maxFrameDelta = 0.1f;
    bool debugDrawEnabled = false;

    void validate() noexcept {
        fixedTimestep = std::clamp(fixedTimestep, 1.0f / 240.0f, 1.0f / 15.0f);
        maxSubsteps = std::clamp(maxSubsteps, 1u, 16u);
        maxFrameDelta = std::clamp(maxFrameDelta, fixedTimestep, 0.5f);
    }
};

struct RaycastHit {
    bool hit = false;
    EntityID entityId = kInvalidEntityID;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

enum class PhysicsEventType : uint32_t {
    CollisionEnter = 0,
    CollisionPersist = 1,
    CollisionExit = 2,
    TriggerEnter = 3,
    TriggerExit = 4
};

struct PhysicsEvent {
    PhysicsEventType type = PhysicsEventType::CollisionEnter;
    EntityID entityA = kInvalidEntityID;
    EntityID entityB = kInvalidEntityID;
    glm::vec3 contactPoint{0.0f};
    glm::vec3 contactNormal{0.0f, 1.0f, 0.0f};
};

struct PhysicsStatistics {
    uint32_t activeBodies = 0;
    uint32_t staticBodies = 0;
    uint32_t dynamicBodies = 0;
    uint32_t kinematicBodies = 0;
    uint32_t collisionPairs = 0;
    uint32_t queryCount = 0;
    uint32_t substepsExecuted = 0;
    double lastStepCpuMs = 0.0;
};

// Generic Engine-Level Interaction Foundation (game-agnostic)
struct InteractableRecord {
    EntityID entityId = kInvalidEntityID;
    glm::vec3 position{0.0f};
    float range = 3.0f;
    float priority = 1.0f;
    bool enabled = true;
};

} // namespace sidera
