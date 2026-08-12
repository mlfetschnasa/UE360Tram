# Unreal Engine 5.7 Tram + 360° Display Plugin — Refined Agent Objectives

## System Definition

Implement a C++ Unreal Engine plugin for a LAN-based, synchronized tram simulation used by a 360° physical display installation.

The physical installation consists of:

* 12 identical 4K portrait displays.
* Displays arranged edge-to-edge around a perfect horizontal circle.
* Each display is vertical and tangent to the circle.
* Display bezels do not need to be compensated for.
* Physical circle diameter is approximately 15 feet.
* Viewer(s) stand near the center of the display circle.
* 4 computers are normally used.
* Each computer drives 3 adjacent displays.
* The number of computers/view slots should remain configurable where practical.
* All game instances run the same Unreal project.
* One instance acts as a listen server.
* The listen-server machine also renders three displays like every other rider machine.

The in-game tram should be approximately the same physical scale as the real display installation.

The display system should behave conceptually as one distributed 360° camera rig rather than several independent multiplayer cameras.

---

# Core Architectural Principle

Separate these systems:

1. Tram movement simulation.
2. Network synchronization.
3. Tram rider / machine slot assignment.
4. Shared observer/camera orientation.
5. Physical display projection.
6. Local rendering of the three screens attached to each machine.

Do not make spline movement logic dependent on display rendering.

Do not make individual network players behave as independent camera viewpoints.

All riders represent portions of one shared virtual observer/viewing rig.

---

# Objective 1 — Runtime C++ Plugin

Create a reusable Unreal Engine 5.7 runtime plugin.

Suggested runtime module:

`TramSystem`

The plugin should:

* Work in packaged builds.
* Support listen-server multiplayer.
* Support LAN clients.
* Expose appropriate controls to Blueprint.
* Avoid editor-only dependencies in runtime code.
* Keep display configuration data-driven.
* Avoid hard-coding exactly 4 clients or 12 displays into general-purpose networking code.

Suggested conceptual classes may include:

* `UTramMovementComponent`
* `ATramSplineRoute`
* `UTramRiderComponent`
* `ATramViewRig`
* `UTramDisplayConfiguration`
* `UTramNetworkSynchronizationComponent`
* `ATramPlayerController`
* Supporting replicated structs and enums

The coding agent may revise exact class boundaries when justified.

---

# Objective 2 — Tram Movement Component

Create an Actor Component that makes its owning Actor behave as a tram.

The component should:

* Reference an Unreal spline route.
* Maintain position as distance along spline.
* Move the owning Actor along the spline.
* Derive orientation from the spline.
* Support configurable starting position.
* Support start, stop, pause, and resume.
* Support server-authoritative operation.
* Be reusable with arbitrary Actor classes.

The authoritative server should own tram simulation state.

Do not primarily replicate the tram using continuous raw transform replication.

Prefer synchronization based on spline-relative motion state and synchronized network time.

---

# Objective 3 — Spline Route

Represent the tram route using an Unreal spline.

Support:

* Arbitrary spline shapes.
* Closed-loop routes.
* Open routes where practical.
* Distance-along-spline queries.
* Spline transform queries.
* Per-spline-point tram metadata.

Spline point metadata should include at minimum:

`TargetSpeed`

A speed assigned to spline point `N` means:

> Use that target speed while travelling from spline point `N` toward spline point `N+1`.

Speed units should use Unreal convention, normally centimeters per second.

---

# Objective 4 — Dynamic Speed and Acceleration

The tram must support dynamic speed.

Each route segment receives a target speed from its starting spline point.

When entering a new segment:

* Determine the segment's target speed.
* Accelerate or decelerate toward that speed.
* Do not instantly jump velocity unless configured to do so.

Expose configurable:

* Acceleration rate.
* Deceleration rate.
* Default speed.
* Segment target speeds.
* Maximum speed if useful.
* Emergency/instant stop if useful.

Acceleration and deceleration should initially be deterministic, preferably using a simple rate-based model.

The server owns velocity state.

Clients reconstruct synchronized motion rather than independently choosing acceleration.

---

# Objective 5 — Server-Controlled Initial Launch

The tram should not automatically begin moving when the session starts.

Initial state:

`WaitingForLaunch`

