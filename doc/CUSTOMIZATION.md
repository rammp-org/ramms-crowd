# RammsCrowd Customization Guide

How to change what the crowd looks like and how it behaves. Everything here is data
authoring — the design goal is that a new NPC kind (different look, different reactions)
requires zero plugin C++.

Prerequisite reading: [SETUP.md](SETUP.md) for what each asset is.

## Modifying the people (appearance)

### Appearance variety (City Sample characters)

`BP_CrowdPerson_CS` subclasses City Sample's `BP_CrowdCharacter`, which exposes a
parametric appearance system in the class defaults:

- **`RandomOptions` = true** (the reference setup): every spawned actor re-rolls
  gender, body weight, head (6), skin (6), hair style/color, outfit, fabric, pattern,
  accessory, and height scale in its construction script. Pooled actors keep their look
  while alive, so nearby pedestrians are visually stable for sensor work.
- **Pinned looks**: set `RandomOptions` = false and pick the indices (`Skeleton`,
  `Body Shape`, `HeadIndex`, `OutfitIndex`, ...) in a *subclass* of `BP_CrowdPerson_CS`.
  Useful for uniformed kinds (e.g. a worker crowd): one subclass per look, one entity
  config per subclass, mixed via spawner `CrowdProfiles` proportions.

### Swapping the people entirely (own meshes / MetaHumans)

Any skeletal mesh works. Recipe (this is exactly how the mannequin-based
`BP_CrowdPerson` fallback is built):

1. Plain `AActor` blueprint: capsule (blocks Visibility+Camera, QueryOnly) +
   `USkeletalMeshComponent` (your mesh, RT-visible, no collision) +
   `URammsCrowdAnimComponent`. **Not** `ACharacter` — CharacterMovement fights Mass
   transform writes.
2. Mesh facing: meshes authored facing +Y (UE mannequin, MetaHumans) need yaw −90 —
   on the mesh component if it's a child, or via the agent trait's
   `RepresentationActorYawOffsetDegrees` if the mesh is the actor root.
3. Point the entity config's Mass Crowd Visualization `HighResTemplateActor` at it.
4. Animation: either give the anim component a locomotion blend space for the mesh's
   skeleton (preferred, below), or just assign `IdleAnim`/`WalkAnim` sequences +
   `WalkAnimReferenceSpeedCmS` for hard-switching playback.

MetaHumans: use the optimized/runtime export preset (hair cards — strands are also
unreliable for ray-traced sensor views), retarget idle/walk onto the MetaHuman skeleton
once with the IK Retargeter, then follow the recipe. Budget 2–4 distinct MetaHumans and
keep the `LODMaxCount` cap in mind.

### Animation tuning (`URammsCrowdAnimComponent`)

The component reads Mass data each tick (speed, travel direction relative to facing,
gait variation) and drives the mesh — no AnimBP needed:

| Property | Effect |
|---|---|
| `LocomotionBlendSpace` | Direction × Speed blend space, fed live. The shipped `BS_CrowdLocomotion` covers ±180° at walk speed plus idle and quick-walk rows |
| `IdleAnim` / `WalkAnim` / `WalkAnimReferenceSpeedCmS` | fallback pair when no blend space; ref speed = ground speed the walk was authored at (measure it — see scripted authoring) |
| `GaitVariation` (via agent trait) | per-agent play-rate jitter and phase desync (±half the value) |
| `ForceLOD0/1/2DistanceCm` | distance clamp for the City Sample LODSync (arm bones vanish at mesh LOD 2+); raise to trade fidelity for GPU headroom |
| `DirectionAngleDegrees`, `MassSpeed`, `bMassIsMoving`, `LookAtDirection` | BlueprintReadOnly outputs — if you later build a real AnimBP (blend smoothing, head aim, motion matching), read these; the component auto-yields to any mesh with an AnimBP class set |

The blend space itself is a normal asset — open `BS_CrowdLocomotion` to retune axis
smoothing or swap sample clips. If you *script* blend-space edits, call
`unreal.RammsCrowdAnimAssetLibrary.rebuild_blend_space(bs)` afterward: without the
resample, the asset evaluates to reference pose ("A-pose") with frozen time. Note the
runtime triangulation of script-authored blend spaces does not survive editor restarts
at all — `URammsCrowdAnimComponent` therefore self-heals its blend space once per
editor session before first playback (editor-only; cooked builds bake blend spaces
during packaging).

## Modifying the behaviors

### Tuning reactions (no editor restart, no recompile)

All proxemics live in `DA_Reaction_*` assets (`URammsCrowdReactionProfile`): zone radii,
exit hysteresis, the closing-speed startle threshold, startle cooldown, back-away
distance and speed scales. Edit between PIE runs; verify with
`ramms.crowd.DebugStimulus 1`.

Walking speed/mix: Mass Movement trait (speed + variance), and the Stand duration in
the StateTree controls the walk/pause rhythm.

### Changing behavior structure (StateTree)

`ST_Pedestrian` is a normal StateTree asset — open it in the editor. The shipped C++
vocabulary to compose from:

| Node | Kind | Does |
|---|---|---|
| `RAMMS Stimulus` (`FRammsStimulusEvaluator`) | evaluator | exposes Zone/Distance/ClosingSpeed/StimulusEntity/AwayDirection for binding |
| `RAMMS Stimulus Zone` (`FRammsStimulusZoneCondition`) | condition | zone ≥ MinZone (invertible) |
| `RAMMS Closing Speed` (`FRammsClosingSpeedCondition`) | condition | approach speed ≥ threshold |
| `RAMMS Back Away` (`FRammsBackAwayTask`) | task | retreat along AwayDirection while facing the stimulus, lane-safe distances |
| engine: ZG Find Wander Target / Path Follow / Stand, LookAt, Find Escape Target, NavMesh tasks | tasks | locomotion + gaze building blocks |

