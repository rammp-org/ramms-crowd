# RammsCrowd Setup Guide

End-to-end guide for wiring the crowd system in a project (written against RAMMS-Sim,
UE 5.7). C++ ships in this plugin; everything below is editor/data authoring.

> The whole Mass/ZoneGraph stack is **Experimental** in UE 5.7. Pin your engine version;
> re-verify trait/task names after engine upgrades.

## 1. Project configuration (done for RAMMS-Sim, listed for reuse)

- Enable plugins: `RammsCrowd` (pulls MassGameplay/MassAI/MassCrowd/StateTree/ZoneGraph/ZoneGraphAnnotations).
- `Config/DefaultPlugins.ini`
  - `[/Script/ZoneGraph.ZoneGraphSettings]` — tags: `Default(0)`, `Crowd(1)`, `Sidewalk(2)`,
    `Crossing(3)`, `OpenArea(4)`, `DensityStd(5)`; lane profile **PedestrianLanes**
    (2 x 150 cm lanes, opposite directions, tag mask Default|Crowd|Sidewalk|DensityStd).
  - `[/Script/MassCrowd.MassCrowdSettings]` — `CrowdTag=Crowd`, `CrossingTag=Crossing`,
    one `LaneDensities` entry (DensityStd, weight 1). **An empty LaneDensities breaks
    density-weighted wander selection.**
- `Config/DefaultMass.ini` — `[/Script/MassMovement.MassMovementSettings]` movement styles
  `Walking`, `Jogging`, `RobotDrive`.
- `Config/DefaultEngine.ini` — NavigationSystem `SupportedAgents` entry **Pedestrian**
  (radius 35, height 180); RecastNavMesh cell settings.

Verify in Project Settings: Zone Graph (tags/profiles), Mass Crowd, Mass Movement pages
show these values.

## 2. NPC entity config (e.g. `/RammsCrowd/DA_EntityConfig_Person`)

`UMassEntityConfigAsset` trait stack (order does not matter):

| Trait | Settings |
|---|---|
| **RAMMS Crowd Agent** (`URammsCrowdAgentTrait`) | Radius 35; ActorKind Person; GaitVariation ~0.25 |
| **RAMMS Crowd Reaction** (`URammsCrowdReactionTrait`) | Profile = `DA_Reaction_Person` |
| **RAMMS Representation Support** | (no settings) |
| Mass Movement (`UMassMovementTrait`) | Movement style **Walking**; DesiredSpeed ~130 with variations (e.g. ±25%, spread entries) |
| Mass Steering (`UMassSteeringTrait`) | defaults |
| Mass Obstacle Avoidance (`UMassObstacleAvoidanceTrait`) | defaults |
| Mass Navigation Obstacle (`UMassNavigationObstacleTrait`) | (makes agents avoid each other) |
| Mass ZoneGraph Navigation (`UMassZoneGraphNavigationTrait`) | LaneFilter: Any = Crowd; **QueryRadius >= max spawn-point-to-lane distance** (500+) |
| Mass Crowd Member (`UMassCrowdMemberTrait`) | (lane tracking/density) |
| Mass LookAt (`UMassLookAtTrait`) | defaults (gaze support) |
| Mass StateTree (`UMassStateTreeTrait`) | StateTree = `ST_Pedestrian` |
| Mass Smooth Orientation (`UMassSmoothOrientationTrait`) | defaults |
| Mass Distance LOD Collector (`UMassDistanceLODCollectorTrait`) | LOD distances: High 1500, Medium 4000, Low 10000 |
| Mass Movable Visualization (`UMassMovableVisualizationTrait`) | see §5 |
| *(Phase 7)* Mass NavMesh Navigation (`UMassNavMeshNavigationTrait`) | for open-area wander |

Per-kind configs (Robot/Pet/...) are copies with different movement style, reaction
profile, StateTree (or reaction subtree), and meshes.

**Point `DA_CrowdAgentProfile.MassEntityConfig` at this asset** and fill the new profile
fields (radius, walk speed range, display mesh, anim class, reaction profile) — they are
the human-facing authoring surface and validation reference.

## 3. StateTree `ST_Pedestrian` (schema: **Mass Behavior** — wrong schema = NPCs never move)

Create via Content Browser → Artificial Intelligence → StateTree, pick the **Mass
Behavior** schema (MassStateTreeSchema).

