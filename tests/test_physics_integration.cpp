#include "physics/PhysicsTypes.h"
#include "physics/PhysicsWorld.h"

#include <cmath>
#include <iostream>

using namespace sidera;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

static void testRigidBodySimulationAndEvents() {
    PhysicsWorld world;

    // Static ground plane at y = 0
    RigidBodyDesc ground{};
    ground.entityId = 1;
    ground.type = RigidBodyType::Static;
    ground.collider.shape = ColliderShapeType::Plane;
    ground.collider.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    ground.collider.planeOffset = 0.0f;
    CHECK(world.addBody(ground), "Static ground plane added");
    CHECK(!world.addBody(ground), "Duplicate EntityID body rejected");

    // Dynamic sphere dropped from y = 3.0
    RigidBodyDesc ball{};
    ball.entityId = 2;
    ball.type = RigidBodyType::Dynamic;
    ball.position = glm::vec3(0.0f, 3.0f, 0.0f);
    ball.mass = 2.0f;
    ball.collider.shape = ColliderShapeType::Sphere;
    ball.collider.radius = 0.5f;
    ball.collider.material.restitution = 0.5f;
    CHECK(world.addBody(ball), "Dynamic sphere added");

    bool sawEnter = false;
    world.setEventCallback([&](const PhysicsEvent& ev) {
        if (ev.type == PhysicsEventType::CollisionEnter) sawEnter = true;
    });

    // Simulate 1.2 seconds via fixed timestep
    for (int i = 0; i < 72; ++i) {
        world.step(1.0f / 60.0f);
    }

    const auto* st = world.findBody(2);
    CHECK(st != nullptr && st->position.y >= 0.45f && st->position.y < 2.5f,
          "Gravity pulled sphere down and ground plane stopped penetration");
    CHECK(sawEnter, "CollisionEnter event dispatched on impact");
    CHECK(world.getStats().dynamicBodies == 1u && world.getStats().staticBodies == 1u,
          "PhysicsStatistics reports 1 dynamic + 1 static body");
}

static void testQueriesAndFiltering() {
    PhysicsWorld world;
    RigidBodyDesc box{};
    box.entityId = 10;
    box.type = RigidBodyType::Static;
    box.position = glm::vec3(0.0f, 1.0f, -5.0f);
    box.collider.shape = ColliderShapeType::Box;
    box.collider.halfExtents = glm::vec3(1.0f);
    box.collider.filter.layer = 0x2u;
    world.addBody(box);

    const RaycastHit missLayer = world.raycast(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 20.0f, {0x1u, 0x1u});
    CHECK(!missLayer.hit, "Layer filter excludes mismatched body");

    const RaycastHit hit = world.raycast(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 20.0f);
    CHECK(hit.hit && hit.entityId == 10u && std::abs(hit.distance - 4.0f) < 1e-3f,
          "Raycast hits box at distance 4.0");

    const auto overlaps = world.overlapSphere(glm::vec3(0.0f, 1.0f, -4.5f), 1.0f);
    CHECK(overlaps.size() == 1u && overlaps[0] == 10u, "OverlapSphere finds box");
}

static void testCharacterControllerAndInteraction() {
    PhysicsWorld world;
    RigidBodyDesc ground{};
    ground.entityId = 1;
    ground.type = RigidBodyType::Static;
    ground.collider.shape = ColliderShapeType::Plane;
    ground.collider.planeNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    ground.collider.planeOffset = 0.0f;
    world.addBody(ground);

    CharacterController cc;
    cc.entityId = 100;
    cc.position = glm::vec3(0.0f, 2.0f, 0.0f);
    for (int i = 0; i < 60; ++i) {
        cc.move(world, glm::vec3(1.0f, 0.0f, 0.0f), 1.0f / 60.0f);
    }
    CHECK(cc.isGrounded(), "CharacterController lands and detects ground");
    CHECK(cc.position.x > 1.0f, "CharacterController moves horizontally");
    cc.jump();
    CHECK(!cc.isGrounded() && cc.getVelocity().y > 0.0f, "CharacterController jump launches upward");

    // Generic InteractionSystem
    InteractionSystem interact;
    interact.registerInteractable({201, glm::vec3(0.0f, 1.0f, -2.0f), 4.0f, 1.0f, true});
    interact.registerInteractable({202, glm::vec3(0.0f, 1.0f, -2.5f), 4.0f, 5.0f, true});
    const EntityID best = interact.queryBestTarget(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 5.0f);
    CHECK(best == 202u, "InteractionSystem selects higher-priority target in range");
    bool interacted = false;
    interact.setInteractionCallback([&](EntityID a, EntityID t) { interacted = (a == 100u && t == 202u); });
    CHECK(interact.triggerInteraction(100u, best) && interacted, "Interaction dispatched to callback");
    CHECK(interact.unregisterInteractable(202u), "Interactable unregistered cleanly");
}

int main() {
    testRigidBodySimulationAndEvents();
    testQueriesAndFiltering();
    testCharacterControllerAndInteraction();

    std::cout << "test_physics_integration: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
