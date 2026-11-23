# Milestone 41 — Reusable RPG / Adventure Framework (`v0.41.0`)

## Overview
Milestone 41 establishes a modular, reusable **RPG / Adventure Game Framework** layer on top of **SIDERA Engine Core**, preserving strict architectural separation (`SIDERA Engine Core → Reusable Game Frameworks → Individual Game Projects`). Engine Core has zero dependencies on any framework header or target, and the framework layer is toggled via `SIDERA_BUILD_FRAMEWORKS` (default `ON`).

## Implemented Modules (`src/framework/`)

| Module | Header | Key Capabilities |
| :--- | :--- | :--- |
| **Framework Core** | `src/framework/FrameworkCore.h` | `IGameFrameworkModule`, `FrameworkRegistry` (duplicate rejection, dependency validation, DFS cycle detection, topological init, reverse shutdown, per-module exception isolation). |
| **Character & Camera** | `src/framework/CharacterCamera.h` | `ThirdPersonCharacter` (`Idle`/`Walk`/`Run`/`Sprint`/`Crouch`/`Jump`/`Fall` modes integrating M37 `CharacterController` + M40 `InputManager` + M40 `AnimationStateMachine`), `ThirdPersonCamera` (pitch/yaw clamping, zoom, exponential follow smoothing, look-at matrix). |
| **Dialogue & Narrative** | `src/framework/DialogueNarrative.h` | `NarrativeState` (persistent flags/values + `SaveGameData` hooks), `DialogueGraph`/`DialogueNode`/`DialogueChoice`/`DialogueCondition`, `DialogueSystem` (M40 `LocalizationManager` & `RuntimeEventBus` integration). |
| **Quest & Objectives** | `src/framework/QuestSystem.h` | `QuestDefinition`, `QuestInstance`, `QuestState` (`Locked`, `Available`, `Active`, `Completed`, `Failed`, `Abandoned`), prerequisites, sequential/parallel objectives, `RuntimeEventBus` event progression, `SaveGameData` persistence. |
| **NPC & Companion** | `src/framework/NPCCompanion.h` | `NPCController` (`Idle`, `Patrol`, `Schedule`, `Interact`, `Dialogue`, world-time schedule evaluation), `CompanionController` (`Follow`, `Wait`, `ReturnToPlayer`). |
| **Inventory, Equipment, Attributes & Progression** | `src/framework/InventoryProgression.h` | `ItemRegistry`, `ItemDefinition` vs `ItemInstance`, transactional `InventoryContainer` (add/remove/split/capacity/stacking), `EquipmentManager`, `AttributeSet` (`Flat`/`AdditivePercent`/`Multiplicative` + clamping), `ProgressionSystem` (XP/levels), full `SaveGameData` persistence. |
| **Optional Combat & World Events** | `src/framework/CombatAndWorldEvents.h` | Optional `CombatSystem` (abilities, damage types, resistances, cooldowns, DoT status effects), `WorldTimeSystem` (day/hour clock, `DayPhase`, scheduled & repeating daily world events via `RuntimeEventBus`, persistence). |

## Verification
- **Automated Framework & Vertical Slice Suite (`tests/test_rpg_framework.cpp`)**:
  - Passed in both **Release** and **Debug** (0 warnings, 0 failures).
  - Covers `FrameworkRegistry` (dependencies, cycle detection, topological init/reverse shutdown, failure isolation), `ThirdPersonCharacter` + `ThirdPersonCamera`, optional `CombatSystem`, and the full end-to-end **Reference Vertical Slice**:
    `InteractionSystem → DialogueSystem (localized) → NarrativeState → QuestManager Activation → Objective Event → Item Award → InventoryContainer → ProgressionSystem Level-Up → EquipmentManager → AttributeSet Change → WorldTimeSystem Dusk Event → NPCController Schedule + CompanionController Follow → SaveSystem Save → Runtime Wipe → SaveSystem Reload → Full State Verification`.
- **Regressions & GPU Smoke Matrix**:
  - M29–M40 regression test suites (`test_runtime_systems` through `test_advanced_post_processing`) and 6-combination GPU smoke matrix (`forward` / `deferred` / `forward_plus` × `Release` / `Debug`) verified.

## Known Limitations
- As in M39.2/M40, live Vulkan 2D HUD/UI rasterization (`UISystem` draw-command rasterizer) and Dear ImGui interactive Editor viewport/panel rendering remain pending; framework runtime and vertical slice verification is executed via automated C++ integration tests.
