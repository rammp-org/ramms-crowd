# RammsCrowd

Unreal Engine 5.7 plugin for Mass Entity-based NPC crowd simulation, built for
robotic assistive technology simulation (RAMMS-Sim): realistic pedestrian crowds that
walk the environment, react to each other, and react to robots — with sensor-honest
representation for camera/ToF/sonar simulation.

## What it provides

- **Spawning** — `ARammsCrowdSpawner` (data-driven `AMassSpawner`) with box-area and
  ZoneGraph-lane spawn generators, multi-kind mixing by proportion via
  `URammsCrowdAgentProfile` data assets, and extensive self-diagnosis logging
  (template validation, known-trap warnings, post-spawn entity audits).
- **Locomotion** — composes the engine Mass stack (ZoneGraph lane following, steering,
  avoidance, StateTree behaviors) and fills its gaps: the agent-radius trait the engine
  traits require, a Mass→actor transform sync for spawned representation actors, and a
  scaffolded pedestrian StateTree (wander / stand / startle-back-away).
- **Perception & reactions** — a stimulus registry (robots, vehicles, virtual sources)
  feeding a batch proxemics processor: Aware/React/Startle zones with hysteresis, a
  closing-speed startle rule, cooldowns, and signal-driven StateTree wake-ups (steady
  state costs nothing). Per-kind tuning in `URammsCrowdReactionProfile` assets;
  extensible via `FInstancedStruct` extension params and custom StateTree nodes.
- **Player/robot integration** — avatar trait + translator that mirrors any pawn into
  Mass (no capsule/CharacterMovement required — built for robot pawns) so crowds avoid
  and look at it; a stimulus source component makes it something to react to.
- **Representation & animation** — near agents are real skeletal-mesh actors (capsule
  for CPU sensor traces, RT-visible meshes for GPU rays, real motion vectors);
  `URammsCrowdAnimComponent` drives locomotion animation straight from Mass data
  (Direction × Speed blend space, per-agent gait desync, distance-managed mesh LODs) —
  no AnimBP required, City Sample crowd characters supported out of the box.
- **Scripted authoring** — editor utilities (StateTree scaffolding, blend-space
  resampling, ZoneGraph shape/build helpers) that make the whole system authorable via
  Python / Remote Control.
- **Self-bootstrapping content** — large marketplace character packs stay out of git;
  a startup script validates the (free) City Sample Crowds install, auto-migrates it
  to the expected path, and degrades gracefully to proxy meshes when it is absent.
