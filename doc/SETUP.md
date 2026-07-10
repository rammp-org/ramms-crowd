# RammsCrowd Setup Guide

End-to-end guide for wiring the crowd system into a project (written against RAMMS-Sim,
UE 5.7). All C++ ships in this plugin; everything below is configuration and editor/data
authoring. `Map_CrowdTest` in RAMMS-Sim is the working reference for every section.

> The whole Mass/ZoneGraph stack is **Experimental** in UE 5.7. Pin your engine version;
> re-verify trait/task names after engine upgrades.

For customizing appearance, animation, and behavior after setup, see
[CUSTOMIZATION.md](CUSTOMIZATION.md).

## 0. Prerequisites

- Enable the `RammsCrowd` plugin (pulls in MassGameplay, MassAI, MassCrowd, StateTree,
  ZoneGraph, ZoneGraphAnnotations).
- **Character content** (for the high-detail representation): the reference setup uses
  Epic's free **City Sample Crowds** pack from Fab, installed at
  `/Game/CitySampleCrowd`. Any skeletal mesh + idle/walk animations work instead — see
  CUSTOMIZATION.md §"Swapping the people".

## 1. Project configuration

- `Config/DefaultPlugins.ini`
  - `[/Script/ZoneGraph.ZoneGraphSettings]` — tags: `Default(0)`, `Crowd(1)`, `Sidewalk(2)`,
    `Crossing(3)`, `OpenArea(4)`, `DensityStd(5)`; lane profile **PedestrianLanes**
    (2 x 150 cm lanes, opposite directions, tag mask Default|Crowd|Sidewalk|DensityStd).
  - `[/Script/MassCrowd.MassCrowdSettings]` — `CrowdTag=Crowd`, `CrossingTag=Crossing`,
    one `LaneDensities` entry (DensityStd, weight 1). **An empty LaneDensities breaks
    density-weighted wander selection.**
- `Config/DefaultMass.ini`
  - `[/Script/MassMovement.MassMovementSettings]` — movement styles `Walking`, `Jogging`,
    `RobotDrive`.
  - `[/Script/MassSimulation.MassSimulationSettings]` —
    `DesiredActorSpawningTimeSlicePerTick=0.006`. The engine default (1.5 ms) makes a
    40-agent crowd take ~1 s to swap from proxy meshes to actors on spawn.
- `Config/DefaultEngine.ini` — NavigationSystem `SupportedAgents` entry **Pedestrian**
  (radius 35, height 180); RecastNavMesh cell settings.

Verify in Project Settings: the Zone Graph, Mass Crowd, and Mass Movement pages show
these values.

## 2. NPC entity config (`/Game/NPCs/DA_MassEntityConfigAsset`)

One `UMassEntityConfigAsset` per NPC kind. The working Person stack (order does not
matter):

| Trait | Settings |
|---|---|
| **RAMMS Crowd Agent** (`URammsCrowdAgentTrait`) | Radius 35; ActorKind Person; GaitVariation 0.25; **RepresentationActorYawOffsetDegrees −90** (City Sample meshes — see gotcha below) |
| **RAMMS Crowd Reaction** (`URammsCrowdReactionTrait`) | Profile = `DA_Reaction_Person` |
| **RAMMS Representation Support** | (no settings) |
| Mass Movement | defaults (walk ~140 cm/s ±10%) or a movement style |
| Mass Steering | defaults |
| Mass Obstacle Avoidance | defaults |
| Mass Navigation Obstacle | (registers agents in the avoidance grid) |
| Mass ZoneGraph Navigation | LaneFilter AnyTags = **Crowd**; **QueryRadius ≥ max spawn-point-to-lane distance** (1000 works) |
| Mass Crowd Member | (adds `FMassCrowdTag` — required, see gotcha) |
| Mass LookAt | defaults (gaze support) |
| Mass StateTree | StateTree = `ST_Pedestrian` |
| Mass Smooth Orientation | defaults |
| Mass Distance LOD Collector | defaults |
| Mass Crowd Visualization | see §5 |

> **Gotcha — missing `AgentRadiusFragment`:** the engine movement/steering/nav/StateTree
> traits all require an agent radius but none of them provide it. **RAMMS Crowd Agent**
> does. Without it every entity fails template validation at spawn (the log names the
> fix).

> **Gotcha — visualization must be the Crowd variant:** the generic visualization traits
> (`UMassMovableVisualizationTrait` etc.) do NOT work: in UE 5.7 the base
> LOD/representation processors never auto-register, and the only registered
> implementations (in MassCrowd) exclusively process entities with `FMassCrowdTag` and
> LOD data filtered to it. Use **Mass Crowd Visualization** + **Mass Crowd Member**
> together, or entities spawn invisible (`Rep=None / LOD=Max`). The spawner logs a
> warning when a config is missing either.

