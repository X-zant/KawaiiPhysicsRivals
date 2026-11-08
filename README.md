# Kawaii Physics for Marvel Rivals modding

A version of the [Kawaii Physics](https://github.com/pafuhana1213/KawaiiPhysics) plugin for
**Unreal Engine 5.3.2** that lines up exactly with the version Marvel Rivals uses.

Because it matches the game, Kawaii Physics settings you make or import in the editor are saved
in a form the game reads correctly. A mismatched plugin can load the wrong values without
showing any error.

---

## Install

1. Put the `KawaiiPhysics` folder in your project's `Plugins` folder.
2. Open the project.

Ready-built files for Windows are included, so you don't need to compile anything. If Unreal
asks to rebuild the plugin, delete the `Binaries` folder and let it rebuild.

**Using [RivalsNotifyCommunityKit](https://github.com/XzantGaming/RivalsNotifyCommunityKit)?**
Put both plugins in `Plugins`. They are designed to work together.

**Only one Kawaii Physics plugin can be installed at a time.** Remove any other copy first,
or the editor will fail to load it.

---

## Importing physics setups from the game

You can bring a character's physics setup over from the game using an
[FModel](https://fmodel.app/) export.

1. In FModel, find the character's physics file, for example
   `Marvel/Content/Marvel/Characters/1014/1014001/Post_1014001_Physics`.
2. Save the **whole file** as JSON (not a single object inside it).
3. In Unreal, do one of these:
   - **Drag the `.json` into the Content Browser.** The nodes are copied to your clipboard.
     Open an Anim Graph and press **Ctrl+V**.
   - **Right-click an Anim Blueprint → Import Kawaii Physics from JSON…** The nodes go
     straight into that Blueprint.
4. Connect your graph's **Input Pose** and **Output Pose** to the open ends of the imported
   nodes.

Everything between the input and output comes across, including the other animation nodes
the game uses alongside Kawaii Physics.

### Placeholder assets: don't pack them

Some settings point to other game files, such as collision data assets and pose assets.
To keep those links working, the importer creates **empty placeholder files** at the same
paths as the game's. The game uses its own real files at runtime.

**Leave the placeholders out when you pack your mod.** Otherwise they replace the game's real
files with empty ones. The import message lists every placeholder it created.

---

## How close is the preview to the game?

The physics follows the game's own version closely, so what you see in the editor should be
close to the game. A few tiny rounding differences remain.

It has been tested automatically inside the engine: chains swing, droop under gravity,
settle back, collide, and keep their length. It has not been compared side by side with the
game, so treat the preview as a close guide, not a guarantee.

What ends up in the game always comes from your saved settings, not the preview: the game
runs its own copy of the physics.

**Not supported yet:**

- The `CapsuleBone` collision type is saved correctly but does nothing in the preview.
- Very old Kawaii Physics nodes that use separate curve assets need their curves re-made by
  hand. The editor shows a warning when this applies.

---

## Building from source

Put the plugin in your project's `Plugins` folder and build the project as normal with UE 5.3.2.

**If the build fails with an error in `ConcurrentLinearAllocator.h`**, your Visual Studio
compiler is newer than UE 5.3 expects. Either:

- install the older compiler: **MSVC v143 – VS 2022 C++ x64/x86 build tools (v14.38)** in
  the Visual Studio Installer, or
- add `-Define:PLATFORM_HAS_ASAN_INCLUDE=0` to your build command.

---

## Licence

Kawaii Physics is MIT licensed, © 2019-2026 pafuhana1213 (see `LICENSE`). The changes in this
version use the same licence.

This plugin contains no game code, assets or other content from Marvel Rivals.
