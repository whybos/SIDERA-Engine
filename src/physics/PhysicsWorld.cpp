#include "physics/PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace sidera {

bool PhysicsWorld::addBody(const RigidBodyDesc& desc) {
    if (desc.entityId == kInvalidEntityID || !desc.collider.isValid()) return false;
    if (m_bodies.find(desc.entityId) != m_bodies.end()) return false; // prevent duplicate body per entity

    RigidBodyState st{};
    st.desc = desc;
    st.desc.validate();
    st.position = st.desc.position;
    st.prevPosition = st.desc.position;
    st.velocity = st.desc.velocity;
    m_bodies[desc.entityId] = st;
    return true;
}

bool PhysicsWorld::removeBody(EntityID entityId) {
    if (m_bodies.erase(entityId) == 0) return false;
    // Clean up any contact/trigger pairs referencing the removed entity
    for (auto it = m_prevContacts.begin(); it != m_prevContacts.end();) {
        if (it->first == entityId || it->second == entityId) it = m_prevContacts.erase(it);
        else ++it;
    }
    for (auto it = m_prevTriggers.begin(); it != m_prevTriggers.end();) {
        if (it->first == entityId || it->second == entityId) it = m_prevTriggers.erase(it);
        else ++it;
    }
    return true;
}

RigidBodyState* PhysicsWorld::findBody(EntityID entityId) noexcept {
    auto it = m_bodies.find(entityId);
    return (it != m_bodies.end()) ? &it->second : nullptr;
}

const RigidBodyState* PhysicsWorld::findBody(EntityID entityId) const noexcept {
    auto it = m_bodies.find(entityId);
    return (it != m_bodies.end()) ? &it->second : nullptr;
}

void PhysicsWorld::clear() noexcept {
    m_bodies.clear();
    m_prevContacts.clear();
    m_prevTriggers.clear();
    m_lastEvents.clear();
    m_accumulator = 0.0f;
}

void PhysicsWorld::applyForce(EntityID entityId, const glm::vec3& force) noexcept {
    if (auto* b = findBody(entityId)) {
        if (b->desc.type == RigidBodyType::Dynamic) b->accumulatedForce += force;
    }
}

void PhysicsWorld::applyImpulse(EntityID entityId, const glm::vec3& impulse) noexcept {
    if (auto* b = findBody(entityId)) {
        if (b->desc.type == RigidBodyType::Dynamic) b->velocity += impulse / b->desc.mass;
    }
}

void PhysicsWorld::setKinematicTarget(EntityID entityId, const glm::vec3& targetPos) noexcept {
    if (auto* b = findBody(entityId)) {
        if (b->desc.type == RigidBodyType::Kinematic) {
            b->prevPosition = b->position;
            b->position = targetPos;
        }
    }
}

uint32_t PhysicsWorld::step(float deltaTime) {
    if (!m_settings.enabled || m_settings.paused) return 0u;
    const auto t0 = std::chrono::steady_clock::now();

    const float clampedDt = std::clamp(deltaTime, 0.0f, m_settings.maxFrameDelta);
    m_accumulator += clampedDt;

    uint32_t substeps = 0u;
    while (m_accumulator >= m_settings.fixedTimestep && substeps < m_settings.maxSubsteps) {
        stepFixed(m_settings.fixedTimestep);
        m_accumulator -= m_settings.fixedTimestep;
        ++substeps;
    }

    m_stats.substepsExecuted = substeps;
    m_stats.queryCount = m_queryCounter;
    m_stats.lastStepCpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return substeps;
}

