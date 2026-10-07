# Vision Stabilizer

A native Unreal Engine AnimGraph node that reduces positional head bob for true first-person characters. It keeps a persistent POV target inside a sphere centered on the animated head and writes the result to a dedicated virtual bone.

The character's real head and body keep their original animation. Attach your camera to the virtual POV bone and keep view input in your character's camera setup.

## Features

- Persistent component-space positional stabilization with an adjustable spherical dead zone.
- Manual clamp radius or a `CurveFloat` driven by normalized ground speed.
- A separate height offset applied immediately after filtering.
- Explicit reset input and support for animation dynamics resets.
- Game-thread curve snapshots for worker-thread evaluation.
- Compile-time bone validation and mode-aware AnimGraph pins.
- Debug visualization of the clamp envelope, incoming POV and stabilized target.
- Optional default curve generation through an editor console command or commandlet.
- Native automation tests using a synthetic skeleton, with no external character assets.

## Requirements

- The plugin descriptor targets **Unreal Engine 5.8**. Other engine versions may require source changes.
- A C++ toolchain configured for your Unreal Engine installation. This repository contains source; precompiled binaries are not included.
- A skeletal mesh and Animation Blueprint with a real head bone and a dedicated leaf virtual bone for the POV.

The plugin uses engine modules only. No third-party plugin dependencies, demo maps, character meshes or prebuilt curve assets are included.

Validated on **Unreal Engine 5.8.3 / Win64**: plugin builds passed for Development Editor, Development Game and Shipping Game, and plugin packaging completed successfully. All four included automation tests passed in a headless editor session. In-game camera behavior still needs verification with your own character setup.

## Installation

1. Copy or clone this repository into `<YourProject>/Plugins/VisionStabilizer`.
2. Confirm that `VisionStabilizer.uplugin` is directly inside that folder.
3. Generate project files and build your project's Editor target with the matching engine toolchain. For a Blueprint-only project, add a C++ class first if you need a project build target.
4. Open the project, enable **Vision Stabilizer** in the Plugins browser and restart if prompted.

```text
YourProject/
  YourProject.uproject
  Plugins/
    VisionStabilizer/
      VisionStabilizer.uplugin
      Config/
      Source/
```

## Quick start

### 1. Create the POV virtual bone

In your character's Skeleton Editor, add a virtual bone with **root** as the source and your animated **head** as the target. Rename it to `pov_head`; Unreal displays the full name as `VB pov_head`.

The default node settings use `head` and `VB pov_head`. Select your own names if the skeleton uses a different naming convention.

The POV bone must be a dedicated leaf with no child bones. Selecting the real head as the output, selecting a regular skinned output bone or using the same bone for both inputs produces a compilation error.

### 2. Add the node to the AnimGraph

Add **Vision Stabilizer** from the **Skeletal Controls** category after the animation operations that should contribute to the head pose. The node consumes a component-space pose:

```text
Final local-space pose
    -> Local To Component
    -> Vision Stabilizer
    -> Component To Local
    -> Output Pose
```

Set **Head Bone** to the real animated head and **POV Head Bone** to the virtual POV bone. Start with **Manual** mode, **POV Clamp = 5 cm** and **Alpha = 1**.

Later skeletal controls can change the final result. Place the stabilizer after controls whose head motion you want it to filter.

### 3. Attach the camera

Attach your first-person camera to the skeletal mesh using `VB pov_head` as the attachment bone. Camera relative transforms, control rotation, view input and update timing remain part of your character setup.

The target holds the component-space rotation and scale captured from the first valid incoming POV pose after a reset. If your camera uses controller-driven view rotation, configure that in the camera or character. The node does not implement look input or a camera manager.

## How the filter works

On the first valid evaluation after a reset, the incoming POV position seeds the persistent state. On subsequent evaluations, the filter uses its previous position rather than reseeding from the animation every frame.

Let `P` be that persistent position, `H` the current head position and `r` the clamp radius:

```text
If |P - H| <= r: keep P
Otherwise:      P = H + normalize(P - H) * r
```

Small head movements inside the sphere leave the target in place. When the head moves far enough, the target moves by the shortest distance needed to remain on the sphere's boundary.

