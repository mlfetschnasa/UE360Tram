# Setup Guide

How to wire the `TramSystem` plugin into a host project for the first time. This assumes the
plugin has already been compiled once (see README.md's "Testing this phase" notes) and is
scoped to getting Phases 1-5 running in ordinary Unreal windows - no nDisplay/multi-monitor
setup here.

## 1. Install the plugin

Copy (or symlink) this repository into the host project's `Plugins/TramSystem/` folder, then
open the `.uproject` and enable it (Edit > Plugins > search "Tram System" > enable) if it isn't
picked up automatically. Regenerate project files and build.

## 2. Set the project's GameMode

`ATramGameMode`'s constructor wires `PlayerStateClass`, `GameStateClass`, and
`PlayerControllerClass` to `ATramPlayerState`/`ATramGameState`/`ATramPlayerController`
automatically - this is the one piece of "global" wiring the whole slot system depends on.

- Simplest: set the project's Default GameMode (Project Settings > Maps & Modes) to
  `ATramGameMode` directly, or to a Blueprint subclass of it.
- If the host project already has its own GameMode hierarchy: either re-parent it to
  `ATramGameMode`, or copy the three class assignments from `ATramGameMode`'s constructor into
  your own GameMode's constructor.
- A per-level GameMode override (World Settings > GameMode Override) works the same way if you
  don't want to change the project default.

Without this step, `ATramPlayerController::RequestTramSlot` will silently fail to find an
`ATramGameMode` (logged as a `LogTramSystem` warning) and no rider will ever get a slot.

## 3. Level setup - which actors point to which

```
Tram Actor                              (bReplicates = true, placed in level)
 └─ UTramMovementComponent
      Route ─────────────────────────►  ATramSplineRoute        (placed in level)

ATramViewRig                            (placed in level)
      TramActor ─────────────────────►  Tram Actor               (above)

Debug Actor (optional - can be the      (placed in level; can just be ATramViewRig itself)
ViewRig itself, or any other actor)
 └─ UTramDisplayDebugComponent
      ViewRig ────────────────────────►  ATramViewRig             (above)
      DisplayConfiguration ───────────►  UTramDisplayConfiguration (Content Browser data asset)
```

Nothing else references anything else. `ATramSplineRoute`, `UTramDisplayConfiguration`, and the
GameMode wiring are all "static" - identical on every machine because they come from the same
loaded level/content, not from replication.

### 3a. Route

Place an `ATramSplineRoute` in the level. Shape its `RouteSpline` component with the normal
spline editing gizmos (add/move points, set a point's type to Curve for smooth bends). Then:

- `PointSpeeds` auto-resizes to match the spline's point count as you edit it in-editor. Set
  each entry's `TargetSpeedCms` (cm/s) - leave it at `-1` to fall back to `DefaultTargetSpeedCms`.
  Remember: the speed on point N applies to the segment N -> N+1.
- `DefaultTargetSpeedCms` - fallback speed for any point left at `-1`.
- `RouteId` - only matters if you ever have more than one route in the project; leave the
  default if you only have one.
- Toggle the spline's closed-loop flag (spline component's own "Closed Loop" property) if the
  route should wrap around rather than stop at the last point.

### 3b. Tram actor

Create an Actor (Blueprint is fine) with a `UTramMovementComponent` added. Set:

- **`bReplicates = true` on the actor itself** (Blueprint: Class Defaults > Replication >
  Replicates). This is the one thing `UTramMovementComponent` cannot set for you, since it's
  reusable on an arbitrary host actor class - it only logs a `LogTramSystem` warning on the
  server if you forget it, tram state will simply never reach any client.
- `Route` on the component - point it at the `ATramSplineRoute` placed in step 3a.
- `AccelerationCmss` / `DecelerationCmss` / `MaxSpeedCms` (0 = unlimited) / `StartingDistanceCms`
  as desired.
- Leave `SnapshotPublishIntervalSeconds` and the `Correction*`/`MaxExtrapolationSeconds` fields
  at their defaults unless you're specifically tuning network behavior.

Give the actor a visible mesh if you want to actually see it move (a simple static mesh cube
scaled to tram-ish proportions is enough for testing).

### 3c. View rig

Place one `ATramViewRig` in the level and set `TramActor` to the tram actor from 3b. Leave
`ObserverOffset`/`LookAxisMode`/rotation-follow settings at their defaults for a first test.
If you want to actually see through it, spawn/possess a `CameraComponent` whose transform you
set every tick from `ATramViewRig::GetSharedObserverTransform()` (there is no built-in camera
Pawn in the plugin yet - see the note at the end of this doc).

### 3d. Display configuration + debug visualization (optional for a first test)

In the Content Browser: Add > Miscellaneous > Data Asset > pick class `TramDisplayConfiguration`.
Open it, set `DisplayCount`/`CircleRadiusCm`/etc., then click the **Generate Default Layout**
button in its Details panel (it's `CallInEditor`) to populate `Screens`/`SlotMappings` with an
evenly-spaced default you can then hand-calibrate.

Add a `UTramDisplayDebugComponent` to any actor (the `ATramViewRig` from 3c is a convenient
place - just point its `ViewRig` field at itself). Set `DisplayConfiguration` to the data asset
you just made and `ViewRig` to the actor from 3c. With `bDrawDebugVisualization` on, PIE will
draw the 15ft circle, all screen planes with forward-vector arrows and slot labels, and the
composed observer forward vector.

## 4. Running a multi-machine test

**In-editor (fastest iteration):** Play dropdown > Advanced Settings > Multiplayer Options: set
"Number of Players" to 2+ and "Net Mode" to "Play As Listen Server" (or "Play As Client" for
the additional windows). Each PIE window is a separate `UTramMovementComponent`/`ATramViewRig`
evaluation - watch that they report matching values (see the diagnostics note below).

**Packaged/launched builds** (closer to the real 4-machine installation): launch the server with
a `?listen` map URL, e.g. `MyProject.exe /Game/Maps/MyMap?listen`; launch each client with the
server's address and, optionally, an explicit slot: `MyProject.exe <ServerIP> -TramSlot=2`. If
`-TramSlot` is omitted, `ATramPlayerController` requests automatic assignment (see Phase 3 in
README.md for the full priority order, including the `[TramSystem] PreferredSlot=` ini option).

## 5. Triggering tram/slot/look actions during a test

`LaunchTram`/`PauseTram`/`ResumeTram`/`StopTram` (on `UTramMovementComponent`),
`RequestTramSlot`/`SetLookAxisMode`/`ApplyOperatorLookInput` (on `ATramPlayerController`/
`ATramViewRig`) are all `BlueprintCallable` but have **no UI or input binding wired up yet** -
that's host-project scope, not something the plugin assumes. For a first test without building
any UI:

- From the **Level Blueprint**, get a reference to the placed tram actor, use "Get Component by
  Class" (`UTramMovementComponent`) on it, and call `LaunchTram` from a key-press event. It's
  safe to trigger this from every connected machine's Level Blueprint - the function silently
  no-ops (with a `LogTramSystem` log line) on any machine that isn't the authority.
- Same pattern for `ATramViewRig::ApplyOperatorLookInput` bound to mouse-look input for a quick
  "does the shared look rotation propagate to other windows" check - again, safe to bind on
  every machine, since it also no-ops without authority.

## 6. Quick verification checklist

- `LogTramSystem` in the Output Log (filterable) is the primary signal - launch/pause/slot
  assignment/corrections all log there.
- `UTramMovementComponent::GetDiagnosticSummary()` and `ATramViewRig::GetDiagnosticSummary()`
  are the fastest way to compare state across windows (e.g. print them to screen with a
  `Print String` on a timer, one per PIE window) - matches the "Role/State/Dist/Speed/Seg/
  PredErr" and "LookMode/SharedLook/SmoothedBaseYaw" fields respectively.
- Expect `PredErr` to sit near 0 on clients almost all the time; it should only move visibly
  right after a correction, and only for a fraction of a second.
- `ATramPlayerController::GetAssignedTramSlot()` should return a valid (non -1) index shortly
  after each client connects, and no two connected riders should ever report the same slot.

## Known gaps at this stage

- No camera Pawn/HUD is provided - `ATramViewRig::GetSharedObserverTransform()` is the value to
  feed into whatever camera you use for a first-pass test; a dedicated Pawn wiring this up
  automatically may come later, but it isn't required for the acceptance tests above.
- No input bindings or UMG widgets for slot selection / look control / tram commands exist yet
  (see section 5) - only the `BlueprintCallable` entry points do.