void PhysicsWorld::stepFixed(float dt) {
    m_stats.activeBodies = 0;
    m_stats.staticBodies = 0;
    m_stats.dynamicBodies = 0;
    m_stats.kinematicBodies = 0;

    // 1. Integrate dynamic bodies
    for (auto& [id, b] : m_bodies) {
        if (!b.active) continue;
        ++m_stats.activeBodies;
        if (b.desc.type == RigidBodyType::Static) {
            ++m_stats.staticBodies;
            continue;
        }
        if (b.desc.type == RigidBodyType::Kinematic) {
            ++m_stats.kinematicBodies;
            continue;
        }
        ++m_stats.dynamicBodies;
        b.prevPosition = b.position;
        glm::vec3 accel = b.accumulatedForce / b.desc.mass;
        if (b.desc.useGravity) accel += m_settings.gravity;
        b.velocity += accel * dt;
        b.velocity *= (1.0f - b.desc.linearDamping * dt);
        b.position += b.velocity * dt;
        b.accumulatedForce = glm::vec3(0.0f);
    }

    // 2. Narrowphase collision detection & impulse response
    std::set<ContactPair> currContacts;
    std::set<ContactPair> currTriggers;
    m_lastEvents.clear();
    resolveCollisions(currContacts, currTriggers);
    m_stats.collisionPairs = static_cast<uint32_t>(currContacts.size());

    // 3. Emit deferred Enter / Persist / Exit events
    for (const auto& pair : currContacts) {
        const bool existed = m_prevContacts.count(pair) > 0;
        m_lastEvents.push_back({
            existed ? PhysicsEventType::CollisionPersist : PhysicsEventType::CollisionEnter,
            pair.first, pair.second, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)
        });
    }
    for (const auto& pair : m_prevContacts) {
        if (currContacts.count(pair) == 0) {
            m_lastEvents.push_back({PhysicsEventType::CollisionExit, pair.first, pair.second, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});
        }
    }
    for (const auto& pair : currTriggers) {
        if (m_prevTriggers.count(pair) == 0) {
            m_lastEvents.push_back({PhysicsEventType::TriggerEnter, pair.first, pair.second, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});
        }
    }
    for (const auto& pair : m_prevTriggers) {
        if (currTriggers.count(pair) == 0) {
            m_lastEvents.push_back({PhysicsEventType::TriggerExit, pair.first, pair.second, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)});
        }
    }

    m_prevContacts = std::move(currContacts);
    m_prevTriggers = std::move(currTriggers);

    if (m_eventCallback) {
        const auto eventsCopy = m_lastEvents;
        for (const auto& ev : eventsCopy) m_eventCallback(ev);
    }
}

