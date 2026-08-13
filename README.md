# UE360Tram — TramSystem Plugin

Unreal Engine 5.7 C++ plugin implementing a server-authoritative, spline-driven tram and a
synchronized shared-observer rig for a 360° multi-display installation (12 portrait 4K
screens / 4 machines, ~15 ft circle).

This repository **is** the plugin, not a host project: drop its contents into a host
project's `Plugins/TramSystem/` folder to use it.

**Setting it up in a level for the first time?** See [SETUP.md](SETUP.md) - GameMode wiring,
which actors go in the level and how they reference each other, and how to run a first
multi-window test.

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
- [x] **Phase 3 — Rider/view slots**
- [x] **Phase 4 — Shared observer rig**
- [x] **Phase 5 — Display geometry model** (this commit)
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

## Phase 4 contents

`ATramViewRig` is the single shared virtual observer (Objective 10) - the "one distributed view
rig" the whole architecture is built around. Every rider derives its final view from this one
actor's evaluation; no per-machine independent camera exists anywhere in the plugin.

| Type | File | Responsibility |
|---|---|---|
| `ETramLookAxisMode` | `TramTypes.h` | `Disabled`/`YawOnly`/`PitchOnly`/`YawPitch`/`YawPitchRoll` (no dedicated `RollOnly`, per objectives). |
| `FTramLookRotationState` | `TramTypes.h` | The look-rotation analogue of `FTramMotionState`: `{StartRotation, TargetRotation, TransitionStartServerTime, TransitionDurationSeconds}`, evaluated everywhere via `Slerp` at synchronized time rather than any per-client interpolation. |
| `TramSyncTime` | `TramSyncTime.h/.cpp` | Factored out of `UTramMovementComponent` once a second consumer (`ATramViewRig`) needed the identical synchronized-time lookup. |
| `ATramViewRig` | `TramViewRig.h/.cpp` | Composes `Tram Transform -> Smoothed Observer Base Orientation -> Shared Look Rotation` into `GetSharedObserverTransform()`. Owns rotation-follow smoothing and the shared operator look rotation. |

**Rotation-follow smoothing (Objective 11) needs no new replicated state at all.** The tram's
raw heading is already a deterministic function of shared state (route + the Phase 2
replicated anchor + synchronized time), so every machine already computes it identically with
zero jitter. "Smoothing" is therefore just a pure fixed time delay applied to that already-
deterministic function: `Smoothed(t) = RawHeading(t - RotationFollowLagSeconds)`, evaluated via
the new `UTramMovementComponent::GetAuthoritativeTransformAtServerTime(T)` (added this phase -
same evaluation machinery as Phase 1/2, just queryable at an arbitrary time, not only "now").
On straight track this is indistinguishable from the raw heading (nothing is changing); through
a turn, the camera visibly lags and eases back once heading stabilizes - the desired cinematic
effect - with no extra state, no extra replication, and by construction identical on every
machine. `GetAuthoritativeTransformAtServerTime` deliberately ignores this machine's own
transient correction blend (Phase 2), so the smoothing input is bit-identical across machines
even during a rare correction event; only the position-only blend itself is allowed to be
briefly client-local.

