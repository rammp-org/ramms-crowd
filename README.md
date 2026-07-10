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

Requires the (Experimental) engine plugins: MassGameplay, MassAI, MassCrowd, StateTree,
ZoneGraph, ZoneGraphAnnotations.

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
