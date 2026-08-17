# Setup Guide

How to wire the `TramSystem` plugin into a host project for the first time. This assumes the
plugin has already been compiled once (see README.md's "Testing this phase" notes). Sections
1-6 get Phases 1-5 running in ordinary Unreal windows, no nDisplay/multi-monitor hardware
needed; section 7 covers the Phase 6 nDisplay production bridge, which does need that hardware
(or at least the nDisplay plugin enabled) to actually test.

## 1. Install the plugin

Copy (or symlink) this repository's `TramSystem/` folder into the host project's
`Plugins/TramSystem/` folder, then open the `.uproject` and enable it (Edit > Plugins > search
"Tram System" > enable) if it isn't picked up automatically. Regenerate project files and build.

Only copy `TramSystemDisplayCluster/` (into a sibling `Plugins/TramSystemDisplayCluster/`
folder) if you're doing the Phase 6 nDisplay setup in section 7 - it's a separate, optional
plugin and Phases 1-5 don't need it at all.

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

`ATramGameMode`'s constructor also sets **`DefaultPawnClass = nullptr`**. This is deliberate,
not an oversight: the installation is one shared tram and one shared observer, not one
viewpoint per player (Objective 1), so there is nothing for a connecting player to possess.
Do **not** set the tram actor (or anything with a `UTramMovementComponent`) as a project's
`DefaultPawnClass` - if the GameMode spawns one at every `PlayerStart`, you get N independent
trams instead of one, and each spawned instance has no way to get its `Route` set (see the
note in 3b). If you're using a Blueprint subclass of `ATramGameMode`, check its own Class
Defaults > `DefaultPawnClass` isn't overriding this back to something - a Blueprint child's
explicit default wins over the C++ parent's constructor value.

Instead, `ATramPlayerController::BeginPlay` automatically points each connecting player's own
camera at the one shared `ATramViewRig` in the level (`SetViewTargetWithBlend`, no Pawn
involved) - see 3c.

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

Create an Actor (Blueprint is fine, and it does **not** need to be a Pawn - a plain `Actor`
parent is correct) with a `UTramMovementComponent` added, and place exactly **one** instance
of it in the level. There is only ever one tram for the whole installation. Set:

- **`bReplicates = true` on the actor itself** (Blueprint: Class Defaults > Replication >
  Replicates). This is the one thing `UTramMovementComponent` cannot set for you, since it's
  reusable on an arbitrary host actor class - it only logs a `LogTramSystem` warning on the
  server if you forget it, tram state will simply never reach any client.
- `Route` on the component (select the placed instance in the level, not the Blueprint editor -
  it's `EditInstanceOnly` because it references another level actor, which a Blueprint's class
  defaults can't do) - point it at the `ATramSplineRoute` placed in step 3a.
- `AccelerationCmss` / `DecelerationCmss` / `MaxSpeedCms` (0 = unlimited) / `StartingDistanceCms`
  as desired.
- Leave `SnapshotPublishIntervalSeconds` and the `Correction*`/`MaxExtrapolationSeconds` fields
  at their defaults unless you're specifically tuning network behavior.

Give the actor a visible mesh if you want to actually see it move (a simple static mesh cube
scaled to tram-ish proportions is enough for testing).

### 3c. View rig

Place one `ATramViewRig` in the level and set `TramActor` to the tram actor from 3b. Leave
`ObserverOffset`/`LookAxisMode`/rotation-follow settings at their defaults for a first test.
Every connecting `PlayerController` automatically calls `SetViewTargetWithBlend` on this actor
in `BeginPlay` and `ATramViewRig::CalcCamera` feeds it `GetSharedObserverTransform()` with a
single conventional FOV (`DebugFieldOfViewDegrees`, default 90) - Objective 27's simplified
single-camera dev mode. No Pawn, camera component, or possession of any kind is needed for a
first test; this is explicitly a development stand-in, not the production per-screen
projection (that's Phase 6).

**By default every machine sees the identical view** (same shared transform, no per-machine
offset) - correct for verifying tram/look synchronization, but not useful for previewing
different slices of the 360° circle across multiple PIE windows. To do that, set
`ATramPlayerController::DevPreviewDisplayConfiguration` to the `UTramDisplayConfiguration`
data asset from 3d (this is a Blueprint-class-default field, not a per-instance one - it's a
content asset reference, not a level actor, so it can be set once on a Blueprint child of
`ATramPlayerController` rather than per placed instance). When set, each connecting machine
spawns its own local `ATramSlotPreviewCamera` instead of viewing the shared rig directly, which
adds a yaw offset toward that machine's assigned slot's portion of the circle (via
`UTramDisplayConfiguration::GetSlotCenterAngularOffsetDegrees`) - so slot 0's window looks
toward its own displays, slot 1's window looks toward a different arc, etc. This camera is
purely local and non-replicated; it holds no simulation state of its own, only reads the same
shared observer transform every machine reads.

### 3d0. Making the project's GameMode use a PlayerController with `DevPreviewDisplayConfiguration` set

Since `ATramGameMode` wires `PlayerControllerClass` to `ATramPlayerController` directly (see
section 2), setting `DevPreviewDisplayConfiguration` requires either a Blueprint child of
`ATramPlayerController` (set the field in its Class Defaults, then point a Blueprint child of
`ATramGameMode`'s `PlayerControllerClass` at it) or a small C++ subclass that sets it in its
constructor. Leave it unset to keep the simpler "identical shared view everywhere" behavior.

### 3d. Display configuration + debug visualization (optional for a first test)

In the Content Browser: Add > Miscellaneous > Data Asset > pick class `TramDisplayConfiguration`.
Open it, set `DisplayCount`/`CircleRadiusCm`/etc., then click the **Generate Default Layout (3
Per Slot)** button in its Details panel to populate `Screens`/`SlotMappings` with an
evenly-spaced default (3 displays/slot) you can then hand-calibrate. (`GenerateDefaultLayout`
itself takes a `DisplaysPerSlot` argument for other configurations, but `CallInEditor` only
generates a Details-panel button for parameterless functions, hence the dedicated button for
the common case - call `GenerateDefaultLayout` directly from Blueprint/C++ for any other value.)

Add a `UTramDisplayDebugComponent` to any actor (the `ATramViewRig` from 3c is a convenient
place - just point its `ViewRig` field at itself). Set `DisplayConfiguration` to the data asset
you just made and `ViewRig` to the actor from 3c. With `bDrawDebugVisualization` on, PIE will
draw the 15ft circle, all screen planes with forward-vector arrows and slot labels, and the
composed observer forward vector.

### 3e. Synchronization HUD (Objective 26, optional)

`ATramSyncHUD` prints the diagnostics Objective 26 asks for directly to the screen via
`Canvas::DrawText` - no UMG/content assets needed. Since `ATramGameMode` doesn't set `HUDClass`
(same reasoning as `PlayerControllerClass` in 3d0 - it's a host-project concern), using it needs
either a Blueprint child of `ATramGameMode` with `HUDClass` set to `ATramSyncHUD`, or a small
C++ subclass that sets `HUDClass = ATramSyncHUD::StaticClass()` in its constructor.

It's off by default (`bShowSyncHUD = false`) so it never appears unintentionally - either check
that box directly on a placed instance for testing, or call `ToggleSyncHUD()` from a debug input
binding. `ViewRig` auto-resolves the same single-candidate-only way `UTramDisplayClusterViewSync`'s
`RootActor` does (3a); set `DisplayConfiguration` (from 3d) explicitly if you want the "Global
display indices" line populated for this machine's assigned slot.

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
  are the fastest way to compare state across windows - matches the "Role/State/Dist/Speed/Seg/
  PredErr" and "LookMode/SharedLook/SmoothedBaseYaw" fields respectively. `ATramSyncHUD` (3e)
  now prints both directly to screen, one per PIE window, without wiring up a manual
  `Print String`.
- Expect `PredErr` to sit near 0 on clients almost all the time; it should only move visibly
  right after a correction, and only for a fraction of a second.
- `ATramPlayerController::GetAssignedTramSlot()` should return a valid (non -1) index shortly
  after each client connects, and no two connected riders should ever report the same slot.

To be notified rather than poll: bind to **`ATramPlayerController::OnLocalTramSlotChanged`**,
not to `ATramPlayerState`'s `OnTramSlotChanged` directly - `PlayerState` can still be `None` at
the exact moment a Level Blueprint's `BeginPlay` runs (especially on a client, before its own
PlayerState has replicated down), so a bind attempted straight against `Get Player State` can
silently never happen. `OnLocalTramSlotChanged` is the controller's own forwarding of that
event, and the controller tracks PlayerState becoming valid internally regardless of whether
that happens before or after the controller's own `BeginPlay`.

That said, **`OnLocalTramSlotChanged` is still an ordinary delegate: it does not replay a
broadcast that happened before you bound to it.** Slot assignment is a full server round-trip
and can complete before your own Blueprint gets around to binding (especially on a listen
server's own window, where it can resolve almost instantly) - if that happens, your bind
silently misses the one-and-only broadcast, and since the slot doesn't change again, nothing
ever fires afterward. The reliable pattern is **both**, in this order, on the same exec chain:

1. `Bind Event to OnLocalTramSlotChanged` (catches any *future* change)
2. Immediately afterward, also call `GetAssignedTramSlot()` directly and handle that value too
   (catches a slot that was already assigned *before* you bound)

Both your bound event and the immediate poll should feed the same handling logic (e.g. the
same `Print String`) - it's fine, and expected, for it to fire twice with the same value if a
change happens to land exactly between steps 1 and 2.

## 7. Phase 6: nDisplay production bridge (needs the nDisplay plugin, and ideally the real hardware)

This section covers wiring up the **production** multi-display path. It's independent of
everything above - your tram/slot/look testing setup doesn't change at all.

### 7a. Enable nDisplay and add the bridge component

Copy `TramSystemDisplayCluster/` into `Plugins/TramSystemDisplayCluster/` if you haven't
already (section 1). Its own `.uplugin` declares plugin-level dependencies on both `TramSystem`
and `nDisplay` (the plugin you see and enable under Edit > Plugins, ships with the engine - no
separate download; note this is the *plugin's* name, distinct from `DisplayCluster`, which is
just the name of the specific module inside it that `Build.cs` links against - getting this
wrong the first time produces an "unable to find plugin 'DisplayCluster'" error at build time),
so enabling **"Tram System - DisplayCluster Bridge"** under Edit > Plugins should bring
`nDisplay` along with it automatically. If it doesn't, or if you see a "module could not be
loaded" error at startup, explicitly enable "nDisplay" under Edit > Plugins too, then restart
the editor - a plugin's own declared dependencies aren't always a substitute for manually
confirming a dependency is enabled, especially the first time. Regenerate project files and
build after enabling.

nDisplay is asset-first, not placement-first: rather than searching the generic Place Actors
panel for a base class, create an nDisplay config asset from the Content Browser (right-click >
look for an "nDisplay" category) - this generates a Blueprint-based `ADisplayClusterRootActor`
subclass along with its Configurator editor for defining nodes/screens (see 7c). Drag that
asset into the level to place an instance; that placed instance is your **stage/root actor**.
Its name in the World Outliner is not a reliable signal of what it is - one project's root
actor instance ended up auto-named "ND_Screen" during setup, which reads like an individual
screen but was in fact the one legitimate root actor. Verify by class instead: select it and
check its type in the Details panel (or Class Viewer), or just trust
`UTramDisplayClusterViewSync`'s own diagnostics (below) to tell you if it's ambiguous. Add a
`UTramDisplayClusterViewSync` component to the stage/root actor itself (simplest - avoids any
ambiguity about which actor it's on) or to any other convenient actor. Set its `ViewRig` to
your `ATramViewRig`.

**Set `RootActor` explicitly** to that same stage/root actor rather than leaving it unset -
`BeginPlay` can auto-resolve it via a level-wide class search only when there is exactly one
`ADisplayClusterRootActor` instance in the level, and warns (naming every candidate, rather than
guessing) if it finds more than one. That's the entire runtime integration - every frame, it
moves `RootActor` to `ATramViewRig::GetSharedObserverTransform()`.

### 7b. Verifying the sync is working

Three ways to confirm it's working, from simplest to most conclusive:

- **Output Log**, filtered to `LogTramSystemDisplayCluster`: `UTramDisplayClusterViewSync` logs
  once on success at `BeginPlay` (which root actor / view rig it resolved), and then - every
  `DiagnosticLogIntervalSeconds` (default 2s, 0 disables it) - the root actor's current
  Location/Rotation, so you can watch the numbers change as the tram moves without needing to
  touch the viewport at all. It only logs on the *failure* path otherwise (missing root
  actor/view rig), so silence alone isn't proof of success - check for the one-time success line.
- **Details panel**: select the `ADisplayClusterRootActor` in the World Outliner and watch its
  Transform values live-update while the tram runs. Each screen's own mesh (a child of the root
  actor) should visibly move/rotate along with it too, via ordinary parent-child transform
  propagation - no special nDisplay rendering needed for just this part.
- **nDisplay's own in-editor preview** (the most conclusive check, confirmed working): select
  the root actor, find its **Preview** category in the Details panel, and enable it (Python API
  name `preview_enable`). This renders a live, full-size window showing the selected node's
  actual per-screen projected output, directly in the editor - no Switchboard or physical
  cluster launch needed. Requires at least one Screen actually configured (7c) to have anything
  to project. This is the real validation tool for projection/seam correctness during
  development; nDisplay's actual clustered rendering (genlocked, multi-machine) does not run in
  the editor at all and is a separate step, orchestrated via Switchboard once you have the
  physical installation.

### 7c. Authoring the nDisplay cluster config

This plugin does **not** generate nDisplay's config for you (see README.md's Phase 6 section
for why - version-schema risk). Use nDisplay's own in-editor Configurator tool, and pull the
numbers it asks for from your `UTramDisplayConfiguration` data asset:

- **Circle radius / observer height**: `CircleRadiusCm`/`ObserverHeightCm` on the data asset.
- **Per-screen size**: `DisplayWidthCm`/`DisplayHeightCm`.
- **Per-screen transform** (position/rotation relative to the stage origin): click
  `LogAllScreenTransforms` in the data asset's Details panel (another `CallInEditor` button,
  parameterless like `GenerateDefaultLayoutButton`) and read the values from the Output Log,
  filtered to `LogTramSystem` - one line per screen, formatted for direct transcription into
  the Configurator's per-screen Location/Rotation/Size fields.
- **Node ↔ machine mapping**: nDisplay's own per-node config (host IP, `-dc_node=` launch arg)
  is separate from this plugin's `-TramSlot=` mechanism - make sure whoever sets up each
  physical PC's launch shortcut keeps both consistent for that machine (e.g. the PC configured
  as nDisplay node 0 should also launch with `-TramSlot=0`). There is no automatic
  cross-checking between the two yet.

#### How nDisplay's Node/Viewport/Screen hierarchy maps onto this project's Slot/Display concepts

It's easy to expect one `ADisplayClusterRootActor`/config asset per machine, the way this
project has one `-TramSlot=` per machine. That's not how nDisplay works, and the mismatch is
worth spelling out before you build the full config:

- There is **one** config asset (one root actor) describing the **entire** installation - all
  4 machines, all 12 screens - not one per machine. You author it once, in one level, and every
  machine loads the same asset at launch.
- Inside that one config asset, nDisplay has **Cluster Nodes**. A Cluster Node corresponds to
  one physical machine - this is the nDisplay-native equivalent of this project's "Slot", so
  you'll have 4 Cluster Nodes, matching the 4 `-TramSlot=` values (0-3).
- Each Cluster Node owns multiple **Viewports**, and each Viewport is bound to a **Screen**
  (the physical projection geometry - position/size/orientation, the same numbers you pull from
  `LogAllScreenTransforms` above). Since each of this project's 4 slots drives 3 portrait
  displays, each Cluster Node will own 3 Viewports/Screens.
- This is structurally identical to `UTramDisplayConfiguration.SlotMappings` (SlotIndex -> 3
  DisplayIndices) - nDisplay's Node -> Viewports relationship is the same shape, just authored
  in the Configurator tool instead of a data asset.
- At launch, every machine runs the *same* shared config, and `-dc_node=<NodeID>` is what tells
  that particular machine which Cluster Node it personifies - it then renders only that node's
  viewports. This is also why there's only one `ADisplayClusterRootActor` in the level for
  `UTramDisplayClusterViewSync` to find and move (see 7a/7b) even though 4 physical machines are
  involved: the root actor is the whole stage, not a per-machine actor.
- Practically, this means you don't need all 4 nodes/12 screens built before you can validate
  anything. Build out **Node 0's 3 screens first**, confirm the projection looks right via the
  in-editor preview (7b), and only then repeat the same pattern for nodes 1-3.

### 7d. If your level uses World Partition: a spatial-loading gotcha

If the Map Check log shows something like `Non-spatially loaded actor ND_Screen references
Spatially loaded actor TramViewRig`, that's World Partition flagging a real (if usually
survivable in a small test level) issue, not an nDisplay- or TramSystem-specific bug. nDisplay's
root actor/screens are typically always-loaded (not tied to a World Partition streaming cell),
but `ATramViewRig` - along with the tram actor and route - defaults to being spatially loaded
like any other placed actor, meaning it could theoretically be streamed out while something
always-loaded still references it. Since these are core, always-needed actors rather than
streamable background content, the fix is to mark them **Always Loaded**.

Where that setting actually lives depends on which World Partition runtime hash your project
uses. On projects using the newer Runtime Hash Set (actors assigned to a named runtime
partition, e.g. `WorldPartitionRuntimeHashSet` + `RuntimePartitionPersistent`, rather than a flat
streaming grid), it's not a standalone top-level checkbox - it's nested under the actor's
**World Partition > Runtime Settings** group alongside `HLODLayers`/`PartitionLayer`. Easiest way
to find it regardless of layout: use the Details panel's search field and type "spatially" to
filter straight to the property. Once the actor is assigned to the always-loaded/persistent
partition (`bIsSpatiallyLoaded=False`), reload the level and re-check the Output Log filtered on
`MapCheck` - confirmed clear on reload once `ATramViewRig` was correctly assigned. Worth doing
for `ATramViewRig`, the tram actor, and `ATramSplineRoute` even outside the nDisplay setup, if
your level uses World Partition at all.

## Known gaps at this stage

- `ATramSyncHUD` (3e) provides the read-only diagnostics overlay; no input bindings or UMG
  widgets for slot selection / look control / tram commands exist yet (see section 5) - only
  the `BlueprintCallable` entry points do.
- `ATramViewRig::CalcCamera`'s single conventional FOV (section 3c) - and
  `ATramSlotPreviewCamera`'s per-slot yaw offset on top of it - are development stand-ins for
  nDisplay's actual per-screen off-axis projection (section 7). Neither renders each machine's
  three real screen frustums, so don't judge seam/projection correctness from ordinary PIE
  testing, only tram/slot/look synchronization; use nDisplay's own in-editor preview (7b) once
  screens are configured (7c) to actually judge projection/seam correctness - confirmed working,
  no physical cluster needed for that.
- No automatic generation of nDisplay's cluster config, and no automatic cross-check between
  its per-node config and this plugin's per-slot config (section 7c) - both are currently
  manual, parallel setup steps an installer has to keep consistent by hand.
- `UTramDisplayClusterViewSync` (section 7a) has been confirmed compiling and correctly syncing
  a real `ADisplayClusterRootActor`'s transform in testing (see README.md's Phase 6 section for
  the two build/plugin-dependency issues that came up along the way and how they were fixed).
  Still unverified: actual genlocked multi-machine cluster rendering via Switchboard, and the
  full 12-screen production config - only reachable with the physical installation or a
  multi-window local cluster test.