void PhysicsWorld::resolveCollisions(std::set<ContactPair>& currContacts, std::set<ContactPair>& currTriggers) {
    std::vector<RigidBodyState*> list;
    list.reserve(m_bodies.size());
    for (auto& [id, b] : m_bodies) {
        if (b.active) list.push_back(&b);
    }

    for (size_t i = 0; i < list.size(); ++i) {
        for (size_t j = i + 1; j < list.size(); ++j) {
            RigidBodyState* a = list[i];
            RigidBodyState* b = list[j];
            if (a->desc.type == RigidBodyType::Static && b->desc.type == RigidBodyType::Static) continue;
            if (!a->desc.collider.filter.matches(b->desc.collider.filter)) continue;

            bool hit = false;
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            float penetration = 0.0f;

            const auto sA = a->desc.collider.shape;
            const auto sB = b->desc.collider.shape;

            // Dynamic/Kinematic vs Plane
            if (sA == ColliderShapeType::Plane || sB == ColliderShapeType::Plane) {
                RigidBodyState* planeBody = (sA == ColliderShapeType::Plane) ? a : b;
                RigidBodyState* other = (sA == ColliderShapeType::Plane) ? b : a;
                const glm::vec3 n = planeBody->desc.collider.planeNormal;
                const float d = planeBody->desc.collider.planeOffset;
                const float extent = (other->desc.collider.shape == ColliderShapeType::Sphere)
                    ? other->desc.collider.radius
                    : (other->desc.collider.shape == ColliderShapeType::Capsule ? other->desc.collider.height * 0.5f : other->desc.collider.halfExtents.y);
                const float signedDist = glm::dot(n, other->position) - d - extent;
                if (signedDist < 0.0f) {
                    hit = true;
                    penetration = -signedDist;
                    normal = (a == other) ? n : -n;
                }
            } else if (sA == ColliderShapeType::Sphere && sB == ColliderShapeType::Sphere) {
                const glm::vec3 delta = a->position - b->position;
                const float dist = glm::length(delta);
                const float rSum = a->desc.collider.radius + b->desc.collider.radius;
                if (dist < rSum) {
                    hit = true;
                    penetration = rSum - dist;
                    normal = (dist > 1e-5f) ? (delta / dist) : glm::vec3(0.0f, 1.0f, 0.0f);
                }
            } else {
                // General AABB overlap fallback for Box/Capsule/Mixed shapes
                auto getHalf = [](const ColliderDesc& c) -> glm::vec3 {
                    if (c.shape == ColliderShapeType::Sphere) return glm::vec3(c.radius);
                    if (c.shape == ColliderShapeType::Capsule) return glm::vec3(c.radius, c.height * 0.5f, c.radius);
                    return c.halfExtents;
                };
                const glm::vec3 hA = getHalf(a->desc.collider);
                const glm::vec3 hB = getHalf(b->desc.collider);
                const glm::vec3 d = a->position - b->position;
                const glm::vec3 ov = (hA + hB) - glm::abs(d);
                if (ov.x > 0.0f && ov.y > 0.0f && ov.z > 0.0f) {
                    hit = true;
                    if (ov.y <= ov.x && ov.y <= ov.z) {
                        penetration = ov.y;
                        normal = glm::vec3(0.0f, (d.y >= 0.0f) ? 1.0f : -1.0f, 0.0f);
                    } else if (ov.x <= ov.z) {
                        penetration = ov.x;
                        normal = glm::vec3((d.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
                    } else {
                        penetration = ov.z;
                        normal = glm::vec3(0.0f, 0.0f, (d.z >= 0.0f) ? 1.0f : -1.0f);
                    }
                }
            }

            if (!hit) continue;

            const ContactPair pair = makePair(a->desc.entityId, b->desc.entityId);
            if (a->desc.collider.isTrigger || b->desc.collider.isTrigger) {
                currTriggers.insert(pair);
                continue;
            }
            currContacts.insert(pair);

            const float invMassA = (a->desc.type == RigidBodyType::Dynamic) ? (1.0f / a->desc.mass) : 0.0f;
            const float invMassB = (b->desc.type == RigidBodyType::Dynamic) ? (1.0f / b->desc.mass) : 0.0f;
            const float invMassSum = invMassA + invMassB;
            if (invMassSum <= 0.0f) continue;

            // Positional depenetration
            a->position += normal * (penetration * (invMassA / invMassSum));
            b->position -= normal * (penetration * (invMassB / invMassSum));

            // Impulse resolution with restitution & dynamic friction
            const glm::vec3 relVel = a->velocity - b->velocity;
            const float velAlongNormal = glm::dot(relVel, normal);
            if (velAlongNormal < 0.0f) {
                const float e = std::min(a->desc.collider.material.restitution, b->desc.collider.material.restitution);
                const float jImp = -(1.0f + e) * velAlongNormal / invMassSum;
                const glm::vec3 impulse = normal * jImp;
                a->velocity += impulse * invMassA;
                b->velocity -= impulse * invMassB;

                // Tangential friction
                const glm::vec3 tangent = relVel - normal * velAlongNormal;
                const float tLen = glm::length(tangent);
                if (tLen > 1e-4f) {
                    const float mu = std::sqrt(a->desc.collider.material.dynamicFriction * b->desc.collider.material.dynamicFriction);
                    const glm::vec3 fricDir = tangent / tLen;
                    const float jt = std::min(tLen / invMassSum, jImp * mu);
                    a->velocity -= fricDir * (jt * invMassA);
                    b->velocity += fricDir * (jt * invMassB);
                }
            }
        }
    }
}

RaycastHit PhysicsWorld::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                 float maxDistance, const CollisionFilter& filter,
                                 bool includeTriggers) const {
    ++m_queryCounter;
    RaycastHit best{};
    best.distance = maxDistance;
    const glm::vec3 dir = glm::normalize(direction);

    for (const auto& [id, b] : m_bodies) {
        if (!b.active) continue;
        if (!includeTriggers && b.desc.collider.isTrigger) continue;
        if (!filter.matches(b.desc.collider.filter)) continue;

        float tHit = -1.0f;
        glm::vec3 hitN(0.0f, 1.0f, 0.0f);

        if (b.desc.collider.shape == ColliderShapeType::Plane) {
            const glm::vec3 n = b.desc.collider.planeNormal;
            const float denom = glm::dot(dir, n);
            if (std::abs(denom) > 1e-5f) {
                const float t = (b.desc.collider.planeOffset - glm::dot(origin, n)) / denom;
                if (t >= 0.0f && t <= best.distance) {
                    tHit = t;
                    hitN = (denom < 0.0f) ? n : -n;
                }
            }
        } else if (b.desc.collider.shape == ColliderShapeType::Sphere) {
            const glm::vec3 oc = origin - b.position;
            const float bTerm = glm::dot(oc, dir);
            const float cTerm = glm::dot(oc, oc) - b.desc.collider.radius * b.desc.collider.radius;
            const float disc = bTerm * bTerm - cTerm;
            if (disc >= 0.0f) {
                const float t = -bTerm - std::sqrt(disc);
                if (t >= 0.0f && t <= best.distance) {
                    tHit = t;
                    hitN = glm::normalize((origin + dir * t) - b.position);
                }
            }
        } else {
            // Slab AABB intersection
            const glm::vec3 h = (b.desc.collider.shape == ColliderShapeType::Capsule)
                ? glm::vec3(b.desc.collider.radius, b.desc.collider.height * 0.5f, b.desc.collider.radius)
                : b.desc.collider.halfExtents;
            const glm::vec3 bMin = b.position - h;
            const glm::vec3 bMax = b.position + h;
            float tmin = 0.0f, tmax = best.distance;
            int hitAxis = 1;
            for (int a = 0; a < 3; ++a) {
                if (std::abs(dir[a]) < 1e-6f) {
                    if (origin[a] < bMin[a] || origin[a] > bMax[a]) { tmin = tmax + 1.0f; break; }
                } else {
                    float t1 = (bMin[a] - origin[a]) / dir[a];
                    float t2 = (bMax[a] - origin[a]) / dir[a];
                    if (t1 > t2) std::swap(t1, t2);
                    if (t1 > tmin) { tmin = t1; hitAxis = a; }
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax) break;
                }
            }
            if (tmin <= tmax && tmin >= 0.0f && tmin <= best.distance) {
                tHit = tmin;
                hitN = glm::vec3(0.0f);
                hitN[hitAxis] = (dir[hitAxis] > 0.0f) ? -1.0f : 1.0f;
            }
        }

        if (tHit >= 0.0f && tHit < best.distance) {
            best.hit = true;
            best.entityId = id;
            best.distance = tHit;
            best.position = origin + dir * tHit;
            best.normal = hitN;
        }
    }
    return best;
}