Point `DA_CrowdAgentProfile.MassEntityConfig` at the config and fill the profile fields
(radius, walk speed range, display mesh, anim class, reaction profile) — profiles are
the human-facing authoring surface the spawner consumes.

## 3. StateTree `ST_Pedestrian`

**Fastest path:** run the shipped scaffolder (Editor Python / Blueprint):

```python
import unreal
st = unreal.RammsCrowdStateTreeLibrary.create_pedestrian_wander_state_tree("/Game/NPCs", "ST_Pedestrian")
```

It creates and compiles a Mass-schema tree:

```
Pedestrian (root)
├─ BackAway  [enter: RAMMS Stimulus Zone ≥ Startle] → RAMMS Back Away → Wander
├─ Wander    → ZG Find Wander Target → ZG Path Follow (target property-bound)
│              on succeeded → Stand; on failed → retry
│              on tick [zone ≥ Startle] → BackAway   ← woken by perception signals
└─ Stand     → ZG Stand (3 s) → Wander (+ same startle interrupt)
```

The utility is idempotent — rerunning rebuilds the tree from scratch — so treat it as a
starting point generator and extend the result in the StateTree editor (add per-kind
reaction states, yield/flee branches; see CUSTOMIZATION.md).

**Manual authoring notes** (if building by hand): schema must be **Mass Behavior**
(wrong schema = agents never move, silently); the path-follow task's `TargetLocation`
is a property ref that must be bound to the wander task's output; back-away must
interrupt path-follow as a *sibling state* — never run both (single writer for the move
target); the tree only ticks on signals — the perception processor signals zone
transitions, and RAMMS Back Away re-arms its own 0.25 s poll while running.

## 4. Reaction profiles (`/Game/NPCs/DA_Reaction_*`)

One `URammsCrowdReactionProfile` per kind; all proxemics/response tuning lives here
(editable live between PIE runs). Starting values:

| | Aware | React | Startle | StartleSpeed | Cooldown | BackAway | Notes |
|---|---|---|---|---|---|---|---|
| Person | 600 | 300 | 120 | 150 | 4 s | 150 cm | back away, resume |
| Robot | 800 | 400 | 150 | 9999 (never) | — | — | deterministic yield |
| Pet | 900 | 500 | 250 | 100 | 2 s | 300 cm | flees via escape lanes |

Semantics: an NPC classifies into Aware/React/Startle zones by distance to the nearest
stimulus source, with exit hysteresis; approaching faster than `StartleSpeed` inside the
React zone startles early (the "fast robot" rule); `Cooldown` suppresses repeat
startles. `ExtensionParams` accepts user-defined `FMassConstSharedFragment` structs so
custom StateTree tasks/conditions can read new parameters without touching plugin code.

## 5. Representation & sensors (`BP_CrowdPerson_CS`)

The high-detail representation actor. Reference implementation subclasses City Sample's
`BP_CrowdCharacter` (modular body/outfit/face assembly + appearance randomization) and
adds:

- **CollisionCapsule** (`UCapsuleComponent`, r=35 h=90 at Z+90): QueryOnly, **blocks
  Visibility and Camera** only — this is what the ToF/sonar **CPU LineTrace fallback**
  hits. Ignores everything else so it never fights physics.
- **RammsCrowdAnim** (`URammsCrowdAnimComponent`): drives locomotion animation from Mass
  data and manages mesh LOD (below). No AnimBP required.
- Skeletal meshes should be **Visible in Ray Tracing** (GPU sensor rays hit skinned
  geometry; skinned motion produces real camera motion vectors). Ensure
  `r.RayTracing.Geometry.SkeletalMeshes=1` when RT is on.

Animation: the anim component plays `BS_CrowdLocomotion` (a Direction × Speed blend
space over the City Sample locomotion set) as a single node and feeds it
(direction, speed) every tick — blended turns, starts/stops, backpedaling, with
per-agent phase/rate desync from `GaitVariation`. See CUSTOMIZATION.md to retune or
replace it.

On the entity config's **Mass Crowd Visualization** trait:

- `HighResTemplateActor` = `BP_CrowdPerson_CS`; LOD representation High =
  HighResSpawnedActor, Medium/Low = StaticMeshInstance (proxy mesh — a cube is fine),
  Off = None.
- LOD distances (reference values): actors to 25 m, proxies 25–60 m, culled at 120 m;
  `LODMaxCount` High = 60. Keep the actor band ≥ 2.5× your longest sensor range so
  representation never swaps inside sensor range.

> **Gotcha — City Sample mesh LODs strip arm bones:** at mesh LOD 2+ the crowd meshes
> remove arm/finger bones (their far crowd was designed for baked vertex animation), so
> arms freeze at reference pose ("A-pose") while legs animate — and the pack's LODSync
> component picks aggressive LODs. `URammsCrowdAnimComponent` clamps the LODSync by
> distance to the Mass viewer (`ForceLOD0/1/2DistanceCm`, defaults 12/30/60 m). If GPU
> cost is too high, raise `ForceLOD0DistanceCm` — LOD 1 keeps arms; only fingers go.