Recipes:
- **Yield (robot-like kinds)**: state gated on `Zone ≥ React` with `ZG Stand`
  (duration 0) + `Mass LookAt` (target bound to the evaluator's StimulusEntity);
  transition out when the condition clears (zone-change signals re-tick the tree).
- **Flee (pets/animals)**: state gated on `Zone ≥ Startle` with
  `ZG Find Escape Target` → `ZG Path Follow`; pair with the stimulus source's
  `bTriggerLaneDisturbance` so fast robot motion marks lanes as dangerous.
- **Rules**: reaction states must *interrupt* path-follow as siblings (never parallel —
  single writer for the move target). Interrupt transitions on Wander/Stand should be
  OnTick + a zone condition; the perception processor's signals wake the tree, so
  steady state costs nothing.

### Adding a new NPC kind (data only)

1. Duplicate `DA_Reaction_Person` → tune proxemics.
2. Duplicate `ST_Pedestrian` → recompose reaction states from the vocabulary above.
3. Duplicate the entity config → swap reaction profile, StateTree, meshes
   (representation template actor), movement style, `ActorKind`.
4. Create a `URammsCrowdAgentProfile` pointing at the config; add it to the spawner's
   `CrowdProfiles` with a proportion (e.g. Person 0.8, Robot 0.2).

### Extending the vocabulary (C++)

- New tasks/conditions: subclass `FMassStateTreeTaskBase` / `FMassStateTreeConditionBase`
  in *your* module (mirror `RammsBackAwayTask` — note `Link()`, `GetDependencies()`, and
  the fixed StateTree wake-signal list: custom signals must be accompanied by
  `UE::Mass::Signals::NewStateTreeTaskRequired`).
- New per-kind parameters without plugin edits: define a `FMassConstSharedFragment`
  struct, add an instance to the reaction profile's `ExtensionParams` — the reaction
  trait bakes it as a const-shared fragment your custom nodes can require.
- New stimulus sources: add `URammsStimulusSourceComponent` to any actor (vans, other
  robots), or push snapshots into `URammsStimulusSubsystem` directly for virtual
  stimuli.

## Seated people (`URammsSeatComponent`)

Puts a posed skeletal-mesh occupant into any seat — the wheelchair, benches,
vehicle seats. Add the component to the actor, position it at the seat surface
(component transform = seat origin, X forward / Z up), done: on BeginPlay it
spawns the occupant, attaches it, disables its collision/physics, and applies a
parametric seated pose.

- **Occupants**: City Sample crowd characters by default (optionally
  appearance-randomized per spawn; soft-referenced, so a missing pack logs a
  warning and leaves the seat empty instead of failing), or any skeletal mesh
  via `ExplicitMesh` mode.
- **The pose is data**: `PoseBoneOffsets` is a bone-name → rotation map applied
  over the reference pose (`URammsSeatedPoseAnimInstance` — no anim graph, no
  authored sit animation needed, skeleton-agnostic since missing bones skip).
  Tune it live in the details panel; changes re-apply immediately, including in
  PIE. Different seats can carry different poses.
- **Axis semantics** (UE-standard skeletons — City Sample, mannequin; bones have
  X along the bone): **Yaw = flexion/bend** (hip, knee, elbow, spine fwd/back),
  **Pitch = lateral swing** (arm abduction, spine side-lean), **Roll = twist
  about the bone's own long axis**. A pure Roll moves no child joint — it looks
  exactly like "the pose isn't applying", so if a limb won't bend, check that
  the offset isn't in Roll. Signs are identical on both body sides (mirrored
  bone frames).
- **Editor preview**: `Spawn Occupant` / `Clear Occupant` / `Apply Pose` buttons
  on the component work without PIE (preview occupants are transient — never
  saved into the map).
- Upgrade path: hand/foot IK fitting (armrests, joystick, footplates) can layer
  on later; the pose map remains the base layer.

## Scripted authoring (Python / Remote Control)

Everything above can be driven headlessly — the plugin's editor module ships utilities
for the pieces the stock Python API can't reach:

| Call | Purpose |
|---|---|
| `unreal.RammsCrowdStateTreeLibrary.create_pedestrian_wander_state_tree(path, name)` | build + compile the baseline pedestrian StateTree |
| `unreal.RammsCrowdAnimAssetLibrary.rebuild_blend_space(bs)` | validate/resample a blend space after scripted edits (mandatory) |
| `unreal.RammsCrowdZoneGraphLibrary.update_zone_shape(comp)` | recompute a `UZoneShapeComponent` after setting its points from script |
| `unreal.RammsCrowdZoneGraphLibrary.rebuild_zone_graph()` | trigger the Build ZoneGraph action |

The `Map_CrowdTest` zone network, entity configs, blend space, and character blueprints
in RAMMS-Sim were authored entirely through this path (Remote Control HTTP →
`ExecutePythonCommandEx`), so the project history doubles as a worked example. Useful
measurement trick: `unreal.AnimPoseExtensions` can extract root-motion speed from a walk
clip to get an exact `WalkAnimReferenceSpeedCmS`.