- A larger radius absorbs more positional bob and permits more separation from the head.
- A zero radius follows the head position, plus any height offset.
- The filter uses mesh component space, so component movement and rotation still carry the target with the character.
- The algorithm has no spring, interpolation speed or delta-time term. Its result depends on the sequence of evaluated poses.

Only the virtual POV bone receives a target transform. Its rotation and scale come from the captured seed. The base skeletal-control node applies **Alpha once to the full transform**; at partial alpha, the final pose blends toward the incoming animation. The filter's internal position remains unblended.

## Settings

| Setting | Default | Behavior |
| --- | --- | --- |
| Head Bone | `head` | Real animated bone used as the sphere center; read only. |
| POV Head Bone | `VB pov_head` | Dedicated leaf virtual bone receiving the stabilized target. |
| Clamp Mode | Manual | Selects a scalar radius or ground-speed curve sampling. |
| POV Clamp | 5 cm | Non-negative radius in Manual mode; stored literal is the fallback in curve mode. |
| Ground Speed | 0 cm/s | Horizontal movement speed supplied by your Animation Blueprint. |
| POV Clamp Curve | None | X = normalized speed in `[0, 1]`; Y = radius in centimeters. |
| Min Ground Speed | 0 cm/s | Lower normalization bound. |
| Max Ground Speed | 600 cm/s | Upper normalization bound. |
| POV Height Offset | 0 cm | Signed offset along mesh component +Z, applied after filtering. |
| Reset Stabilization | False | Resets the captured state while true; pulse for one animation update. |
| Enable Debug Draw | False | Queues diagnostic primitives through the animation proxy in supported debug builds. |

**POV Clamp** and **Ground Speed** are exposed by default in their respective modes. Height offset and reset can be exposed as pins. Curve selection and speed bounds are configured in the node's Details panel. Alpha and LOD settings are inherited from Unreal's skeletal-control base node.

### Ground-speed curve mode

1. Create a `CurveFloat`, or generate the optional default curve below.
2. Select **Ground-Speed Curve** as the clamp mode.
3. Assign the curve and set the minimum and maximum ground speeds.
4. Feed **Ground Speed** from horizontal velocity magnitude, such as `Velocity.Size2D()`, in cm/s.

The curve input is:

```text
Speed01 = (clamp(GroundSpeed, MinGroundSpeed, MaxGroundSpeed) - MinGroundSpeed)
          / (MaxGroundSpeed - MinGroundSpeed)
Radius  = Curve(Speed01)
```

Runtime sanitizes the inputs before normalizing: negative or non-finite speed becomes zero, reversed bounds are sorted, and a collapsed range samples X = 0. Negative radii become zero. Missing curves, empty curves and non-finite samples use the stored Manual **POV Clamp** literal; a non-finite manual radius uses 5 cm.

**Changing clamp mode disconnects the inactive input pin and its Property Access binding.** An unlinked manual literal is retained as the fallback. A connected runtime value is not sampled or baked into that literal, and switching back does not restore the old wire. Set your fallback before switching modes and reconnect inputs when needed.

Curve data is copied on the game thread and sampled from an owned `FRichCurve` during worker evaluation. Editor key edits refresh that snapshot. In cooked builds, replacing the assigned curve asset refreshes the snapshot; in-place runtime edits to the same curve asset are not tracked.

### Generate the default curve

Run this in the editor's console to create a project-owned curve:

```text
VisionStabilizer.CreateDefaultCurve /Game/Curves/CF_VisionStabilizer_DefaultClamp
```

With no argument, the command uses `/VisionStabilizer/Curves/CF_VisionStabilizer_DefaultClamp` under the plugin's content mount. The destination must be mounted and writable. Enable **Show Plugin Content** in the Content Browser to find assets created there.

The generated curve uses linear interpolation and constant extrapolation:

| Normalized speed | Clamp radius |
| --- | --- |
| 0.0 | 2 cm |
| 0.5 | 4 cm |
| 1.0 | 6 cm |

Existing saved curves are returned unchanged. An object of another type, an unloadable existing package or an unsaved curve at the same object path causes the operation to fail without overwriting it.