Evaluators (root):
- **RAMMS Stimulus** (`FRammsStimulusEvaluator`) — exposes Zone / Distance / ClosingSpeed / StimulusEntity / AwayDirection.

States (selector, first match wins):

1. **Reactions** (highest priority; per-kind linked subtree or inline states)
   - **Startle** — enter condition: `RAMMS Stimulus Zone >= Startle`
     - Task: **RAMMS Back Away** (`FRammsBackAwayTask`) — MaxDuration 2 s
     - On Succeeded → back to root selection.
   - **Yield** (Robot kind) — condition `Zone >= React`
     - Tasks: `FMassZoneGraphStandTask` (Duration 0 = until transition) + `FMassLookAtTask`
       (LookAtMode: LookAtEntity, TargetEntity bound to evaluator's StimulusEntity)
     - Transition out when `Zone >= React` is false (re-evaluated on zone-change signals).
   - **Glance** (Person kind) — condition `Zone >= Aware`, runs *alongside walking* is NOT
     possible (one active branch) — instead put `FMassLookAtTask` (Glance, Duration 1–2 s)
     as an additional task on the Wander state gated by the condition, or as a brief
     interrupt state.
   - **Flee** (Pet/WildAnimal) — condition `Zone >= Startle` (or annotation tag present)
     - Tasks: `FMassZoneGraphFindEscapeTarget` (DisturbanceAnnotationTag = Danger tag)
       → `FMassZoneGraphPathFollowTask` bound to its escape target output.
2. **Wander**
   - Tasks: `FMassZoneGraphFindWanderTarget` (density-weighted) → `FMassZoneGraphPathFollowTask`
     (bind TargetLocation to wander target output). On Succeeded → re-enter (loop).
3. **Stand**
   - Task: `FMassZoneGraphStandTask`, Duration randomized 2–8 s (bind a random param or
     use several stand states). Weight the Wander/Stand selection for behavior mix.
4. *(Phase 7)* **OpenAreaWander** — `FMassNavMeshFindReachablePointTask` →
   `FMassNavMeshPathFollowTask` → `FMassNavMeshStandTask`.

Notes:
- Back-away MUST interrupt path-follow as a sibling state — never run them in parallel
  (single writer for the move target).
- The tree only ticks on signals; the RammsCrowd perception processor signals on zone
  transitions, and RAMMS Back Away re-arms its own 0.25 s poll while running.

## 4. Reaction profiles (`/RammsCrowd/DA_Reaction_*`)

`URammsCrowdReactionProfile` assets per kind. Starting values:

| | Aware | React | Startle | StartleSpeed | Cooldown | BackAway | Notes |
|---|---|---|---|---|---|---|---|
| Person | 600 | 300 | 120 | 150 | 4 s | 150 cm | glance + back away |
| Robot | 800 | 400 | 150 | 9999 (never) | — | — | deterministic yield, no startle |
| Pet | 900 | 500 | 250 | 100 | 2 s | 300 cm | flees via escape lanes |

`ExtensionParams` accepts user-defined `FMassConstSharedFragment` structs — custom
StateTree tasks/conditions can read them without touching plugin code.

## 5. Representation & sensors (`BP_CrowdPerson`)

Plain **AActor** (NOT ACharacter — CharacterMovement fights Mass transform writes):
- Root: `UCapsuleComponent` r=35 h=90 — collision: QueryOnly, **block Visibility**
  (gives the ToF/sonar CPU LineTrace fallback a correct hit), ignore Pawn/Vehicle.
- `USkeletalMeshComponent` — mesh from the kind's profile (placeholder: engine Quinn/Manny
  mannequin); **Visible in Ray Tracing = true** (GPU sensor rays hit skinned geometry;
  skinned motion produces real motion vectors for camera capture); collision off.
- `URammsCrowdAnimComponent` (this plugin).
- AnimBP `ABP_CrowdPerson`: 1D idle↔walk blendspace driven by `MassSpeed`; play-rate
  jittered by `GaitVariation`; head AimOffset from `LookAtDirection`/`bHasLookAtTarget`.

On the entity config's **Mass Movable Visualization** trait:
- `HighResTemplateActor` = `BP_CrowdPerson`; LOD representation: High = HighResSpawnedActor,
  Medium/Low = StaticMeshInstance (assign a low-poly person static mesh — the placeholder
  cube works for testing), Off = None.