- **Traffic** — lightweight Mass traffic on ZoneGraph vehicle lanes: kinematic cars
  that keep their distance, stop at signals and all-way stops, and yield to pedestrians
  and the robot; one signal clock for the level that also opens and closes the crowd's
  street crossings. See [Traffic](#traffic).

Requires the (Experimental) engine plugins: MassGameplay, MassAI, MassCrowd, StateTree,
ZoneGraph, ZoneGraphAnnotations.

## Traffic

Cars on generated streets (RammsWorldGen's `traffic.py` lays the lanes and places the
actors; any level with the same ZoneGraph tags works). It is not CitySample's
MassTraffic: no lane changes, no parking manoeuvres, no turning wheels.

| Piece | What it does |
|---|---|
| ZoneGraph lanes | Lanes tagged **Vehicle** (profiles `VehicleLanes` / `VehicleLanesNarrow` in ramms-sim's `DefaultPlugins.ini`: two-way, right-hand, lane 0 forward on the right). Intersections are polygon shapes whose lanes also carry **Intersection** (stop or signal control) and **Signalized** (run by the signal clock); a polygon without them is a plain bend. The vehicle lanes carry no other bit, so they never link to pedestrian lanes. Tag names are settings on the spawner. |
| `URammsTrafficSubsystem` | The vehicle lane graph (lanes, next lanes, intersections, the intersection lanes that cross, merge or pass within 2.2 m of each other), rebuilt when ZoneGraph data comes or goes; the signal clock registry. |
| `ARammsTrafficSpawner` | Spawns `VehicleCount` (or `DensityPerKm`) Mass vehicles on the road lanes when play begins, `MinSpawnGap` apart and `MinSpawnDistance` from the player and every PlayerStart, each with a car from `CarMeshes` (by weight) and its own desired speed around `Driving.SpeedLimit` (1000 cm/s), all from `Seed`. |
| `URammsTrafficVehicleTrait` | The vehicle archetype: transform, `FRammsTrafficVehicleFragment` (lane, next lane, distance, speed, size, stop and claim state, a random stream), `FRammsTrafficVisualFragment`, `FRammsTrafficVehicleTag`, and the shared `FRammsTrafficDrivingParams`. |
| `URammsTrafficVehicleProcessor` | Per frame on the game thread: a lane occupancy index, intersection claims, all-way stop arrivals, the crowd's pedestrians (now and `PredictSeconds` ahead) and the robot footprints (the player avatar entities, as for `URammsRobotClearanceProcessor`, else the player pawns' bounds); then each car's acceleration is the lowest of IDM on a free road, IDM behind its leader (its lane and the next two), its turn speed, its stop line, and IDM behind the nearest pedestrian or robot in its corridor (path ahead, half width + `CorridorMargin`). It never moves past a stop it has to make. Height and pitch follow the lanes, plus two axle ground traces near the player. |
| `ARammsTrafficVehicleActor` | One per car, moved kinematically by `URammsTrafficVisualProcessor`: the car mesh (movable: shadows, motion vectors, ray tracing; query-only collision of object type Vehicle, so CPU traces hit the mesh) and a physics-only box over its footprint (the Chaos robot bumps a solid kinematic shape). Collision switches off beyond `CollisionRadius` of the player. Tagged `seg:Car`. `GetVelocity` reports the car's speed. |
| `ARammsTrafficSignalClock` | One global phase (`FRammsSignalTiming`: X green, X amber, all red, Y green, Y amber, all red; Walk for the parallel crossing, then Flashing Don't Walk for `PedestrianClearanceSeconds`). Drives the signal heads through a material parameter collection (`SignalParameters`: `GreenX`, `AmberX`, `RedX`, `GreenY`, `AmberY`, `RedY`, `WalkX`, `HandX`, `WalkY`, `HandY`), and opens each crowd crossing lane (MassCrowd `CrowdTag` + `CrossingTag`) while its walkers have Walk (`UMassCrowdSubsystem::SetLaneState`). |
| `URammsCrossingGateProcessor` | Holds a pedestrian at the start of a closed crossing lane (zero desired speed, given back when it opens). Untouched where no lane carries the Crossing tag. |

Intersection rules: at a signalized intersection green goes, amber goes unless it can stop
with `AmberStopDeceleration`, red stops unless it can no longer stop with `MaxDeceleration`;
straight and right turns are protected, a left turn is permitted on green but waits for
oncoming straight and right-turning traffic within `LeftTurnGapSeconds`; no turns on red.
Elsewhere (or with no clock in the level) every intersection is an all-way stop: stand at
the line for `AllWayStopSeconds`, then go in arrival order among conflicting lanes. Always:
nobody enters while a car on a conflicting lane is in the intersection or has been let in, nor
when its exit has no room (no blocking the box); a car held only by other cars for
`MaxWaitSeconds` goes anyway. Cars stop for pedestrians and the robot anywhere in their
corridor, so turning cars yield at the crossings and nobody drives into the robot.

`ramms.Traffic.Debug 1` draws each car's speed and what it is yielding to (2: also its
corridor). The pure rules (IDM, stop lines, the signal cycle, turn classification) are tested
in `Ramms.Crowd.Traffic.*`.

Not yet: MuJoCo does not see the cars (they are Unreal kinematic bodies), one phase for every
signal (no per-intersection offsets), wheels do not turn.

## Documentation

- **[doc/SETUP.md](doc/SETUP.md)** — project configuration, the entity-config trait
  stack (with the non-obvious requirements spelled out), StateTree, reaction profiles,
  representation actors, robot/player setup, ZoneGraph level authoring, verification
  and debug commands.
- **[doc/CUSTOMIZATION.md](doc/CUSTOMIZATION.md)** — changing appearance (City Sample
  variety, swapping meshes, MetaHumans), animation tuning, reaction/behavior tuning,
  adding NPC kinds without C++, extending the StateTree vocabulary in C++, scripted
  authoring reference.

`Map_CrowdTest` in RAMMS-Sim is the maintained working example: a complete ZoneGraph
teaching network (perimeter loop, junction patterns, obstacle corridor), a configured
spawner, entity configs, and the robot pawn wired as avatar + stimulus source.
