# UE360Tram — TramSystem Plugin

Unreal Engine 5.7 C++ plugin implementing a server-authoritative, spline-driven tram and a
synchronized shared-observer rig for a 360° multi-display installation (12 portrait 4K
screens / 4 machines, ~15 ft circle).

This repository **is** the plugin, not a host project: drop its contents into a host
project's `Plugins/TramSystem/` folder to use it.

## Core architectural principle

The installation is **one distributed virtual observer**, not several independent
multiplayer cameras. Every machine (including the listen-server machine) evaluates the same
authoritative state through the same code path and differs only in which physical screens it
projects. Seam consistency between neighboring displays is the top priority — see
"Determinism model" below.

## Module layout

Single runtime module: `TramSystem`. No editor-only dependencies; safe in packaged builds.

## Status

- [x] **Phase 1 — Plugin skeleton & tram core** (this commit)
- [ ] Phase 2 — Network synchronization
- [ ] Phase 3 — Rider/view slots
- [ ] Phase 4 — Shared observer rig
- [ ] Phase 5 — Display geometry model
- [ ] Phase 6 — nDisplay/DisplayCluster production backend
- [ ] Phase 7 — Calibration & hardening

## Phase 1 contents

| Type | File | Responsibility |
|---|---|---|
| `ETramMovementState` | `TramTypes.h` | `WaitingForLaunch` / `Running` / `Paused` / `Stopped` |
| `FTramSplinePointMetadata` | `TramTypes.h` | Per-spline-point `TargetSpeedCms` (segment N→N+1). `< 0` means "use route default". |
| `FTramMotionState` | `TramTypes.h` | The compact authoritative snapshot (Objective 14): RouteId, ServerTimestamp, DistanceAlongSpline, CurrentSpeed, TargetSpeed, CurrentSegmentIndex, MovementState. |
| `TramMotionMath` | `TramMotionMath.h/.cpp` | Pure, UE-actor-independent closed-form kinematics: given (v0, target, accel/decel rate, time budget, distance budget) → time/distance/speed consumed. No side effects, no UObject dependency — reusable from automation tests. |
| `ATramSplineRoute` | `TramSplineRoute.h/.cpp` | Wraps a `USplineComponent` + per-segment speed metadata. Distance↔transform queries, segment lookups, closed-loop wraparound. Pure route data — no movement or networking knowledge. |
| `UTramMovementComponent` | `TramMovementComponent.h/.cpp` | Makes any Actor a tram. Holds one authoritative `FTramMotionState` anchor; every tick, re-evaluates current distance/speed as a deterministic function of `(anchor, SynchronizedServerTime - anchor.timestamp)`. `LaunchTram/PauseTram/ResumeTram/StopTram` publish a fresh anchor. |

## Determinism model (why this satisfies the seam-consistency requirement)

The component never integrates `DeltaTime` frame-over-frame. Instead:

1. The authority (server, once Phase 2 lands) holds one `FTramMotionState` "anchor".
2. **Every** machine — including the authority's own actor movement — computes current
   distance/speed by evaluating `Evaluate(anchor, Now - anchor.ServerTimestamp)`, a
   deterministic multi-segment closed-form walk (`TramMotionMath::AdvanceTowardTarget`
   applied per segment, handling segment-boundary target-speed changes).
3. The anchor is periodically re-published (`SnapshotPublishIntervalSeconds`) and immediately
   re-published on every discrete state change (Launch/Pause/Resume/Stop). This bounds
   extrapolation error and becomes the network replication point in Phase 2.

Because step 2 is pure math over shared inputs, two machines holding the *same anchor* and
querying at the *same synchronized time* compute bit-for-bit identical distance/speed — the
central invariant required by the acceptance tests.

**Synchronized time** is `AGameStateBase::GetServerWorldTimeSeconds()` — Unreal's built-in
mechanism for client/server clock-offset compensation — rather than a bespoke NTP-style
exchange. It already degrades correctly to plain world time with no networking active, so
Phase 1 code needs no rework in Phase 2 (Rule 14: prefer built-in Unreal systems).

## Documented assumptions (not fully specified by objectives.md)

- **Pause freezes instantly** (no coast-down to 0); **Resume** continues from the frozen
  speed toward the current segment's target. **Stop freezes instantly** at the current
  position and is terminal (no auto-resume; a fresh `LaunchTram` is required, which is only
  valid from `WaitingForLaunch`). This keeps the four states exactly as specified
  (`WaitingForLaunch/Running/Paused/Stopped`) rather than inventing a transient
  "decelerating-to-stop" state. Revisit if a graceful stop-to-zero is later required.
- The tram never reverses (speeds/rates are treated as non-negative).
- `ATramSplineRoute` is level-placed static data and is not itself replicated; only the
  `FTramMotionState` derived from it needs to travel over the network (Phase 2).

## Testing this phase

No engine is available in the environment that produced this commit, so it has not been
compiled against UE 5.7. Verify by dropping the plugin into a 5.7 project, placing an
`ATramSplineRoute` with a closed-loop spline and a couple of per-point speeds, adding a
`UTramMovementComponent` to a test Actor referencing that route, and calling `LaunchTram()`
(e.g. from a Blueprint or console). Expect the actor to accelerate along the spline, hold
each segment's target speed, and decelerate into speed changes.