The listen-server operator determines when the tram begins.

The server must be able to launch regardless of how many riders are currently connected.

Possible states:

* `WaitingForLaunch`
* `Running`
* `Paused`
* `Stopped`

Expose server-only actions such as:

* Launch Tram
* Pause Tram
* Resume Tram
* Stop Tram

Late-joining riders synchronize to the current tram state automatically.

---

# Objective 6 — LAN Multiplayer

Networking is LAN-only.

Topology:

* One listen server.
* Listen server participates as a normal display/rider machine.
* Remaining machines connect as clients.
* Server authoritatively owns tram and shared camera state.

The system does not need internet-grade lag compensation.

However, visible seams between machines are extremely sensitive to small synchronization differences, so synchronization should favor visual consistency over conventional character-networking behavior.

---

# Objective 7 — Rider Identity and Machine Slots

Distinguish between:

`Rider / Machine Identity`

and:

`Tram Slot`

A tram slot represents a rendering machine's assigned portion of the physical display circle.

Default four-machine example:

* Slot 0 → Displays 0, 1, 2
* Slot 1 → Displays 3, 4, 5
* Slot 2 → Displays 6, 7, 8
* Slot 3 → Displays 9, 10, 11

Slot ownership must be server-authoritative.

The server should:

* Track occupied slots.
* Validate requested slots.
* Prevent accidental duplicate ownership.
* Assign an available slot automatically when requested.
* Replicate slot assignments.

The listen server participates using the same slot mechanism rather than receiving hard-coded special camera behavior.

---

# Objective 8 — Slot Selection and Automatic Assignment

Support multiple methods for choosing a machine's slot.

Required:

1. In-game UI selection.
2. Command-line selection.
3. Configuration-file selection.

Possible launch syntax:

`TramExperience.exe -TramSlot=2`

Also support automatic assignment.

If a connecting client does not specify a slot:

* Server assigns an available tram slot.
* Assignment should be deterministic where practical.
* The rider receives the assigned slot before enabling its final display view.

If the desired slot is already occupied, either:

* Reject the requested slot and automatically choose another free slot, or
* Report the conflict based on configuration.

Default behavior should favor automatically placing the machine into an available slot.

---

# Objective 9 — Late Joining

Clients may join after the tram has launched.

On connection, a late rider must receive:

* Current tram state.
* Current spline distance or deterministic motion state.
* Current velocity.
* Current route segment.
* Current server time reference.
* Shared camera orientation.
* Current camera-follow interpolation state if necessary.
* Slot configuration.
* Physical display configuration.

The joining machine should rapidly converge to the same rendered tram transform as existing machines.

Do not require restarting or stopping the tram when a rider connects.

---

# Objective 10 — Shared Virtual Observer

Treat the display system as one shared virtual observer.

The observer exists at a configurable transform relative to the tram.

Its default position should approximately correspond to the center of the physical 15-foot-diameter display installation.

Separate:

* Tram transform.
* Observer transform relative to tram.
* Smoothed tram-heading contribution.
* Shared operator-controlled rotation.
* Physical screen projection.

Conceptually:

`Tram Transform`
→ `Smoothed Observer Base Orientation`
→ `Shared Look Rotation`
→ `Physical Screen Projection`

Individual computers do not receive independent observer locations.

---

# Objective 11 — Smooth Tram Rotation Following

When the tram changes heading along the spline, the viewing rig should not instantly snap to the new tram orientation.

The observer's base rotation should smoothly follow tram rotation.

Provide configurable rotational smoothing.

At minimum expose:

* Rotation follow interpolation speed.
* Option to enable/disable smoothing.
* Possibly separate yaw/pitch/roll follow behavior if needed later.

The smoothing algorithm must be deterministic enough that all computers produce matching camera transforms.

Avoid allowing each client to interpolate from its own most recently received raw transform because small timing differences could produce visible seams.

Prefer calculating the smoothed orientation from shared authoritative state/time.

---

# Objective 12 — Shared Operator Look Rotation

The listen-server operator can rotate the complete viewing rig using mouse input.

This rotation is shared by all connected machines.

The system should support selectable axis modes.

Required modes:

* `YawOnly`
* `PitchOnly`
* `YawPitch`
* `YawPitchRoll`

Optionally support:

* `Disabled`

A `RollOnly` mode is not required.

The active mode should be changeable at runtime by the listen-server operator.

All machines must derive their camera orientations from the same authoritative shared rotation.

Replicate the orientation or enough state to reconstruct it deterministically.

---

# Objective 13 — Shared Camera Transform Consistency

Seam consistency is critical.

Every machine must evaluate the same virtual observer transform for the same render time as closely as practical.

Avoid conventional independent client camera interpolation.

Instead design around:

* Authoritative server timestamps.
* Synchronized server/world time.
* Time-based tram state evaluation.
* Time-based rotation evaluation.
* Common deterministic interpolation rules.

The important invariant is:

> At equivalent presentation time, all riders should calculate the same tram transform and observer transform.

Exact frame-lock between GPUs is not required.

---

# Objective 14 — Network Tram State

Investigate a replicated tram-state structure similar conceptually to:

```cpp
USTRUCT()
struct FTramMotionState
{
    FName RouteId;

    double ServerTimestamp;

    double DistanceAlongSpline;

    double CurrentSpeed;

    double TargetSpeed;

    int32 CurrentSegmentIndex;

    ETramMovementState MovementState;
};
```

Exact implementation may differ.

The server periodically publishes authoritative state.

Clients use the state plus synchronized network time to derive current tram position locally.

Avoid relying only on `ReplicateMovement`.

---

# Objective 15 — Correction Handling

Even deterministic extrapolation can drift.

Implement periodic authoritative correction.

Corrections should:

* Detect positional/time error.
* Avoid visible snapping unless error is severe.
* Converge machines toward the same authoritative state.
* Prefer identical correction behavior on all clients.

Provide debug information showing synchronization error.

Potential values:

* Predicted spline distance.
* Authoritative spline distance.
* Distance error.
* Estimated time offset.
* Last correction amount.

---

# Objective 16 — Physical Screen Projection

Treat each of the 12 physical displays as an individual projection surface.

Default installation:

* 12 screens.
* 360° total circle.
* Approximately 30° angular spacing per screen.
* Three neighboring screens rendered by each PC.

Do not represent each PC using one conventional 90° perspective camera unless explicitly selected as a simplified/debug rendering mode.

Each monitor should receive a projection appropriate to its physical plane.

The desired result is that geometry viewed from the physical center appears spatially continuous when crossing from one monitor to the next.

---

# Objective 17 — Off-Axis / Screen-Space Projection

Investigate physically based off-axis projection for each display.

Configuration should eventually describe each screen using physical or normalized geometry such as:

* Screen center location.
* Screen orientation.
* Screen width.
* Screen height.
* Virtual eye location.

From these values, derive the camera/frustum appropriate for that display.

Because the displays form a regular circle, configuration may initially be generated parametrically from:

* Circle diameter: ~15 ft.
* Screen count: 12.
* Screen angular spacing: 30°.
* Common screen dimensions.
* Shared screen height.
* Observer position at circle center.

Avoid hard-coding FOV values if projection can instead be generated from physical screen geometry.

---

# Objective 18 — Local Three-Screen Rendering

Each PC normally renders three adjacent physical displays.

For example:

Slot 0:

* Screen 0
* Screen 1
* Screen 2

Slot 1:

* Screen 3
* Screen 4
* Screen 5

and so on.

The local rendering layer should know:

* Assigned tram slot.
* Global display indices owned by that slot.
* Projection configuration for each display.
* Local viewport/window mapping for each monitor.

The three views must originate from the same virtual observer position.

They differ only in projection surface/frustum.

---

# Objective 19 — Rendering Technology Abstraction

Keep tram simulation independent from the rendering implementation.

Investigate Unreal's nDisplay / DisplayCluster capabilities for:

* Multiple viewports.
* Per-screen projection.
* Cluster-style display definitions.
* Multi-monitor window placement.

If nDisplay provides suitable functionality, integrate with it rather than recreating established Unreal projection systems.

However:

* Do not make spline movement depend directly on DisplayCluster.
* Do not make rider slot assignment dependent on a specific projection backend.
* Create a clean boundary between synchronized view state and display rendering.

A simplified normal-camera renderer may be useful for development/testing without the physical display installation.

---

# Objective 20 — Physical Scale

