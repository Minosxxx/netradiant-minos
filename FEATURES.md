# netradiant-minos: features

A custom build of [NetRadiant-custom](https://github.com/Garux/netradiant-custom) 1.6.0n
(commit `5daf2e26`, March 2024) for Quake III Arena / ioquake3 mapping. Everything below is on top
of the regular editor; existing maps, brushes and workflows are unchanged.

- Source: branch [`hotspot`](https://github.com/Minosxxx/netradiant-minos/tree/hotspot)
- Windows builds: [Actions](https://github.com/Minosxxx/netradiant-minos/actions) → latest green
  `build` run → **Windows-x86_64** artifact
- Change history: [CHANGELOG-minos.md](https://github.com/Minosxxx/netradiant-minos/blob/hotspot/CHANGELOG-minos.md)

## Contents

1. [Lighting preview](#1-lighting-preview)
2. [Hotspot texturing](#2-hotspot-texturing)
3. [Transform tools and gizmo](#3-transform-tools-and-gizmo)
4. [Models and entities](#4-models-and-entities)
5. [FBX export](#5-fbx-export)
6. [Interface](#6-interface)
7. [Build and packaging](#7-build-and-packaging)

---

## 1. Lighting preview

**View > Lighting Preview** opens a window that compiles the map with q3map2 in the background and
shows it lit the way vanilla Quake 3 draws it: lightmap × texture, overbright bits, gamma and
intensity, using the same maths as the game's renderer. Side by side with the game, it matches.

**Toolbar**
- **Compile** (F5), **Quality** (Draft / Medium / High / Final) and **Auto update** after every edit.
- **Show**: Lit, Lighting only, Fullbright.
- **Follow editor camera**, or fly the preview on its own: drag to look, WASD to move, Q/E down/up,
  Shift faster, wheel for speed. **Editor camera here** moves the editor's camera to the preview's view.
- **Settings** and **Log** (the full q3map2 output, with per-step timings).

**Settings**
- q3map2 `-light` options applied to every quality preset, e.g.
  `-gamma 2.2 -compensate 3 -dirty -dirtmode 1 -dirtscale 3 -dirtdepth 20 -bounce 4`.
  **Import from build menu** copies them from your build commands.
- Per-preset options and the BSP-stage options.
- The game's renderer settings: `r_mapOverBrightBits`, `r_overBrightBits`, `r_gamma`, `r_intensity`
  and fullscreen / hardware gamma, with **Reset to game defaults**.
- **Reload textures**.

**What it handles**
- Sun and sky light from sky shaders, and real sky boxes, drawn the way the game maps them.
- Fast updates: when only lights or other point entities change, it relights with `-onlyents`
  instead of rebuilding the BSP. Models (`misc_model`), decals and skybox entities are compiled into
  the BSP, so changing those runs the full compile.
- **Leak warning**: when the map leaks, an amber bar appears with **Show leak in editor**, which draws
  the leak line in the editor views and moves the camera to it (Ctrl+Shift+K / Ctrl+Shift+L step
  along it). While the map leaks, every update runs the full compile, so the bar clears as soon as
  the hole is fixed.
- Refreshing textures or models in the editor updates the preview too: textures reload at once,
  reloaded models trigger a full compile.
- Diagnostics: light counts (point, spot, area, sun/sky), sky shaders used and compile times.
- Game profiles keep everything game-specific in one place; Quake 3 (`baseq3`, `missionpack`) is
  built in and other games can be added.

## 2. Hotspot texturing

Texture faces from a sheet of predefined rectangles ("hotspots"): each face gets the rectangle closest
to its shape. Brushes stay normal brushes; only their texture alignment changes.

**Surface Inspector**
- **Hotspot: Apply** picks the best rectangle for each selected face.
- **Grouped** treats parallel selected faces as one shape, so a strip of faces shares one rectangle.
- **Edit...** opens the Hotspot Editor for the current texture.
- **Keep aspect: Width / Height** fit the texture to one axis of the face and keep its aspect ratio.

**Rectangle modes**
- `fit`: the texture region is stretched to the face.
- `tile_h` / `tile_v`: the region tiles horizontally / vertically. A trim sheet's top row is
  typically `tile_h`.
- Textures without a `.hotspot` file are treated as one horizontal tiling strip.

**Hotspot Editor**
- Draw, move and resize rectangles on the texture; Shift+drag draws over existing ones.
- Set each rectangle's mode, a sheet-wide inset (avoids bleeding from neighbours) and a snap grid.
- **Auto-detect from gutters** finds the cells between dark gutter lines.
- **Save** (Ctrl+S), **Save + Apply**, **Save + Apply grouped**.
- Wheel zoom, right/middle-drag pan, F to fit, Del to delete, arrows to nudge (Shift = 8×).

**.hotspot file** (saved next to the texture, plain text):

```
size  <width> <height>           # reference image size (optional)
inset <pixels>                   # shrink every rect (optional)
rect  <x> <y> <w> <h> [mode]     # mode: fit (default) | tile_h | tile_v
```

## 3. Transform tools and gizmo

- **W / E / R = move / rotate / scale**, as in Maya. Edge selection moves from E to Y. Shortcut files
  with the old defaults are switched once; customised shortcuts are left alone.
- **Rotate snapping** (toolbar toggle, on by default): 15° steps, set in Preferences > Selection.
  Holding Shift while dragging does the opposite.
- **Scale snapping** (toolbar toggle): sizes change in grid steps; off for free scaling.
- **Pivot at entity origin** (Preferences > Selection): with one point entity selected, the gizmo
  sits on its origin, so models rotate around their exported pivot.
- **Gizmo on the last selected object** (toolbar toggle and Preferences > Selection): with several
  objects selected, the gizmo goes on the last one instead of the centre of the selection; point
  entities use their origin, brushes the centre of their bounds.
- Fixed: a selected light's gizmo appeared at the map origin instead of on the light.

## 4. Models and entities

- **Spawn points show a player**: `info_player_deathmatch`, `info_player_start` and the CTF spawns
  are drawn as a player standing in the idle pose, with the default skin (red / blue skins on CTF
  spawns). The model is assembled on the fly from the game's own lower / upper / head .md3 files, the
  same way the game attaches them, so nothing is copied out of the pk3s.
  - Choose the player in Preferences > Display > Spawn Points (default `sarge`; empty for boxes;
    applies after a restart).
  - If that player model isn't found, spawn points stay as normal boxes.
- **Reload models from disk**: the texture browser's refresh button also reloads every model, so a
  re-exported .md3 / .ase shows up without reopening the map. The toolbar's **Refresh Models** now
  always reloads every model, including those inside .pk3s and files rewritten within the same second.

## 5. FBX export

**File > Export selected to FBX...** and **File > Export map to FBX...** write binary FBX 7.4.

| Target | Up axis | Scale |
|---|---|---|
| Maya | Y | 1 unit = 1 cm (1:1 with Quake units) |
| Blender | Z (Blender's own) | 1 unit = 1/32 m (player ≈ 1.75 m) |
| Quake | Z | raw map coordinates |
| Metric Y-up | Y | 1 unit = 1/32 m |

- One object per entity (worldspawn, each func_group, each brush entity) and one per model, with
  each model's pivot at its entity origin.
- Brush faces are welded polygons with hard edges, patches are quads, models are triangles.
- One material per shader, with its texture linked by file path. Textures that only exist inside a
  .pk3 are named but not linked.
- Tool faces (caulk, clip, nodraw, triggers, sky, fog) are skipped unless you tick the option.
- Hidden and filtered objects, lights and editor-only models (item and spawn previews) are left out.

## 6. Interface

- **Icon size slider** in Preferences > Layout (50–400%), for high-DPI / 4K screens. Icons are
  enlarged pixel for pixel, so they stay crisp instead of blurring.

## 7. Build and packaging

- GitHub Actions builds a Windows x86_64 package (MSYS2 MINGW64) on every push to `hotspot`.
- Compile errors appear as annotations on the run, and the full build log is uploaded with it.