- LOD distances (on the LOD collector trait): High **1500** (2.5x ToF range so
  representation never swaps inside sensor range), Medium 4000, Low 10000.
- Cap HighRes actor count (~25) via the visualization LOD max-count settings.
- Ensure `r.RayTracing.Geometry.SkeletalMeshes=1` (default with RT on).

## 6. Player (MeBot) setup — `BP_Mebot_Ramms`

Add two components:
1. **MassAgent** (`UMassAgentComponent`) — Entity Config = `/RammsCrowd/DA_MassConfig_PlayerAvatar`:
   - **RAMMS Player Avatar** (`URammsPlayerAvatarTrait`) — radius ~50, pill collider,
     half-length ~55 (wheelchair footprint).
   - Mass Navigation Obstacle (`UMassNavigationObstacleTrait`) — NPCs steer around it.
   - Mass LookAt Target (`UMassLookAtTargetTrait`) — uncheck capsule-offset (no capsule);
     high priority so gaze prefers the wheelchair.
   - **No representation/LOD traits** — the real pawn already renders.
2. **RammsStimulusSource** (`URammsStimulusSourceComponent`) — radius 50; enable
   `bTriggerLaneDisturbance` in Phase 8 for fast-approach lane fleeing.

## 7. Level authoring (`Map_CrowdTest`)

1. Place `AZoneShape` actors (spline type) along walk routes; Lane Profile =
   **PedestrianLanes**. Snap endpoints together to connect; add a polygon-type ZoneShape
   at junctions so wander targets can branch.
2. Build → **Build ZoneGraph**. Verify: viewport `Show > ZoneGraph`;
   `ai.debug.zonegraph.DisplayLaneTags 1` shows the Crowd tag on lanes.
3. Add `ANavMeshBoundsVolume` over walkable ground; press `P` to see the green navmesh
   (agent "Pedestrian").
4. Crowd spawner (`BP_CrowdSpawner`): SpawnMode = ZoneGraphLanes, LaneTagFilter Any =
   Crowd, DesiredCount 50.

## 8. Phase-by-phase PIE verification

| Phase | Do | Verify |
|---|---|---|
| Walk lanes | traits 1–2 + StateTree Wander | cubes stream along lanes, branch at junctions. Gameplay Debugger (') → Mass + StateTree. Idle crowd? check lane tags, StateTree schema, ZG QueryRadius |
| Avoidance | avoidance traits + MeBot MassAgent | 150 agents: no interpenetration; crowd parts around driving wheelchair. `ai.mass.avoidance.UseDrawDebugHelpers 1` |
| Behavior mix | Stand branch, speed variations | mixed speeds; pause/resume; StateTree Debugger shows transitions |
| Reactions | stimulus source + reaction states | `ramms.crowd.DebugStimulus 1` rings + zone-colored markers; fast approach startles at range, slow only at 1.2 m; back-away then lane rejoin <= 2 s; Robot yields; `ai.debug.mass.SendLookAtPlayerRequestToAll 1` pre-validates gaze plumbing |
| Actors + sensors | BP_CrowdPerson + LOD | `mass.debug.RepresentationLOD 1`; skeletal actors <= 15 m, animated, speed-matched; ToF/sonar depth on a pedestrian at 2–4 m correct on GPU AND CPU paths; camera capture shows motion vectors on NPCs |
| NavMesh areas | NavMesh trait + OpenArea branch + box spawner | off-lane wander with avoidance; watch for lane/navmesh move-target jitter (fallback: split lane/open-area entity configs) |
| Scale | 500 agents, multi-profile, SimulationLOD trait | `stat unit`, `stat Mass` (sim <= 2–3 ms); HighRes cap respected; kinds at proportions |

## 9. Adding a new NPC kind (no C++)

1. Duplicate `DA_Reaction_Person` → tune proxemics/responses.
2. Duplicate `ST_Reaction_Person` subtree (or the whole per-kind tree) → recompose states
   from the shipped vocabulary (Back Away, Stand/yield, LookAt, escape-target flee...).
3. Duplicate the entity config → swap reaction profile, StateTree, movement style, meshes.
4. Create a `URammsCrowdAgentProfile` pointing at the config; add it to a spawner's
   `CrowdProfiles` with a proportion.