For unattended generation, use the matching engine's editor commandlet after building the plugin. Example in PowerShell:

```powershell
& "C:\Path\To\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "C:\Path\To\YourProject.uproject" `
  -run=VisionStabilizerCreateAssets `
  -Package=/Game/Curves/CF_VisionStabilizer_DefaultClamp `
  -unattended -nop4
```

The commandlet exits with `0` for a created or existing saved curve, and `1` on failure. Asset generation is explicit; module startup does not create content.

### Reset behavior

Pulse **Reset Stabilization** for one animation update to capture a new incoming POV transform. Holding it true resets every active update.

Initialization, required-bone reinitialization, animation dynamics resets and detected interruptions in active updates also discard the previous state. When the control resumes after an alpha, LOD or graph traversal gap, the next valid pose supplies a new seed. Invalid input transforms reset the state and pass through without adding a new bone transform.

A world-space teleport alone does not change component-space filter coordinates. For a new seed, use the reset input or your animation instance's dynamics-reset path.

## Debugging

Enable **Debug Draw** on the node. Debug primitives require animation debug support and are compiled out of Shipping and Test configurations.

| Color | Meaning |
| --- | --- |
| Green | Head-centered clamp envelope and center. |
| Yellow | Incoming POV position. |
| Cyan | Stabilized target and the incoming-to-target correction line. |

All displayed points include the height offset. The clamp envelope is built in component space and transformed point by point, so non-uniform mesh scale is reflected in its world-space shape.

AnimGraph debug text reports bone names, mode, radius, normalized speed, correction distance, initialization state and manual fallback use.

| Symptom | Check |
| --- | --- |
| Node does not appear | Plugin enabled, Editor target rebuilt, and search performed inside an AnimGraph. |
| Bone validation fails | Head is real; POV is a distinct virtual leaf present in the Blueprint's skeleton. |
| Camera still follows head bob | Camera attachment uses the POV bone, Alpha is relevant, and no later control overwrites the result. |
| Too much separation from the head | Reduce the radius or adjust the speed curve. |
| Curve uses the manual value | Curve assigned, keys present, sample finite; review compiler warnings and fallback debug text. |
| Input wire disappears after changing mode | Expected mode-switch behavior; reconnect the active input. |
| Height offset moves in an unexpected direction | Offset follows mesh component +Z, which may differ from world up. |

## Source layout

```text
Source/
  VisionStabilizer/             Runtime node, standalone math and curve tests
  VisionStabilizerEditor/       AnimGraph tooling, asset creation and pose/editor tests
Config/
  FilterPlugin.ini              Additional files included in plugin packaging
```

`VisionStabilizer` depends on `Core`, `CoreUObject`, `Engine` and `AnimGraphRuntime`. `VisionStabilizerEditor` is an `UncookedOnly` module containing graph tooling and editor asset operations; cooked evaluation uses the runtime module.

Header comments use Doxygen syntax. The engine-independent implementation is in [VisionStabilizerCore.h](Source/VisionStabilizer/Public/VisionStabilizerCore.h), and the runtime settings are declared in [AnimNode_VisionStabilizer.h](Source/VisionStabilizer/Public/AnimNodes/AnimNode_VisionStabilizer.h).

## Tests

Build a Development Editor target with development automation tests enabled. In the editor's automation test browser, filter for `VisionStabilizer`, or run:

```text
Automation RunTests VisionStabilizer
```

The included tests cover:

- `VisionStabilizer.Runtime.CurveSnapshotAndFallback`: manual radius, normalized curve sampling, snapshot ownership, editor refresh and invalid-value fallback.
- `VisionStabilizer.Runtime.PoseContract`: single-bone output, persistent position, captured rotation/scale, height offset, resets and unsafe output-bone rejection.
- `VisionStabilizer.Editor.BoneValidation`: valid and invalid skeleton selections, plus missing-curve warnings.
- `VisionStabilizer.Editor.ModePinReconstruction`: mode-specific visibility, old-wire removal and retained manual fallback literals.

Run these tests in your target project and verify camera behavior with your own skeleton and animation stack before release.

## License

MIT. See [LICENSE](LICENSE).
