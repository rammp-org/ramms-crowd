# RammsCrowd

Unreal Engine 5.7 plugin for Mass Entity-based NPC crowd simulation, built for
robotic assistive technology simulation (RAMMS-Sim).

- **Spawning** — `ARammsCrowdSpawner` (data-driven `AMassSpawner`) with box-area and
  ZoneGraph-lane spawn-point generators and per-kind `URammsCrowdAgentProfile` data assets.
- **Locomotion** — composes the engine Mass stack (ZoneGraph lane following, NavMesh
  open-area wander, steering, ORCA avoidance, StateTree behaviors).
- **Player/robot reactions** — extensible, data-driven perception + reaction framework:
  proxemics zones (aware/react/startle), closing-speed startle rule, per-NPC-kind
  reaction profiles and StateTree reaction subtrees (glance, yield, back away, flee).
- **Sensor-realistic representation** — near-agent skeletal-mesh actors with collision
  (correct ToF/sonar/camera returns), instanced static meshes at distance.

Requires the (Experimental) engine plugins: MassGameplay, MassAI, MassCrowd, StateTree,
ZoneGraph, ZoneGraphAnnotations.

See `doc/SETUP.md` for project configuration, content authoring, and level setup.