The display circle diameter is approximately:

`15 ft ≈ 457.2 cm`

Radius:

`7.5 ft ≈ 228.6 cm`

Use Unreal's centimeter scale.

The virtual tram interior and observer area should approximately reproduce this scale.

Provide configuration rather than permanently hard-coding the radius.

Example:

`PhysicalDisplayCircleRadiusCm = 228.6`

This allows the virtual camera geometry to correspond to the real installation.

---

# Objective 21 — Display Configuration

Create a data-driven display configuration.

Possible fields:

```text
DisplayCount
MachineSlotCount
DisplaysPerMachine
CircleRadiusCm
ObserverHeightCm
DisplayWidthCm
DisplayHeightCm
DisplayAngularOffsets
PerDisplayCalibration
MachineToDisplayMappings
```

Do not assume displays always remain exactly evenly distributed even though that is the initial installation.

Support optional calibration offsets per screen.

Possible adjustments:

* Yaw offset.
* Pitch offset.
* Roll offset.
* Position offset.
* FOV/projection offset if appropriate.

---

# Objective 22 — Startup Flow

Desired listen-server flow:

1. Launch game as listen server.
2. Assign/select the host's tram slot.
3. Enter tram in `WaitingForLaunch`.
4. Clients may connect.
5. Clients select/request/automatically receive slots.
6. Server synchronizes tram and camera state.
7. Operator verifies connected riders.
8. Operator presses Launch.
9. Tram starts moving.

The server does not need to wait for every possible tram slot to be occupied.

Desired late-client flow:

1. Launch client.
2. Load configured slot or request automatic assignment.
3. Connect to listen server.
4. Server assigns slot.
5. Client synchronizes time.
6. Client receives current tram state.
7. Client receives shared camera state.
8. Client initializes local display projections.
9. Client begins rendering synchronized view.

---

# Objective 23 — Missing Riders

Missing riders must not prevent tram operation.

If only some machines are connected:

* Tram still launches when server requests.
* Connected riders continue rendering normally.
* Missing portions of the physical display installation simply remain absent/unrendered.

When another machine joins later:

* Assign it an available slot.
* Synchronize it to current state.
* Begin rendering its assigned screens without disturbing existing riders.

---

# Objective 24 — Listen Server Equality

The listen-server machine performs additional authority/input responsibilities but its rendering path should otherwise be equivalent to client machines.

Avoid logic such as:

`if (HasAuthority()) use different camera calculation`

when computing the final display view.

Instead both server and clients should feed the same synchronized state into the same view-evaluation code.

This reduces the chance that the host's three screens visually disagree with adjacent client screens.

---

# Objective 25 — Debug Visualization

Provide a debug mode that can visualize the entire display rig in Unreal.

Useful visualization:

* Virtual 15-foot display circle.
* Twelve screen planes.
* Screen numbering.
* Machine/slot ownership.
* Virtual observer location.
* Forward vector for each screen.
* Current tram orientation.
* Smoothed observer orientation.
* Shared look orientation.

This should make it possible to test projection setup without standing inside the real installation.

---

# Objective 26 — Synchronization HUD

Provide optional debugging output containing:

* Network role.
* Tram slot.
* Global display indices.
* Server time.
* Local synchronized server time.
* Current spline segment.
* Distance along spline.
* Current speed.
* Target speed.
* Tram movement state.
* Position prediction error.
* Shared camera yaw/pitch/roll.
* Active mouse-axis mode.
* Current smoothed tram-follow rotation.
* Ping.
* Last synchronization correction.

The listen server should additionally show:

* Connected riders.
* Occupied/free slots.
* Launch state.

---

# Objective 27 — Development Test Modes

Support simpler development configurations.

Examples:

### Single-screen debug mode

One normal Unreal window shows one selected screen's view.

### Machine debug mode

One development machine displays its three screen views side-by-side.

### Entire-circle debug mode

One development machine renders thumbnails/previews of all twelve display views.

### Simplified 90° machine camera

Optionally allow each machine to render one approximate 90° conventional camera for networking tests.

This mode is for development only and should not be treated as the production projection solution.

---

# Objective 28 — Acceptance Tests

The implementation should eventually pass these tests.

## Tram Motion Test

Run listen server plus multiple clients.

