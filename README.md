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
| `URammsTrafficSubsystem` | The vehicle lane graph (lanes with copies of their polylines and widths, next lanes, intersections, the intersection lanes that cross, merge or pass within the widest car + 30 cm of each other -- 2.2 m for 1.9 m cars), the street crossings (where each pedestrian lane meets the traffic's lanes), rebuilt on the game thread when ZoneGraph data comes or goes (StateTree tasks on worker threads read an immutable copy of the street crossings); the crossings held for walkers each frame; the signal clock registry. Game and PIE worlds only. |
| `ARammsTrafficSpawner` | Spawns `VehicleCount` (or `DensityPerKm`) Mass vehicles on the road lanes when play begins, `MinSpawnGap` apart and `MinSpawnDistance` from the player and every PlayerStart, each with a car from `CarMeshes` (by weight) and its own desired speed around `Driving.SpeedLimit` (1000 cm/s), all from `Seed`. It ticks after the physics (no Mass processing then); asked to spawn while Mass is processing it waits for its next tick, and a despawn then goes through Mass's deferred commands. `Driving` values set out of range (Python, Blueprint) are clamped to the editor's ranges. |
| `URammsTrafficVehicleTrait` | The vehicle archetype: transform, `FRammsTrafficVehicleFragment` (lane, next lane, distance, speed, size, stop and claim state, a random stream), `FRammsTrafficVisualFragment`, `FRammsTrafficVehicleTag`, and the shared `FRammsTrafficDrivingParams`. |
| `URammsTrafficVehicleProcessor` | Per frame on the game thread it fills a `RammsTraffic::FTrafficFrame` (`Private/RammsTrafficRules.h`: the rules as plain data and functions, which the tests run over hand-made networks): a lane occupancy index, intersection claims, all-way stop arrivals, the crowd's pedestrians (now and `PredictSeconds` ahead) and the robot footprints (the player avatar entities, as for `URammsRobotClearanceProcessor`, else the player pawns' bounds); then each car's acceleration is the lowest of IDM on a free road, IDM behind its leader (its lane and the next two), its turn speed (once braking for it is due), its stop line (or the end of a lane that leads nowhere), and IDM behind the nearest pedestrian or robot in its way (`RammsTraffic::IsInTheWay`: in its path, or on the carriageway within half width + `CorridorMargin` of it). It never moves past a stop it has to make. Height and pitch follow the lanes, plus two axle ground traces near the player. |
| `ARammsTrafficVehicleActor` | One per car, moved kinematically by `URammsTrafficVisualProcessor`: the car mesh (movable: shadows, motion vectors, ray tracing; query-only collision of object type Vehicle, so CPU traces hit the mesh) and a physics-only box over its footprint (the Chaos robot bumps a solid kinematic shape). Collision switches off beyond `CollisionRadius` of the player. Tagged `seg:Car`. `GetVelocity` reports the car's speed. |
| `ARammsTrafficSignalClock` | One global phase (`FRammsSignalTiming`: X green, X amber, all red, Y green, Y amber, all red; Walk for the parallel crossing, then Flashing Don't Walk for `PedestrianClearanceSeconds`). Drives the signal heads through a material parameter collection (`SignalParameters`: `GreenX`, `AmberX`, `RedX`, `GreenY`, `AmberY`, `RedY`, `WalkX`, `HandX`, `WalkY`, `HandY`), and opens each crowd crossing lane (MassCrowd `CrowdTag` + `CrossingTag`) while its walkers have Walk (`UMassCrowdSubsystem::SetLaneState`). |
| `URammsCrossingGateProcessor` | Holds a pedestrian at the start of a closed crossing lane, or of one a car is over or about to cross (zero desired speed, given back when it opens); a car yielding to walkers holds just those who come onto the crossing after it began to, and whoever already waits for a car keeps waiting while any car holds the crossing (one who waited for the signal goes on Walk unless a car that was there first holds it). On a street crossing nobody is held beyond the edge of the traffic's lanes. Holds come from the last traffic frame only: a frame with nothing to drive publishes none, and the gate ignores a set more than 1 s old. It also warns once when crowd members walk on vehicle lanes. Levels without crossing-tagged lanes or traffic are untouched. |

Intersection rules: at a signalized intersection green goes, amber goes unless it can stop
with `AmberStopDeceleration`, red stops unless it can no longer stop with `MaxDeceleration`;
straight and right turns are protected, a left turn is permitted on green but waits for
oncoming straight and right-turning traffic that has been let in or will reach its line within
`LeftTurnGapSeconds` (never for another left turn, nor for a car standing at its line held by
something else), and a left turn that waited at the line goes on the amber and the all red after
it; no turns on red. Elsewhere (or with no clock in the level) every intersection is an all-way
stop: stand at the line for `AllWayStopSeconds`, then go in arrival order among conflicting
lanes. Each frame the entry decisions are made per intersection before anyone moves, protected
movements first, and claims are only taken within a stopping distance (at least 15 m) of the
line, and only by the first car at its line (a car queued behind one that has not been let in, or
stands, takes none). Always: nobody enters while a car on a conflicting lane is in the
intersection (its rear too) or has been let in, nor when its exit has no room beyond a slow queue
there, less the cars already let in to it (no blocking the box); a car let in that can no longer
stop (over its line, or too fast for `MaxDeceleration`) keeps going. A car held at its line by other
cars' rules for `MaxWaitSeconds` in all (time at a red light, in a queue or at the box does not
count) stops waiting for cars that are only let in: it still waits for any in the intersection, any
let in that can no longer stop, any oncoming car it would turn across that can no longer stop, and
room on its exit. Two intersections joined directly (no road lane between) are decided at their
boundary. A lane that leads nowhere ends in a stop; a car reaching it starts again elsewhere at
once when no player is within 40 m, else after standing there `MaxWaitSeconds`. A car that has just turned off a shared
entry stays the leader of the one behind it until their lanes have separated. Cars stop for
pedestrians and the robot in their way -- physically in their path, or on the carriageway within
`CorridorMargin` of it -- so turning cars yield at the crossings and nobody drives into the robot,
while a robot waiting on the sidewalk beside the road stops nobody; and walkers do not step onto a
crossing a car is over or about to cross (`CrossingHoldSeconds`, at least 3 m and the car's
comfortable stopping distance, at most 25 m: `URammsCrossingGateProcessor` holds them at its
start), so a car clearing the intersection after the lights changed is not trapped by the next
Walk. At a crossing, who was first goes first: a car driving on holds everyone at the kerb, a car
yielding to nothing but walkers holds those who step up after it began to and those already waiting
for a car (it goes once the walkers already crossing are across), a car stopped for anything else
holds only the crossings under it, and a car stopped for a walker who stands (held up by the crowd
waiting at the far kerb) stops holding newcomers after 2 s so the crowd can make way. Walkers
waiting at the kerb -- held, standing short of their crossing's first traffic lane or past its last
one, or standing on a sidewalk lane -- stop a car only where they physically stand in its path;
walkers on the crossing and anyone walking into the way are given the full margin. Walkers never end
a walk standing on a street crossing; and a car that has stood `MaxWaitSeconds` for a walker who does
not move drives on past that walker (only), with a warning naming the walker. Crowd members never
start on vehicle lanes: both crowd spawn generators skip points on (or nearest) lanes tagged
`ExcludeLaneTagName` (default `Vehicle`); give the entity config's ZoneGraph Navigation trait a
`LaneFilter` with `NotTags` = `Vehicle` as well (doc/SETUP.md).

`ramms.Traffic.Debug 1` draws each car's speed and what limits it (2: also its corridor).
`ramms.Traffic.Dump` logs every car (lane and whether it is a road or an intersection's turning
lane, distance, speed, acceleration, next lane and turn, the limiting term -- leader, signal,
stop sign, claim, box, left-yield, pedestrian, robot, turn, queue, dead end -- with the car behind it, the entry
decision and the car it waits for, claim, left-wait, deadlock and stop timers; for a pedestrian
limit the walker: entity, position, speed, lane, distance along it, signal and hold state, desired
speed and movement action), every intersection's claims and approaching cars, the held crossings,
and the walkers on street crossings who are held or stand still, to `LogRammsCrowd`;
`ramms.Traffic.DumpInterval N` repeats it every N s. `Ramms.Crowd.Traffic.*` tests the pure rules
(IDM, stop lines, the signal cycle, turn classification, who is in a car's way) and, over hand-made
networks, the frame's rules themselves: the deadlock breaker against committed cars, queued cars,
the box, all-way stop order, the permitted left on the amber, walkers and the robot in a car's way,
dead ends and joined intersections, and the crossing gate.

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
