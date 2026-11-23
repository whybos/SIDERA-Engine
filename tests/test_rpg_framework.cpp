#include "framework/FrameworkCore.h"
#include "framework/CharacterCamera.h"
#include "framework/DialogueNarrative.h"
#include "framework/QuestSystem.h"
#include "framework/NPCCompanion.h"
#include "framework/InventoryProgression.h"
#include "framework/CombatAndWorldEvents.h"

#include "physics/PhysicsWorld.h"
#include "runtime/AnimationRuntime.h"
#include "runtime/InputManager.h"
#include "runtime/SaveAndLocalization.h"
#include "runtime/ScriptingAndEvents.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sidera;
using namespace sidera::framework;
using namespace sidera::runtime;

namespace {

int g_failures = 0;

void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    } else {
        std::cout << "[PASS] " << msg << "\n";
    }
}

struct MockModule : IGameFrameworkModule {
    FrameworkModuleDescriptor desc;
    std::vector<std::string>* log = nullptr;
    bool throwOnUpdate = false;

    MockModule(std::string id, std::vector<std::string> deps, std::vector<std::string>* l, bool failUpd = false)
        : desc{std::move(id), "", std::move(deps)}, log(l), throwOnUpdate(failUpd) {}

    FrameworkModuleDescriptor getDescriptor() const override { return desc; }
    bool onInitialize() override { if (log) log->push_back("init:" + desc.id); return true; }
    void onUpdate(float) override {
        if (throwOnUpdate) throw std::runtime_error("simulated module failure");
        if (log) log->push_back("upd:" + desc.id);
    }
    void onShutdown() override { if (log) log->push_back("shut:" + desc.id); }
};

void testFrameworkRegistry() {
    std::cout << "\n--- 1. FrameworkRegistry (Dependencies, Cycles, Order, Isolation) ---\n";
    std::vector<std::string> log;
    FrameworkRegistry reg;
    check(reg.registerModule(std::make_unique<MockModule>("quest", std::vector<std::string>{"dialogue"}, &log)),
          "Register quest module");
    check(!reg.registerModule(std::make_unique<MockModule>("quest", std::vector<std::string>{}, &log)),
          "Reject duplicate module ID");
    check(reg.registerModule(std::make_unique<MockModule>("dialogue", std::vector<std::string>{}, &log)),
          "Register dialogue dependency");
    check(reg.registerModule(std::make_unique<MockModule>("faulty", std::vector<std::string>{}, &log, true)),
          "Register failure-isolated module");

    std::string err;
    check(reg.initializeAll(&err), "Topological initializeAll succeeds");
    check(log.size() == 3 && log[0] == "init:dialogue" && log[1] == "init:quest",
          "Dependency initialized before dependent");

    reg.updateAll(0.016f);
    check(reg.getFailureCount() == 1, "Faulty module exception isolated");

    log.clear();
    reg.shutdownAll();
    check(log.size() == 3 && log.back() == "shut:dialogue", "Reverse shutdown order");

    // Cycle detection
    FrameworkRegistry cyclic;
    cyclic.registerModule(std::make_unique<MockModule>("A", std::vector<std::string>{"B"}, nullptr));
    cyclic.registerModule(std::make_unique<MockModule>("B", std::vector<std::string>{"A"}, nullptr));
    check(!cyclic.validateDependencies(&err), "Cyclic dependency rejected");
}

void testCharacterAndCamera() {
    std::cout << "\n--- 2. Character & Camera Framework ---\n";
    PhysicsWorld phys;
    RigidBodyDesc floor{};
    floor.entityId = 1;
    floor.type = RigidBodyType::Static;
    floor.position = {0.0f, -0.5f, 0.0f};
    floor.collider.halfExtents = {50.0f, 0.5f, 50.0f};
    phys.addBody(floor);

    InputManager input;
    input.registerAction({"MoveForward", InputContext::Gameplay, 87});
    input.registerAction({"Sprint", InputContext::Gameplay, 340});
    input.setKeyState(87, true);
    input.setKeyState(340, true);

    AnimationStateMachine anim;
    anim.addState({"Idle", 1.0f, true, {}, {}, {}});
    anim.addState({"Sprint", 0.6f, true, {}, {}, {}});
    anim.addTransition({"Idle", "Sprint", "Speed", 6.0f, 0.15f});

    ThirdPersonCharacter character;
    character.setPosition({0.0f, 1.0f, 0.0f});
    ThirdPersonCamera cam;
    cam.addInput(0.0f, 100.0f, -2.0f); // test pitch clamp & zoom
    check(cam.getPitchDegrees() <= 65.0f && cam.getDistance() == 3.0f, "Camera pitch clamped and zoom applied");

    for (int i = 0; i < 10; ++i) {
        character.update(input, phys, &anim, cam.getYawRadians(), 0.05f);
        cam.update(character.getPosition(), 0.05f);
    }
    check(character.getPosition().z > 1.0f, "Character moved forward via InputManager + PhysicsWorld");
    check(anim.getCurrentState() == "Sprint", "AnimationStateMachine transitioned to Sprint");
}

