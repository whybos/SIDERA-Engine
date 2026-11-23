#pragma once

#include "physics/PhysicsTypes.h"

#include <cstdint>
#include <functional>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sidera {

struct RigidBodyState {
    RigidBodyDesc desc{};
    glm::vec3 prevPosition{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 accumulatedForce{0.0f};
    bool active = true;
};

// Fixed-timestep 3D Physics World with rigid bodies, collision filtering,
// deferred collision/trigger events, and raycast/overlap queries (Milestone 37).
class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld() = default;

    void setSettings(const PhysicsSettings& s) noexcept { m_settings = s; m_settings.validate(); }
    [[nodiscard]] const PhysicsSettings& getSettings() const noexcept { return m_settings; }

    // Body lifecycle (keyed by stable Scene EntityID)
    bool addBody(const RigidBodyDesc& desc);
    bool removeBody(EntityID entityId);
    [[nodiscard]] RigidBodyState* findBody(EntityID entityId) noexcept;
    [[nodiscard]] const RigidBodyState* findBody(EntityID entityId) const noexcept;
    void clear() noexcept;

    void applyForce(EntityID entityId, const glm::vec3& force) noexcept;
    void applyImpulse(EntityID entityId, const glm::vec3& impulse) noexcept;
    void setKinematicTarget(EntityID entityId, const glm::vec3& targetPos) noexcept;

    // Fixed-timestep simulation with accumulator & substep clamp
    uint32_t step(float deltaTime);
    void stepFixed(float dt);

    // Deferred event dispatch (safe against entity destruction during callbacks)
    void setEventCallback(std::function<void(const PhysicsEvent&)> cb) { m_eventCallback = std::move(cb); }
    [[nodiscard]] const std::vector<PhysicsEvent>& getLastEvents() const noexcept { return m_lastEvents; }

    // Physics queries
    [[nodiscard]] RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                     float maxDistance, const CollisionFilter& filter = {},
                                     bool includeTriggers = false) const;
    [[nodiscard]] std::vector<EntityID> overlapSphere(const glm::vec3& center, float radius,
                                                      const CollisionFilter& filter = {}) const;

    [[nodiscard]] const PhysicsStatistics& getStats() const noexcept { return m_stats; }
    [[nodiscard]] float getInterpolationAlpha() const noexcept {
        return (m_settings.fixedTimestep > 0.0f) ? (m_accumulator / m_settings.fixedTimestep) : 0.0f;
    }

private:
    using ContactPair = std::pair<EntityID, EntityID>;

    void resolveCollisions(std::set<ContactPair>& currContacts, std::set<ContactPair>& currTriggers);
    static ContactPair makePair(EntityID a, EntityID b) noexcept {
        return (a < b) ? ContactPair{a, b} : ContactPair{b, a};
    }

    PhysicsSettings m_settings{};
    PhysicsStatistics m_stats{};
    mutable uint32_t m_queryCounter = 0;
    float m_accumulator = 0.0f;

    std::unordered_map<EntityID, RigidBodyState> m_bodies;
    std::set<ContactPair> m_prevContacts;
    std::set<ContactPair> m_prevTriggers;
    std::vector<PhysicsEvent> m_lastEvents;
    std::function<void(const PhysicsEvent&)> m_eventCallback;
};

// Reusable Capsule-based 3D Character Controller (ground, slope, step, wall-slide, jump, teleport)
class CharacterController {
public:
    EntityID entityId = kInvalidEntityID;
    glm::vec3 position{0.0f, 1.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.4f;
    float height = 1.8f;
    float movementSpeed = 5.0f;
    float jumpSpeed = 6.0f;
    float gravity = -9.81f;
    float slopeLimitDeg = 45.0f;
    float stepOffset = 0.35f;

    void move(const PhysicsWorld& world, const glm::vec3& wishDir, float dt);
    void jump() noexcept;
    void teleport(const glm::vec3& newPos) noexcept { position = newPos; velocity = glm::vec3(0.0f); }

    [[nodiscard]] bool isGrounded() const noexcept { return m_grounded; }
    [[nodiscard]] bool isFalling() const noexcept { return !m_grounded && velocity.y < -0.1f; }
    [[nodiscard]] const glm::vec3& getVelocity() const noexcept { return velocity; }

private:
    bool m_grounded = false;
};

// Generic Engine-Level Interaction System (game-agnostic target selection & dispatch)
class InteractionSystem {
public:
    void registerInteractable(const InteractableRecord& rec);
    bool unregisterInteractable(EntityID entityId);
    [[nodiscard]] EntityID queryBestTarget(const glm::vec3& origin, const glm::vec3& forwardDir, float maxRange) const;
    bool triggerInteraction(EntityID actorId, EntityID targetId);
    void setInteractionCallback(std::function<void(EntityID, EntityID)> cb) { m_callback = std::move(cb); }

private:
    std::unordered_map<EntityID, InteractableRecord> m_interactables;
    std::function<void(EntityID, EntityID)> m_callback;
};

} // namespace sidera
