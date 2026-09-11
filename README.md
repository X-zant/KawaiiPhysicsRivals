# KawaiiPhysics, rebuilt against the game's schema

A UE 5.3.2 build of Kawaii Physics whose reflected types match, exactly, the
`KawaiiPhysics` module in the shipped game (`5.3.2-3761137+++depot_marvel+S9.5_release-Marvel`).

Based on upstream [pafuhana1213/KawaiiPhysics](https://github.com/pafuhana1213/KawaiiPhysics)
**v1.18.0** (MIT), which is the version the game's fork derives from.

---

## Install

Drop this folder into `<YourProject>/Plugins/` and open the project. That is the whole install.

Precompiled `Win64` editor binaries for **UE 5.3.2** are included, so a Blueprint-only project
needs no compiler. They are built against engine `BuildId 27405482`; if your engine reports a
different one, delete `Binaries/` and let your project rebuild the plugin from `Source/`.

> **Using it with [RivalsNotifyCommunityKit](https://github.com/XzantGaming/RivalsNotifyCommunityKit)?**
> Just drop both into `Plugins/`. That kit declares this plugin as a required dependency and
> ships no `KawaiiPhysics` module of its own, so there is nothing to reconcile — the two anim
> notify classes it needs (`UAnimNotify_KawaiiPhysicsAddExternalForce` and its NotifyState
> counterpart) come from here.

---

## 1. What "compatible" means here, precisely

The game cooks with **unversioned property serialization**. Under that scheme a
property is identified purely by its **index in the owning struct's schema**, and
that index is its **declaration order in C++**. There are no names in the cooked
bytes to fall back on.

So compatibility is not "has the same fields somewhere". It is:

- every `UENUM` / `USTRUCT` / `UCLASS` present, under the same name;
- every property present, same name, same type, **same position**;
- same inheritance, since a super's properties occupy the leading indices.

**Why this matters:** get one property out of position and every property after
it silently shifts. Assets still load. They load *wrong* — a float lands where a
bool was — and it does not fail loudly.

Every `UPROPERTY` in `AnimNode_KawaiiPhysics.h` carries its schema index in a comment
(`[0]`, `[1]`, …) for exactly this reason. **Declaration order is load-bearing. Do not
reorder members.**

## 2. Ground truth, and how it was established

| Source | Used for |
|---|---|
| the game's `*.usmap` mappings | the authoritative schema: names, types, order |
| Dumper-7 `CppSDK` headers | memory offsets and property flags |
| upstream v1.18.0 | the code being ported |

Two facts had to be settled before a single header could be written, and both
were settled against the game's own data rather than assumed:

**Declaration order is the schema order.** The usmap's property indices were
compared against the Dumper-7 SDK's memory offsets across every type in the
package. They agree, monotonically, everywhere.

**UHT strips the `_DEPRECATED` suffix.** The game's node carries 49 deprecated
properties reflected under bare names (`RootBone`, `ExcludeBones`, …) with
`CPF_Deprecated` set. Scanning all 31,996 types in the usmap finds **zero**
properties whose reflected name ends in `_DEPRECATED`, and stock UE's
`USkinnedMeshComponent::SkeletalMesh_DEPRECATED` appears as plain `SkeletalMesh`.
So declaring `FBoneReference RootBone_DEPRECATED;` yields exactly what the game
has: name `RootBone`, deprecated flag set, never written back on save.

Corollary the plugin obeys throughout: `WITH_EDITORONLY_DATA` and `ENABLE_ANIM_DEBUG`
properties are compiled out of the shipped game, so they must be declared **after**
all runtime properties of their struct.

`Docs/game-kawaii-schema.txt` is the extracted reference schema.

## 3. How the game's fork differs from upstream

The headline change is a **multi-chain refactor**. Upstream carries one chain's
settings flat on the node; the fork holds `TArray<FKawaiiPhysicsChain> Chains`
and runs the solver per entry. The old flat properties survive as the deprecated
block, and `UpgradeDeprecatedProperties()` folds them into `Chains[0]` on load.

Fork-only features reproduced here:

| Feature | Where |
|---|---|
| Multi-chain (`Chains`) | `FKawaiiPhysicsChain` |
| Merge node (shared limits/constraints across chains) | `FAnimNode_KawaiiPhysicsMerge` |
| Wave animation | `FWaveAnimSettings` |
| Per-bone custom colliders | `FBoneShape`, `EBoneShapeType` |
| `SpringStiffness` | `FBonePhysicsParams` |
| Relative-rotation limits (6 half-axes) | `FBonePhysicsParams` |
| `LimitLinear`, `GravityScale` | `FKawaiiPhysicsSettings` |
| `CapsuleBone` collision type | `ECollisionLimitType` |
| Parent-component-driven limits, collision slots | `bUseParentCompBone`, `ParentDrivingBoneName`, `SlotID` |
| Fixed / follow bones, tail pinning | `FBoneSettings` |
| Per-chain LOD threshold, cached net role | `FKawaiiPhysicsChain` |
| `ChainIndex` on every per-chain Blueprint accessor | `UKawaiiPhysicsLibrary` |

---

## 4. Importing setups from the shipped game

Export a cooked `Post_<SkinID>_<CharID>_Physics` with FModel, then use either route.

### Drag the `.json` into the Content Browser

The whole graph is converted and placed on your clipboard as UE node text (T3D).
Open any AnimGraph and **Ctrl+V**. No asset is created — where the nodes belong is
your choice, not a guess this plugin should make.

This needs no skeleton, so it works before you have extracted the mesh.

### Right-click an Anim Blueprint → *Import Kawaii Physics from JSON…*

Same conversion, but the nodes go straight into that Blueprint's AnimGraph.

### How to export from FModel

Locate e.g. `Marvel/Content/Marvel/Characters/1014/1014001/Post_1014001_Physics` and save
the whole **package** as JSON — not a single object. The importer needs the
`AnimBlueprintGeneratedClass` (which declares the nodes) *and* the `Default__…`
object (which holds their values).

### What comes across

All of it. Every property the game's Kawaii Physics types declare lands on the matching
`UPROPERTY`, because of §1 — FModel writes property names and nothing has to translate
or guess.

`FInstancedStruct` is the one case needing real work: FModel emits external-force presets
as flat objects with **no type tag**, and `FInstancedStruct` has no reflected properties
of its own. The concrete type is inferred from the fields present (`ForceCurve` → Curve,
`OverrideGravityDirection` → Gravity, `ForceRateByBoneLengthRate` → Wind, …) and the
struct is initialised as that type.

`UKawaiiPhysicsJsonImporter::AuditJsonCoverage` walks every field in an export and reports
anything with no matching property, so "did all of it come across?" is a measurement
rather than a belief.

**Graph wiring** is recovered from each node's `ComponentPose.LinkID`, an index into the
generated class's anim-node properties in declaration order, counting only `AnimGraphNode_*`
entries.

### Referenced assets are stubbed, not dropped

`LimitsDataAsset`, `PhysicsAssetForLimits`, `BoneConstraintsDataAsset` and Pose Driver
pose assets point at cooked game assets that are not in your project. An empty placeholder
of the right class is created **at the game's own package path** and saved to disk, so the
reference serializes correctly; the packaged game resolves it against its own paks at
runtime. The contents are irrelevant — only the path, name and class matter.

> **Exclude placeholders when you pack.** Shipping a stub at the same path as the game's
> real asset overrides it with an empty one. Every placeholder is listed by path in the
> import result and the completion dialog.

### Which nodes it recreates

Node types are read from the export, not guessed: the generated class names the exact
struct behind every node property.

Coverage is driven by what the game actually ships, not by guesswork. Every anim node
across all **3,531** cooked `*_Physics` packages in the paks:

| Node | Instances | Packages | Imported |
|---|---:|---:|:---:|
| `AnimNode_KawaiiPhysics` | 6760 | 507 | ✅ |
| `AnimNode_Constraint` | 1533 | 202 | ✅ |
| `AnimNode_PoseDriver` | 593 | 257 | ✅ |
| `AnimNode_ModifyBone` | 305 | 104 | ✅ |
| `AnimNode_KawaiiPhysicsMerge` | 142 | 43 | ✅ |
| `AnimNode_CopyBone` | 2 | 2 | ✅ |
| `AnimNode_Root` | 622 | 622 | graph plumbing |
| `AnimNode_LinkedInputPose` | 622 | 622 | graph plumbing |
| `AnimNode_ConvertComponentToLocalSpace` | 602 | 588 | graph plumbing |
| `AnimNode_ConvertLocalToComponentSpace` | 602 | 588 | graph plumbing |
| `AnimNode_UseCachedPose` | 72 | 36 | graph plumbing |
| `AnimNode_LayeredBoneBlend` | 45 | 36 | ❌ see gaps |
| `AnimNode_SaveCachedPose` | 40 | 40 | graph plumbing |
| `AnimNode_BlendListByInt` | 36 | 36 | ❌ see gaps |
| `AnimNode_ApplyAdditive` | 4 | 4 | ❌ see gaps |
| `AnimNode_SequencePlayer` | 4 | 4 | ❌ see gaps |

"Graph plumbing" is the pose routing a physics graph is wired through
(`LinkedInputPose` → `LocalToComponentSpace` → … → `ComponentToLocalSpace` → `Root`).
It is stock UE, identical in every such asset, and how your own Blueprint should be wired
is your call — so it is reported rather than recreated.

The unhandled ones all take **multiple** pose inputs, which the single `ComponentPose`
link-restoration path does not cover. They are rare, and never carry Kawaii data
themselves.

Anything skipped is **named** in the result (`SkippedNodeTypes`) and in the dialog, so a
partial graph is never silent.

The census is reproducible: `SchemaCheck --scan-nodes` walks the paks with CUE4Parse and
tallies node structs the same way the importer reads them.

### What it deliberately does not do

- **Does not connect the chain to your graph.** The head and tail pins are left open. The
  game feeds these through LinkedInputPose → LocalToComponentSpace and back out; how your
  Blueprint should do that is your call.
- **Does not import `ModifyBones`.** That is runtime state, rebuilt from the skeleton on
  first evaluation.

Covered by an automation test whose fixture mirrors real FModel output:

```
UnrealEditor-Cmd.exe <YourProject>.uproject -ExecCmds="Automation RunTests KawaiiPhysics.JsonImporter.ParseNode; Quit" -unattended -nopause -nosplash -nullrhi
```

---

## 5. Building from source

Drop into `<YourProject>/Plugins/` and build normally. Verified against UE 5.3.2 (CL 29314046):
clean from-scratch build, **zero errors and zero warnings**. UHT accepts all headers with
`-WarningsAsErrors`.

### If your build dies in `ConcurrentLinearAllocator.h`

A toolchain mismatch, not a plugin bug. UE 5.3's preferred MSVC toolsets top out around
**14.38**; VS 17.14 installs **14.44**. `Platform.h` sets `PLATFORM_HAS_ASAN_INCLUDE` from
`__has_include(<sanitizer/asan_interface.h>)`. Newer MSVC *ships* that header, so the macro
flips on for the first time and exposes `#elif __has_feature(address_sanitizer)` — a
Clang-only builtin. Under MSVC that gives C4668, which UE escalates via `/we4668`. It breaks
`SharedPCH.Engine`/`SharedPCH.UnrealEd`, so no per-module setting can reach it.

**Proper fix:** install MSVC v14.38 ("MSVC v143 — VS 2022 C++ x64/x86 build tools
(v14.38-17.8)" in the VS Build Tools installer).

**Workaround:** add `-Define:PLATFORM_HAS_ASAN_INCLUDE=0` to the UBT command line. That
restores exactly the state the prebuilt engine binaries were compiled in — with ASan off,
both branches no-op identically. Nothing in the plugin depends on it either way.

---

## 6. Status — read this before relying on it

**Schema: exact and verified.** 48/48 types, every property name, type and index,
machine-checked against the game's usmap in both directions (a deleted type is a failure,
not a silent pass). This is the part that determines whether assets round-trip.

**Compiles clean against stock UE 5.3.2**, and both modules load in a real Blueprint-only
project. JSON import is verified end-to-end there.

**JSON import is tested.** The automation test covers enum-qualifier rewriting, the
fork-only limit fields, stripped runtime state, external-force type inference, and
unresolved references.

**The simulation has never been run.** No chain has been evaluated in the editor or in
game. Treat runtime behaviour as untested.

**Simulation behaviour is a faithful port, not a bit-exact reproduction.** The solver —
Verlet-style integration, framerate-corrected damping/stiffness, cone and linear limits,
sphere/capsule/box/plane collision, XPBD distance constraints — is ported from upstream and
is correct in kind. The fork-only features (wave animation, spring stiffness,
relative-rotation limits, follow bones) are implemented from their schema and names;
NetEase's exact formulas are not observable from a data dump, so numbers will differ.

For asset work this rarely matters: **mods ship cooked assets, and the game runs its own
compiled KawaiiPhysics.** The plugin's job is to make the editor read and write the right
bytes. Editor preview fidelity is a convenience.

**Known gaps, deliberate:**

- `FAnimNode_KawaiiPhysicsMerge` serializes `ExternalForces` / `CustomExternalForces`
  (they are in the schema) but does not evaluate them; its own `Gravity` and `Wind` are
  applied. The force virtuals are declared against `FAnimNode_KawaiiPhysics` and a chain,
  which the Merge node does not have. The AnimGraph node emits a compile note rather than
  failing quietly.
- `ECollisionLimitType::CapsuleBone` is in the enum and serializes correctly, but no
  bone-driven capsule collision path is implemented — authored capsules work.
- `FBoneShape` / `CustomShapes` serialize; they are not yet fed into collision.
- Legacy `UCurveFloat` references on pre-refactor nodes are not auto-migrated to inline
  curves; the node logs a warning naming the problem.
- Multi-pose-input nodes are not recreated on import: `LayeredBoneBlend` (45 instances
  game-wide), `BlendListByInt` (36), `ApplyAdditive` (4), `SequencePlayer` (4). Link
  restoration follows a single `ComponentPose`, so a node with several pose inputs cannot
  be rewired from `LinkID` alone. Kawaii nodes in such a graph still convert; the blend
  node is reported as skipped.
- Inside `CollisionChannelSettings`, a few `FBodyInstance` fields the game extends
  (`AccelerationStructureType`, `PenetrationDepth`, `GameTraceChannel19`–`23`, …) have no
  stock-engine counterpart and do not import. No observed export sets
  `bOverrideCollisionParams`, so this is currently inert.

---

## 7. Licence

Upstream Kawaii Physics is MIT, © 2019-2026 pafuhana1213 — see `LICENSE`. Ported files keep
that notice, and the changes in this port are released under the same licence.

This port exists to interoperate with assets you already have. It contains **no game code,
assets or content**; the schema it reproduces is a description of a data format, read from a
public SDK dump.
