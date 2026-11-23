#include "runtime/RuntimeUI.h"
#include "runtime/InputManager.h"
#include "runtime/AnimationRuntime.h"
#include "runtime/ScriptingAndEvents.h"
#include "runtime/SaveAndLocalization.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace sidera;
using namespace sidera::runtime;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond, msg) \
    do { \
        if (cond) { ++g_passed; } \
        else { ++g_failed; std::cerr << "[FAIL] " << msg << "\n"; } \
    } while (0)

class ThrowingScript final : public IScriptBehavior {
public:
    void onUpdate(EntityID, float) override { throw std::runtime_error("Simulated script failure"); }
};

class CounterScript final : public IScriptBehavior {
public:
    explicit CounterScript(int* c) : m_counter(c) {}
    void onUpdate(EntityID, float) override { ++(*m_counter); }
private:
    int* m_counter = nullptr;
};

static void testRuntimeUIAndInput() {
    UISystem ui;
    bool clicked = false;
    UIWidget btn{};
    btn.type = UIWidgetType::Button;
    btn.name = "StartBtn";
    btn.localRect = {10.0f, 20.0f, 120.0f, 40.0f};
    btn.anchorX = 0.5f;
    btn.focusable = true;
    btn.onClick = [&]() { clicked = true; };
    const uint64_t btnId = ui.createWidget(btn);

    ui.computeLayout(1000.0f, 800.0f, 1.0f);
    CHECK(std::abs(ui.findWidget(btnId)->computedRect.x - 510.0f) < 1e-3f, "UI anchor + localRect layout computed");
    CHECK(ui.dispatchPointerClick(550.0f, 35.0f) && clicked, "UI pointer hit-test dispatches onClick");
    CHECK(ui.focusNext() == btnId && ui.getFocusedWidgetId() == btnId, "UI focus navigation cycles focusables");
    CHECK(ui.buildDrawList().size() == 1u, "UI draw list generated");

    // Input Manager: context isolation, rebinding conflict detection, analog deadzone
    InputManager input;
    input.registerAction({"Jump", InputContext::Gameplay, 32});
    input.registerAction({"Interact", InputContext::Gameplay, 69});
    CHECK(!input.rebindAction("Jump", 69, false), "Rebind conflict within same context rejected");
    CHECK(input.rebindAction("Jump", 70, false), "Non-conflicting rebind succeeds");
    input.setKeyState(70, true);
    CHECK(input.isActionHeld("Jump"), "Action held when Gameplay context active");
    input.setContextActive(InputContext::Gameplay, false);
    CHECK(!input.isActionHeld("Jump"), "Action isolated when Gameplay context inactive");
    CHECK(InputManager::shapeAnalogAxis(0.1f, 0.15f, 1.0f, false) == 0.0f, "Analog input inside deadzone zeroed");
    CHECK(InputManager::shapeAnalogAxis(1.0f, 0.15f, 1.0f, true) == -1.0f, "Analog inversion works");
}

static void testAnimationScriptingEvents() {
    // Animation State Machine: crossfade + timed event
    AnimationStateMachine anim;
    anim.addState({"Idle", 1.0f, true, {0,0,0}, {0,0,0}, {}});
    anim.addState({"Run", 1.0f, true, {0,0,0}, {10,0,0}, {{0.4f, "StepEvent"}}});
    anim.addTransition({"Idle", "Run", "Speed", 0.5f, 0.2f});

    bool firedEvent = false;
    anim.setEventCallback([&](const std::string& ev) { if (ev == "StepEvent") firedEvent = true; });
    anim.setParameter("Speed", 1.0f);
    anim.update(0.5f);
    CHECK(anim.getCurrentState() == "Run", "Animation transitioned Idle -> Run");
    CHECK(firedEvent, "Timed AnimationEvent fired at 0.4s");
    CHECK(anim.evaluateBlendedPosition().x > 0.0f, "Blended pose evaluated");

    // Runtime Event Bus + Script failure isolation
    RuntimeEventBus bus;
    int evCount = 0;
    const uint64_t tok = bus.subscribe("OnLoot", [&](const RuntimeEvent&) { ++evCount; });
    bus.enqueue({"OnLoot", 1, "Gold"});
    CHECK(evCount == 0, "Queued event deferred until dispatchQueued()");
    bus.dispatchQueued();
    CHECK(evCount == 1, "Queued event dispatched");
    bus.unsubscribe("OnLoot", tok);

    ScriptRuntime scripts;
    int healthyTicks = 0;
    scripts.attachScript(1, std::make_unique<ThrowingScript>());
    scripts.attachScript(2, std::make_unique<CounterScript>(&healthyTicks));
    scripts.updateAll(1.0f / 60.0f);
    CHECK(scripts.getFailureCount() == 1u && healthyTicks == 1,
          "Throwing script isolated without breaking healthy entity scripts");
    scripts.shutdownAll();
}

static void testSaveLoadAndLocalization() {
    const std::filesystem::path saveDir = "runtime_save_unit_test_tmp";
    std::error_code ec;
    std::filesystem::remove_all(saveDir, ec);

    SaveSystem saves(saveDir);
    SaveGameData d{};
    d.slotId = "slot_01";
    d.sceneName = "EmeraldValley";
    d.variables["gold"] = "250";
    CHECK(saves.saveGame(d), "Transactional saveGame succeeds");
    CHECK(saves.listSaveSlots().size() == 1u, "listSaveSlots finds saved slot");

    SaveGameData loaded{};
    CHECK(saves.loadGame("slot_01", loaded) && loaded.sceneName == "EmeraldValley" && loaded.variables["gold"] == "250",
          "loadGame restores persistent state");
    CHECK(!saves.loadGame("missing_slot", loaded) && loaded.variables["gold"] == "250",
          "Failed loadGame preserves existing state transactionally");
    CHECK(saves.deleteSave("slot_01"), "deleteSave removes slot");
    std::filesystem::remove_all(saveDir, ec);

    // Localization
    LocalizationManager loc;
    loc.registerString("en-US", "greet", "Welcome, {0}!");
    loc.registerString("tr-TR", "greet", "Hos geldin, {0}!");
    loc.setFallbackLocale("en-US");
    loc.setActiveLocale("tr-TR");
    CHECK(loc.format("greet", "Hero") == "Hos geldin, Hero!", "Active locale + {0} substitution works");
    loc.setActiveLocale("fr-FR");
    CHECK(loc.format("greet", "Hero") == "Welcome, Hero!", "Fallback locale resolves missing active locale");
    CHECK(loc.lookup("unknown_key") == "[unknown_key]", "Missing key returns safe marker");
}

int main() {
    testRuntimeUIAndInput();
    testAnimationScriptingEvents();
    testSaveLoadAndLocalization();

    std::cout << "test_runtime_systems: " << g_passed << " passed, " << g_failed << " failed.\n";
    return (g_failed == 0) ? 0 : 1;
}