std::vector<EntityID> PhysicsWorld::overlapSphere(const glm::vec3& center, float radius,
                                                  const CollisionFilter& filter) const {
    ++m_queryCounter;
    std::vector<EntityID> hits;
    for (const auto& [id, b] : m_bodies) {
        if (!b.active || !filter.matches(b.desc.collider.filter)) continue;
        const float otherR = (b.desc.collider.shape == ColliderShapeType::Sphere)
            ? b.desc.collider.radius
            : glm::length(b.desc.collider.halfExtents);
        if (glm::length(b.position - center) <= (radius + otherR)) {
            hits.push_back(id);
        }
    }
    return hits;
}

// --- CharacterController ---

void CharacterController::jump() noexcept {
    if (m_grounded) {
        velocity.y = jumpSpeed;
        m_grounded = false;
    }
}

void CharacterController::move(const PhysicsWorld& world, const glm::vec3& wishDir, float dt) {
    glm::vec3 horiz(wishDir.x, 0.0f, wishDir.z);
    if (glm::length(horiz) > 1e-4f) horiz = glm::normalize(horiz) * movementSpeed;
    velocity.x = horiz.x;
    velocity.z = horiz.z;

    if (!m_grounded) {
        velocity.y += gravity * dt;
    }

    // Horizontal wall collision & sliding
    const glm::vec3 horizStep(velocity.x * dt, 0.0f, velocity.z * dt);
    const float hLen = glm::length(horizStep);
    if (hLen > 1e-5f) {
        const RaycastHit wallHit = world.raycast(position, horizStep / hLen, hLen + radius);
        if (wallHit.hit && wallHit.normal.y < std::cos(glm::radians(slopeLimitDeg))) {
            // Slide along wall plane
            const glm::vec3 slide = horizStep - wallHit.normal * glm::dot(horizStep, wallHit.normal);
            position += slide;
        } else {
            position += horizStep;
        }
    }

    // Vertical movement & ground detection
    position.y += velocity.y * dt;
    const float halfH = height * 0.5f;
    const RaycastHit groundHit = world.raycast(position, glm::vec3(0.0f, -1.0f, 0.0f), halfH + stepOffset);
    if (groundHit.hit && velocity.y <= 0.0f) {
        const float minY = groundHit.position.y + halfH;
        if (position.y <= minY + 0.05f) {
            position.y = minY;
            velocity.y = 0.0f;
            m_grounded = true;
            return;
        }
    }
    m_grounded = false;
}

// --- InteractionSystem ---

void InteractionSystem::registerInteractable(const InteractableRecord& rec) {
    if (rec.entityId != kInvalidEntityID) m_interactables[rec.entityId] = rec;
}

bool InteractionSystem::unregisterInteractable(EntityID entityId) {
    return m_interactables.erase(entityId) > 0;
}

EntityID InteractionSystem::queryBestTarget(const glm::vec3& origin, const glm::vec3& forwardDir, float maxRange) const {
    const glm::vec3 fwd = (glm::length(forwardDir) > 1e-4f) ? glm::normalize(forwardDir) : glm::vec3(0.0f, 0.0f, -1.0f);
    EntityID bestId = kInvalidEntityID;
    float bestScore = -1e30f;

    for (const auto& [id, rec] : m_interactables) {
        if (!rec.enabled) continue;
        const glm::vec3 toTarget = rec.position - origin;
        const float dist = glm::length(toTarget);
        const float effectiveRange = std::min(maxRange, rec.range);
        if (dist > effectiveRange) continue;
        const float align = (dist > 1e-4f) ? glm::dot(fwd, toTarget / dist) : 1.0f;
        const float score = rec.priority * 2.0f + align - (dist / std::max(effectiveRange, 0.1f));
        if (score > bestScore) {
            bestScore = score;
            bestId = id;
        }
    }
    return bestId;
}

bool InteractionSystem::triggerInteraction(EntityID actorId, EntityID targetId) {
    auto it = m_interactables.find(targetId);
    if (it == m_interactables.end() || !it->second.enabled) return false;
    if (m_callback) m_callback(actorId, targetId);
    return true;
}

} // namespace sidera
