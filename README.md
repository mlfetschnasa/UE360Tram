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

- [x] **Phase 1 — Plugin skeleton & tram core**
- [x] **Phase 2 — Network synchronization**
- [x] **Phase 3 — Rider/view slots** (this commit)
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

## Phase 2 contents

`UTramMovementComponent` is now network-live:

- `CurrentSnapshot` is `UPROPERTY(ReplicatedUsing = OnRep_CurrentSnapshot)`. The server is the
  only writer (through `PublishSnapshot`, unchanged from Phase 1); every other machine receives
  updates through `OnRep_CurrentSnapshot`.
- **No Server RPCs** for `LaunchTram/PauseTram/ResumeTram/StopTram`. These are only ever
  invoked locally, on the listen-server operator's own machine (e.g. from a HUD button that
  only that machine's controller exposes) — `HasControlAuthority()` alone is correct and
  simpler than routing through an RPC that would need a valid owning connection it doesn't
  have (the tram Actor is level-placed, not player-owned).
- The owning Actor must set `bReplicates = true`; the component logs a warning on the server if
  it doesn't. The component also forces `bAlwaysRelevant = true` on its owner, since the tram
  is the one object every rider must always be able to see regardless of UE's distance-based
  relevancy culling.
- **Late joining** (Objective 9) falls out of normal UE property replication: a newly-relevant
  actor channel sends the current value of `CurrentSnapshot` as part of its initial bunch, so a
  late client's very first `OnRep_CurrentSnapshot` call already carries the tram's current
  state (Running, mid-route, at whatever distance/speed it currently has) — no special-case
  "catch-up" logic was needed. That first call is detected (`bHasReceivedAnySnapshot`) and
  adopts the state directly, skipping error measurement (there is no prior local prediction to
  compare against).
- **Correction handling** (Objective 15) lives entirely in `OnRep_CurrentSnapshot`, which only
  ever fires on non-authority machines:
  1. Evaluate the *old* anchor forward to the *new* anchor's timestamp → "predicted distance".
  2. `Error = NewSnapshot.Distance - PredictedDistance`.
  3. `|Error| <= CorrectionSnapThresholdCm` (default 3cm): adopt the new anchor directly, no
     visible change.
  4. `|Error| <= CorrectionSevereThresholdCm` (default 300cm): blend, over
     `CorrectionBlendDurationSeconds` (default 0.35s), from "where the old anchor's trajectory
     would be right now" to "where the new anchor's trajectory is right now" — both evaluated
     with the same deterministic function, so the blend is smooth without any machine needing
     to agree on the transient blend itself, only on the converged result.
  5. Otherwise: snap instantly and log a warning (a slow blend across hundreds of cm would look
     worse than a snap, and something is likely wrong with connectivity).
- Extrapolation is clamped to `MaxExtrapolationSeconds` (default 3s) with a throttled warning,
  guarding against wild projections if replication stalls or synchronized time jumps.
- A subtle late-join bug was caught and fixed while implementing this: `BeginPlay` must **not**
  reset `CurrentSnapshot` on non-authority machines. Actor `BeginPlay` order relative to initial
  property replication isn't guaranteed, so a client resetting its own already-correct
  (possibly `Running`) replicated state back to `WaitingForLaunch` would have silently broken
  late joining. Only the authority seeds the initial snapshot in `BeginPlay`; everyone else
  waits for replication.

## Phase 3 contents

Rider identity (a connection) and tram slot (a portion of the display circle) are kept
strictly separate, and slot state lives where other clients can see it - not only locally in
a controller:

| Class | File | Responsibility |
|---|---|---|
| `ATramPlayerState` | `TramPlayerState.h/.cpp` | Replicated `SlotIndex` per rider (`INDEX_NONE` = unassigned) plus a `OnTramSlotChanged` Blueprint event fired identically on server and clients. |
| `ATramGameState` | `TramGameState.h/.cpp` | Replicated `NumTramSlots` (configurable, default 4 - never assumed elsewhere). `IsSlotOccupied`/`FindFirstFreeSlot`/`GetOccupiedSlots` are derived live from `PlayerArray`, so occupancy can never drift from a separately-maintained list. |
| `ATramGameMode` | `TramGameMode.h/.cpp` | Server-only. `TryAssignTramSlot` is the one place slot decisions are made: releases any slot the rider already holds, validates a requested slot (range + occupancy), auto-falls-back to another free slot unless `bStrictSlotRequests` is set, and reports failure (no free slots) without corrupting state. Also wires `PlayerStateClass`/`GameStateClass`/`PlayerControllerClass` to the Tram classes, and frees a rider's slot implicitly on `Logout` (occupancy is derived live, so there is nothing extra to clean up). |
| `ATramPlayerController` | `TramPlayerController.h/.cpp` | Owns the Server RPC (`ServerRequestTramSlot`) since, unlike the level-placed tram Actor, a PlayerController has a real owning connection. `BeginPlay` resolves an initial desired slot from `-TramSlot=N` on the command line, then `[TramSystem] PreferredSlot=` in ini config, else requests automatic assignment. `RequestTramSlot(int32)` is `BlueprintCallable` for an in-game slot-picker UI (the widget itself is a host-project concern, out of plugin scope). |

The listen server's own local rider goes through the identical RPC path (a Server RPC called
from a locally-owned connection just executes in-process), so there is no special-cased host
camera/slot logic - satisfying Objective 24.

**Duplicate-slot prevention** falls out of the single-threaded game-tick execution model: slot
assignment always runs inside `TryAssignTramSlot` on the server, which re-checks occupancy at
the moment it decides, so two simultaneous requests cannot both win the same slot - the second
one sees it already taken and (by default) falls back automatically.

## Determinism model (why this satisfies the seam-consistency requirement)

The component never integrates `DeltaTime` frame-over-frame. Instead:

1. The authority (server) holds one `FTramMotionState` "anchor".
2. **Every** machine — including the authority's own actor movement — computes current
   distance/speed by evaluating `Evaluate(anchor, Now - anchor.ServerTimestamp)`, a
   deterministic multi-segment closed-form walk (`TramMotionMath::AdvanceTowardTarget`
   applied per segment, handling segment-boundary target-speed changes).
3. The anchor is periodically re-published (`SnapshotPublishIntervalSeconds`) and immediately
   re-published on every discrete state change (Launch/Pause/Resume/Stop). This bounds
   extrapolation error and is the network replication point (Phase 2).

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
- Default slot-conflict behavior favors automatic fallback over rejection
  (`bStrictSlotRequests = false`), per Objective 8's stated default; strict rejection is
  available as a per-installation toggle on `ATramGameMode`.
- `-TramSlot=N` and `[TramSystem] PreferredSlot=` are read once, at `PlayerController::BeginPlay`,
  on the locally-controlling machine only (`IsLocalController()`) - a remote client's command
  line/config is never visible to the server, so this must happen client-side and be sent up
  via RPC, not read server-side (Phase 3).

## Testing this phase

No engine is available in the environment that produced this commit, so it has not been
compiled against UE 5.7. Verify by dropping the plugin into a 5.7 project, placing an
`ATramSplineRoute` with a closed-loop spline and a couple of per-point speeds, adding a
`UTramMovementComponent` to a test Actor referencing that route, and calling `LaunchTram()`
(e.g. from a Blueprint or console). Expect the actor to accelerate along the spline, hold
each segment's target speed, and decelerate into speed changes.