void testCombatSystem() {
    std::cout << "\n--- 3. Optional Combat System ---\n";
    CombatSystem combat;
    combat.registerAbility({"slash", 40.0f, "Physical", 2.0f});

    CombatantState hero{100.0f, 100.0f, {}, {}, {}};
    CombatantState dummy{100.0f, 100.0f, {{"Physical", 0.25f}}, {}, {}};

    float dealt = 0.0f;
    check(combat.useAbility(hero, dummy, "slash", dealt) && std::abs(dealt - 30.0f) < 0.01f,
          "Damage reduced by 25% Physical resistance (40 -> 30)");
    check(!combat.useAbility(hero, dummy, "slash", dealt), "Cooldown blocks immediate reuse");

    combat.applyStatusEffect(dummy, {"burn", 2.0f, 5.0f});
    combat.update(hero, 2.5f);
    combat.update(dummy, 2.5f);
    check(std::abs(dummy.health - 60.0f) < 0.01f && dummy.activeEffects.empty(),
          "Status effect ticked (10 dmg) and expired cleanly");
    check(combat.useAbility(hero, dummy, "slash", dealt), "Ability usable again after cooldown");
}

void testReferenceVerticalSliceAndPersistence() {
    std::cout << "\n--- 4. Reference Vertical Slice + Save/Reload ---\n";
    RuntimeEventBus bus;
    LocalizationManager loc;
    loc.setActiveLocale("en");
    loc.registerString("en", "dlg.guide.greet", "Find the beacon relic before dusk.");

    // Engine Core Interaction
    InteractionSystem interact;
    InteractableRecord guideRec{};
    guideRec.entityId = 100;
    guideRec.position = {2.0f, 0.0f, 0.0f};
    guideRec.range = 3.0f;
    guideRec.priority = 1.0f;
    guideRec.enabled = true;
    interact.registerInteractable(guideRec);
    const EntityID bestTarget = interact.queryBestTarget({0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 3.0f);
    check(bestTarget == 100, "InteractionSystem detects NPC interactable");

    // Dialogue + Narrative
    NarrativeState narrative;
    DialogueSystem dialogue;
    DialogueGraph g;
    g.id = "guide_intro";
    g.entryNodeId = "n1";
    g.nodes["n1"] = {"n1", "speaker.guide", "dlg.guide.greet", {
        {"accept", "choice.accept", "", {}, "accepted_guide", "1", "quest.request_start", "q_beacon"}
    }};
    dialogue.registerGraph(g);

    // Quest Manager wired to RuntimeEventBus
    QuestManager quests;
    quests.registerQuest({
        "q_beacon", "quest.beacon.title", {},
        {{"find_relic", "obj.find_relic", "world.relic_collected", "beacon_relic", 1, false}},
        false, "reward.grant_item", "relic_amulet"
    });

    // Inventory, Equipment, Attributes, Progression
    ItemRegistry items;
    items.registerItem({"relic_amulet", "item.amulet", 1, "Neck", {{"", "Focus", ModifierOp::Flat, 15.0f}}});
    items.registerItem({"herb", "item.herb", 10, "", {}});

    InventoryContainer inv(8);
    EquipmentManager equip;
    AttributeSet attrs;
    attrs.defineAttribute("Focus", 10.0f, 0.0f, 100.0f);
    ProgressionSystem prog;

    // World Time, NPC & Companion
    WorldTimeSystem worldTime;
    worldTime.setTime(1, 17.5f);
    worldTime.setTimeScale(1.0f);
    worldTime.scheduleEvent({"dusk_bell", 18.0f, "world.dusk_started", "bell", true, 0});

    NPCController guideNpc(100);
    guideNpc.addScheduleEntry({18.0f, 22.0f, "evening_watch", {10.0f, 0.0f, 0.0f}});
    CompanionController companion(200);
    companion.setPosition({0.0f, 0.0f, 0.0f});

    // Wire events
    bool duskEventFired = false;
    bus.subscribe("quest.request_start", [&](const RuntimeEvent& ev) {
        quests.activateQuest(ev.payload, &bus);
    });
    bus.subscribe("world.relic_collected", [&](const RuntimeEvent& ev) {
        quests.onRuntimeEvent(ev, &bus);
    });
    bus.subscribe("reward.grant_item", [&](const RuntimeEvent& ev) {
        inv.addItem(items, ev.payload, 1);
        prog.addXp(150);
    });
    bus.subscribe("world.dusk_started", [&](const RuntimeEvent&) {
        duskEventFired = true;
    });

    // Execute Vertical Slice
    guideNpc.beginDialogue();
    check(dialogue.startDialogue("guide_intro", &bus), "Dialogue started");
    check(dialogue.getLocalizedBody(loc) == "Find the beacon relic before dusk.", "Localized dialogue body resolved");
    check(dialogue.selectChoice("accept", narrative, &bus), "Dialogue choice selected");
    guideNpc.endDialogue();

    check(narrative.getFlag("accepted_guide"), "Narrative variable updated");
    check(quests.getQuestState("q_beacon") == QuestState::Active, "Quest activated from dialogue event");

    // Objective completion -> Quest complete -> Item awarded + XP gained
    bus.publish({"world.relic_collected", 1, "beacon_relic"});
    check(quests.getQuestState("q_beacon") == QuestState::Completed, "Quest completed on objective event");
    check(inv.getItemCount("relic_amulet") == 1, "Reward item added to inventory");
    check(prog.getLevel() == 2 && prog.getXp() == 150, "Progression leveled up on quest XP");

    // Equip item -> Attribute changes
    check(equip.equip(items, "relic_amulet", attrs), "Equipped reward amulet");
    check(std::abs(attrs.getValue("Focus") - 25.0f) < 0.01f, "Attribute updated by equipment modifier (10 + 15 = 25)");

    // World time advances to dusk -> World event + NPC schedule + Companion follow
    worldTime.update(1.0f, &bus); // 17.5 -> 18.5
    check(duskEventFired && worldTime.getDayPhase() == DayPhase::Dusk, "WorldTimeSystem transitioned to Dusk & fired event");
    guideNpc.update(worldTime.getHour(), 1.0f);
    check(guideNpc.getState() == NPCState::Schedule && guideNpc.getActiveActivity() == "evening_watch",
          "NPC reacted to world time schedule");
    companion.update({8.0f, 0.0f, 0.0f}, 1.0f);
    check(companion.getPosition().x > 3.0f, "Companion followed player");

    // Save -> Wipe -> Reload -> Verify
    const auto saveRoot = std::filesystem::temp_directory_path() / "sidera_m41_vertical_slice";
    std::filesystem::remove_all(saveRoot);
    SaveSystem saveSys(saveRoot);
    SaveGameData save{};
    save.slotId = "slot_m41";
    save.sceneName = "vertical_slice_scene";
    narrative.saveTo(save);
    quests.saveTo(save);
    inv.saveTo(save);
    equip.saveTo(save);
    prog.saveTo(save);
    worldTime.saveTo(save);
    check(saveSys.saveGame(save), "Vertical slice state saved via SaveSystem");

    // Wipe runtime state
    narrative.clear();
    quests.clearInstances();
    inv.clear();
    equip.unequip("Neck", attrs);
    prog = ProgressionSystem{};
    worldTime.setTime(1, 8.0f);

    // Reload
    SaveGameData loaded{};
    check(saveSys.loadGame("slot_m41", loaded), "Vertical slice save loaded");
    narrative.loadFrom(loaded);
    quests.loadFrom(loaded);
    inv.loadFrom(loaded);
    equip.loadFrom(loaded, items, attrs);
    prog.loadFrom(loaded);
    worldTime.loadFrom(loaded);

    check(narrative.getFlag("accepted_guide"), "Reloaded narrative flag preserved");
    check(quests.getQuestState("q_beacon") == QuestState::Completed, "Reloaded quest state preserved");
    check(inv.getItemCount("relic_amulet") == 1, "Reloaded inventory preserved");
    check(equip.getEquippedItem("Neck") == "relic_amulet" && std::abs(attrs.getValue("Focus") - 25.0f) < 0.01f,
          "Reloaded equipment & attributes preserved");
    check(prog.getLevel() == 2 && prog.getXp() == 150, "Reloaded progression preserved");
    check(worldTime.getDay() == 1 && worldTime.getDayPhase() == DayPhase::Dusk, "Reloaded world time preserved");

    std::filesystem::remove_all(saveRoot);
}

} // namespace

int main() {
    std::cout << "=== SIDERA Milestone 41 — Reusable RPG / Adventure Framework Tests ===\n";
    testFrameworkRegistry();
    testCharacterAndCamera();
    testCombatSystem();
    testReferenceVerticalSliceAndPersistence();

    if (g_failures > 0) {
        std::cerr << "\n[FAILED] " << g_failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "\n[ALL PASSED] Milestone 41 Reusable RPG / Adventure Framework verified.\n";
    return 0;
}