Verify all instances calculate nearly identical:

* Spline distance.
* Tram position.
* Tram rotation.
* Observer transform.

---

## Segment Speed Test

Configure spline points with different speeds.

Verify:

* Speed assigned at point N applies through segment N→N+1.
* Tram accelerates/decelerates according to configured rates.
* All machines calculate matching velocity and position.

---

## Shared Look Test

Move the listen-server mouse.

Verify every machine updates shared view orientation consistently.

Cycle:

* Yaw only.
* Pitch only.
* Yaw + pitch.
* Yaw + pitch + roll.

Verify seams remain aligned.

---

## Tram Turn Test

Drive through a significant spline turn.

Verify:

* Tram orientation follows spline.
* Observer orientation smoothly interpolates toward new tram heading.
* Every client uses the same interpolation result.
* No camera seam opens because machines rotate at different rates.

---

## Late Join Test

Launch tram with only the listen server.

Connect clients after motion has started.

Verify:

* Each receives an available slot.
* It immediately reconstructs current tram state.
* Existing riders are unaffected.
* New view becomes visually contiguous.

---

## Projection Test

Place long straight geometry through the viewer's environment.

View it crossing neighboring physical screens.

Verify that:

* Perspective remains geometrically plausible.
* Lines do not visibly kink due to an incorrect single-wide-camera projection.
* Adjacent displays appear to represent one continuous surrounding world.

---

# Initial Implementation Milestones

## Milestone 1 — Tram Core

Implement:

* Spline route.
* Tram movement component.
* Per-segment speed.
* Acceleration/deceleration.
* Waiting/launch/running states.

No multiplayer rendering complexity yet.

---

## Milestone 2 — Deterministic Network Motion

Implement:

* Listen server.
* Tram state replication.
* Server-time synchronization.
* Client reconstruction.
* Late joining.
* Diagnostics.

Test with ordinary windows first.

---

## Milestone 3 — Rider Slots

Implement:

* Slot model.
* Server slot registry.
* Automatic assignment.
* UI selection.
* Command-line selection.
* Config selection.
* Late rider assignment.

---

## Milestone 4 — Shared View Rig

Implement:

* Shared observer transform.
* Tram-heading smoothing.
* Server mouse control.
* Yaw/pitch/roll mode enum.
* Synchronized camera state.

Prove seam consistency using simplified cameras.

---

## Milestone 5 — Twelve-Screen Projection Model

Implement:

* 15-foot virtual circle.
* Twelve virtual display planes.
* Shared center observer.
* Per-screen projection calculation.
* Three-screen-to-machine assignment.

Validate projection inside the editor/debug mode.

---

## Milestone 6 — Production Multi-Display Output

Integrate the chosen Unreal rendering backend.

Prefer evaluating nDisplay/DisplayCluster before writing custom viewport infrastructure.

Map each machine's three view frustums to its three portrait 4K outputs.

---

## Milestone 7 — Installation Calibration

Add:

* Per-screen positional calibration.
* Per-screen angular calibration.
* Observer calibration.
* Persistent settings.
* On-screen diagnostics.

Validate the complete physical installation.

---

# Critical Implementation Rules for the Coding Agent

1. Never model tram riders as four independently controlled viewing pawns.

2. Treat all machines as render nodes for one virtual observer.

3. Keep tram simulation independent from display projection.

4. Keep physical screen identity separate from network player identity.

5. Use the spline and synchronized server time as the authoritative basis for tram transforms.

6. Do not rely solely on replicated Actor transforms for seam-critical motion.

7. Use identical camera-transform evaluation code on listen server and clients.

8. Treat each physical monitor as a separate projection surface in the production renderer.

9. Do not use one ordinary 90° perspective projection stretched across three angled monitors as the final display solution.

10. Allow the tram to launch without all rider slots being populated.

11. Allow late riders to synchronize and join without stopping the tram.

12. Keep all installation-specific dimensions and mappings configurable.

13. Use Unreal centimeters and physically meaningful dimensions where possible.

14. Prefer established Unreal multi-display/projection systems where they satisfy the requirement rather than duplicating them inside the plugin.

15. Build diagnostics from the beginning because synchronization errors of only a few milliseconds or centimeters may become visible at physical screen boundaries.