**Fixed during testing:** `GetAuthoritativeTransformAtServerTime`'s query time
(`Now - RotationFollowLagSeconds`) regularly fell *before* `CurrentSnapshot`'s own timestamp -
every periodic re-anchor (`SnapshotPublishIntervalSeconds`, default 0.5s) resets that timestamp
to "now", and the default lag (0.4s) is nearly as large as that interval, so the query landed
in the past relative to the brand-new anchor for roughly 80% of every republish cycle. The
original code clamped that to "frozen at the instant of re-anchor," which produced a visible
freeze-then-snap pattern at a ~2Hz cadence - identical on every machine (same server-timed
republish schedule), which is why it looked like consistently jerky rotation rather than
per-client jitter. Fixed by retaining the anchor `CurrentSnapshot` held immediately before its
most recent update (`PreviousSnapshotForHistory`, set from `PublishSnapshot` on the authority
and from `OnRep_CurrentSnapshot`'s `OldSnapshot` parameter everywhere else) and answering a
backward-looking query against it when the query falls in its range instead of clamping. This
correctly covers any lag up to roughly one full publish interval; a `RotationFollowLagSeconds`
much larger than `SnapshotPublishIntervalSeconds` would still hit the old clamped behavior for
the portion of the query beyond that single interval of retained history.

**Shared operator look rotation (Objective 12)** reuses the exact "anchor + deterministic
evaluation" pattern from tram motion, applied to rotation instead of distance: the server
accumulates mouse deltas (only on axes enabled by the current `LookAxisMode`, other axes
untouched rather than reset) into a local `DesiredLookRotator`, and every `LookPublishIntervalSeconds`
publishes a new Slerp transition that continues from wherever the *previous* transition is
currently evaluated (no pop between updates). `ApplyOperatorLookInput`/`SetLookAxisMode` use
the same "no Server RPC, `HasAuthority()` is enough because it's only ever called locally by
the listen-server operator's own process" reasoning as `UTramMovementComponent`'s commands.
`LookAxisMode` itself is replicated (Objective 26 wants "active look mode" as a diagnostic on
every machine) even though only the server's own accumulation logic depends on it.

**Composition order** (documented per the "document transform multiplication order" rule):
`ObserverLocation = TramTransform.TransformPosition(ObserverOffset.Location)` - the observer
rides rigidly with the tram body, unaffected by rotation smoothing. `FinalRotation =
SmoothedBaseRotation * ObserverOffset.Rotation * SharedLookRotation` - the fixed mounting
offset and rotation-follow smoothing apply first, the operator's shared look is layered on top.

Not yet built: an actual input-binding/HUD widget wiring mouse movement to
`ApplyOperatorLookInput` and slot selection to `RequestTramSlot` - the plugin exposes
`BlueprintCallable` entry points for both, but the concrete UMG/input-binding assets are a
host-project concern. Likewise, console commands for the diagnostics values are not yet
implemented (`GetDiagnosticSummary()` on both `UTramMovementComponent` and `ATramViewRig`
exist and can back a HUD, but no HUD/console command consumes them yet) - deferred to Phase 7
(hardening) rather than built speculatively now.

**Added after initial testing: `ATramSlotPreviewCamera`.** Objective 27 actually specifies a
simplified camera **per machine** ("allow each machine to render one approximate 90-degree
conventional camera"), not one identical shared camera for every machine - the first pass only
built the latter, so every PIE window showed the same view, which surfaced as a "both windows
look identical" report during testing. `ATramSlotPreviewCamera` is a purely local,
non-replicated actor (no simulation state of its own) that `ATramPlayerController` optionally
spawns per machine (opt-in via `DevPreviewDisplayConfiguration`, since it's a content-asset
reference, not a level actor, so it can be set once on a Blueprint child of
`ATramPlayerController` rather than needing per-instance level wiring). It reads the same
`GetSharedObserverTransform()` every machine reads and adds a yaw offset toward its assigned
slot's portion of the circle, computed as the circular mean of that slot's displays'
`BaseAngularPositionDegrees` (`UTramDisplayConfiguration::GetSlotCenterAngularOffsetDegrees` -
a circular, not arithmetic, mean, since a naive average breaks near the 0/360 wraparound).
Still explicitly Objective 27's dev/test tool, not Phase 6's off-axis per-screen projection.

## Phase 5 contents

Physical display geometry as data, kept independent of both tram simulation and networking
(Objectives 16, 17, 20, 21) - and independent of any specific rendering backend, so Phase 6 can
target nDisplay (or a fallback) without this data model changing:

| Type | File | Responsibility |
|---|---|---|
| `FTramScreenDefinition` | `TramDisplayTypes.h` | One physical screen: global `DisplayIndex`, parametric `BaseAngularPositionDegrees`, plus calibration offsets (position/yaw/pitch/roll) layered on top of the parametric ideal. |
| `FTramSlotDisplayMapping` | `TramDisplayTypes.h` | Explicit `SlotIndex -> DisplayIndices[]`, not a formula - Objective 21 explicitly warns against assuming a perfectly even distribution. |
| `UTramDisplayConfiguration` | `TramDisplayConfiguration.h/.cpp` | A `UDataAsset` holding `DisplayCount`, `CircleRadiusCm` (default 228.6cm = 7.5ft, configurable), `ObserverHeightCm`, per-screen `DisplayWidthCm`/`DisplayHeightCm`, the `Screens`/`SlotMappings` arrays, and queries (`GetScreenLocalTransform`, `GetDisplaysForSlot`, `GetSlotForDisplay`, `IsConfigurationValid`). |
| `UTramDisplayDebugComponent` | `TramDisplayDebugComponent.h/.cpp` | Draws the virtual circle, all screen planes with forward-vector arrows and slot-ownership labels, the observer location, and the composed observer forward vector - reading only already-shared state (`ATramViewRig`), owning none of it. Not replicated; purely a local dev aid. |

`GenerateDefaultLayout(DisplaysPerSlot)` is an explicit, `CallInEditor` reset-to-ideal tool
(evenly-spaced angles from `DisplayCount`/`CircleRadiusCm`, contiguous slot chunks) - it is
never called implicitly, so it can never silently discard an installer's calibration work.

`GetScreenLocalTransform(DisplayIndex)` returns a transform **relative to the observer rig's
origin**; combine with `ATramViewRig::GetSharedObserverTransform()` to get a world-space screen
transform: `ScreenWorld = GetScreenLocalTransform(Index) * ObserverTransform` (same
local-then-parent composition order used throughout, see Phase 4's composition-order note).
Screens are modeled as tangent to the circle, facing inward toward the center, vertically
centered at `ObserverHeightCm` - not yet using per-screen physical width/height to derive an
off-axis frustum (that's Phase 6, once real screen dimensions and Unreal's chosen multi-display
backend are known); `DisplayWidthCm`/`DisplayHeightCm` are carried now so Phase 6 doesn't need
a data model change, and already size the debug-visualization boxes.

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
- Rotation-follow smoothing is a pure fixed time delay, not a true recursive/exponential
  filter - a recursive filter's state would need to be replayed from history to evaluate at an
  arbitrary synchronized time, which is incompatible with this plugin's stateless
  "re-derive everything from a shared anchor + time" model. A fixed delay still fully addresses
  the objective's concern (no per-client local-frame-dependent divergence) and produces a
  reasonable lag/settle feel for the spline-turn scenario described in the acceptance tests
  (Phase 4).
- `ObserverOffset` default (`(0,0,150)` cm, identity rotation) is a placeholder seated-eye-height
  guess, not a measurement from the real installation; it's fully configurable per Objective 10
  and is expected to be tuned once physical dimensions are known (Phase 4/5).
- Screens are modeled as vertically centered at one configurable `ObserverHeightCm`, not as
  spanning an independently configurable floor/ceiling range - a simplification consistent with
  "portrait 4K panels arranged around a circle at viewer eye height," revisit if the real
  installation needs per-screen vertical placement beyond `CalibrationPositionOffsetCm` (Phase 5).
- `DisplayWidthCm`/`DisplayHeightCm` (default 70x120cm) are placeholder guesses for a portrait
  4K panel, not measured - configurable and intended to be corrected once real panel dimensions
  are known, before Phase 6 derives off-axis frustums from them (Phase 5).
- `ATramGameMode` sets `DefaultPawnClass = nullptr`: no per-player Pawn is ever spawned, since
  the installation is one shared tram/observer, not one viewpoint per player (Objective 1).
  `ATramPlayerController::BeginPlay` instead calls `SetViewTargetWithBlend` on the level's one
  `ATramViewRig`, whose new `CalcCamera` override feeds it `GetSharedObserverTransform()` with
  a single conventional FOV (`DebugFieldOfViewDegrees`) - this is Objective 27's "simplified
  90-degree machine camera" dev mode, explicitly not the production per-screen projection
  (Phase 4/SETUP.md).

## Testing this phase

No engine is available in the environment that produced this commit, so it has not been
compiled against UE 5.7. Verify by dropping the plugin into a 5.7 project, placing an
`ATramSplineRoute` with a closed-loop spline and a couple of per-point speeds, adding a
`UTramMovementComponent` to a test Actor referencing that route, and calling `LaunchTram()`
(e.g. from a Blueprint or console). Expect the actor to accelerate along the spline, hold
each segment's target speed, and decelerate into speed changes.