> **Gotcha — mesh facing:** MetaHuman-lineage meshes (City Sample, mannequin) are
> authored facing +Y. If the mesh is a child component, give it yaw −90 in the actor.
> City Sample's mesh IS the actor root, so the offset can't live there — set
> **RepresentationActorYawOffsetDegrees = −90** on the RAMMS Crowd Agent trait instead
> (applied by the actor-sync processor). Wrong/missing offset = agents "crab-walk"
> sideways.

Why spawned actors follow entities at all: the engine only teleports a representation
actor once, at the swap moment. `URammsCrowdActorSyncProcessor` (this plugin) pushes
entity transforms to Mass-owned representation actors every frame.

## 6. Player / robot setup (`BP_Mebot_Ramms`)

Two components on the pawn:

1. **MassAgent** (`UMassAgentComponent`) — Entity Config = `DA_MassConfig_PlayerAvatar`:
   - **RAMMS Player Avatar** (`URammsPlayerAvatarTrait`) — radius ~50, pill collider
     half-length ~55 (wheelchair footprint). Exists because the engine's sync traits
     require a capsule + CharacterMovement, which robot pawns don't have; the paired
     translator mirrors the pawn's transform/velocity into Mass every frame.
   - Mass Navigation Obstacle — NPCs steer around the robot.
   - Mass LookAt Target — NPC gaze can target the robot.
   - **No representation/LOD traits** — the real pawn already renders.
2. **RammsStimulusSource** (`URammsStimulusSourceComponent`) — pushes position/velocity
   snapshots into the perception registry (radius ~50). Multiple sources are supported
   (vans, other robots). `bTriggerLaneDisturbance` additionally fires ZoneGraph danger
   annotations when moving fast (for escape-lane fleeing).

## 7. Level authoring — see `Map_CrowdTest`

The map contains a complete example network (World Outliner folder **ZoneGraph**):
8 junction polygons + 10 spline segments forming a perimeter loop, a T-branch, a
central 3-way, and a corridor threading between obstacles.

1. **Paths**: `AZoneShape` (spline type), Lane Profile = **PedestrianLanes**, Tags =
   Crowd. Add interior points to shape around obstacles (see `ZS_Corridor_North`).
2. **Junctions**: `AZoneShape` (polygon type). Points of type **Lane Profile** are the
   connection slots — their rotation faces *into* the polygon; Sharp points fill out the
   boundary between connections (see any `ZS_Junction_*`). Snap spline endpoints onto
   the lane-profile points.
3. Build → **Build ZoneGraph**. Verify with viewport `Show > ZoneGraph` and
   `ai.debug.zonegraph.DisplayLaneTags 1`.
4. `ANavMeshBoundsVolume` over walkable ground for open-area (NavMesh) wandering;
   press `P` to inspect.
5. Spawner (`BP_CrowdSpawner` / `ARammsCrowdSpawner`): SpawnMode = **ZoneGraphLanes**
   spawns directly on the network; BoxArea spawns in a volume (agents then acquire
   lanes within the nav trait's QueryRadius). `CrowdProfiles` mixes multiple kinds by
   proportion.

## 8. Verification & debugging

The spawner self-diagnoses at spawn: it validates the entity template, logs the trait
stack / representation setup with warnings for the known trap configurations, prints
spawn locations, and 2 s after spawning audits live entities (`Rep=... LOD=...
ViewerDist=...`) — an invisible crowd is diagnosable from the log alone.

| Console command | Shows |
|---|---|
| `ramms.crowd.DebugStimulus 1` | proxemics rings around stimulus sources, zone-colored markers over NPCs |
| `ai.debug.zonegraph.DisplayLaneTags 1` | lane tag masks over the network |
| `ai.mass.avoidance.UseDrawDebugHelpers 1` | steering/avoidance forces |
| `mass.debug.RepresentationLOD 1` | per-entity representation LOD |
| Gameplay Debugger (`'`) → Mass, StateTree | per-agent entity + behavior state |

Behavior checklist (PIE, driving the robot):
- Agents stream along lanes, branch at junctions, pause (Stand) between legs.
- Crowd parts around the moving/parked robot and rejoins lanes.
- Slow approach startles only inside the Startle radius; **fast approach startles from
  the React radius** (closing-speed rule); startled agents back away *while facing the
  robot*, then resume; repeat startle within the cooldown is suppressed.
- Sensor pass: ToF/sonar depth on a pedestrian at 2–4 m correct on **both** GPU-ray and
  CPU-fallback paths; camera capture shows nonzero motion vectors on walking NPCs; no
  representation swap inside sensor range.
