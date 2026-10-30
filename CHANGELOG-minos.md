# netradiant-minos changelog

Changes in this fork (branch `hotspot`) on top of NetRadiant-custom 1.6.0n (5daf2e26).
Newest first within each section.

## Unreleased

### Lighting preview (View > Lighting Preview)
- Background q3map2 compile shown lit with the vanilla Quake 3 renderer math (overbright bits,
  gamma, lightmap x texture), so the preview matches the game. Quality presets from Draft to Final.
- Auto update after edits. Light-only changes relight with `-onlyents` instead of a full compile.
- The preview camera can follow the editor camera, and "Editor camera here" moves the editor camera
  to the preview's view. F5 compiles.
- Real sky boxes, drawn the way the game maps them.
- Your look options (`-gamma`, `-compensate`, `-dirty...`, `-bounce` ...) apply to every quality preset.
- Diagnostics: per-step timings, light counts, sky shaders used and a compile log.
- Profiles make other games easy to add.
- **New:** warning bar when the map leaks, with a "Show leak in editor" button that draws the leak
  line in the editor views and jumps the camera to it. While the map leaks, every update runs the
  full BSP compile, so the warning clears as soon as the hole is fixed.
- **Fixed:** moving, copying or deleting models (`misc_model`), decals or skybox entities didn't
  update the preview until a brush changed.
- **Fixed:** leak detection never triggered (it looked for "LEAKED"; q3map2 prints "leaked").
- Reloading models (see below) rebuilds the preview, since models are compiled into the map.
- **Fixed:** the preview kept showing old textures after they were changed on disk; the texture
  browser's refresh button now reloads them in the preview too, without a recompile.

### Hotspot texturing (Surface Inspector)
- Hotspot Apply: each selected face gets the closest-shaped rectangle of the texture's hotspot
  sheet. The top row tiles horizontally.
- Grouped mode: parallel faces are treated as one shape.
- Hotspot Editor window to draw and tag the rectangles; saves a `.hotspot` file next to the texture.
- Textures without a `.hotspot` file are treated as one horizontal tiling strip.
- Keep aspect: "Width" / "Height" fit buttons fit one axis and keep the texture's aspect ratio.

### Editing
- **New:** spawn points (`info_player_deathmatch`, `info_player_start`, CTF spawns) are drawn as a
  player standing in its idle pose, with the default skin (red / blue skins for CTF spawns). The
  model is assembled from the game's own player files (lower / upper / head .md3), so nothing is
  copied out of the pk3s. Choose the player in Preferences > Display > Spawn Points (default Sarge;
  empty for plain boxes). Without that player model, spawn points stay boxes.
- **New:** W / E / R = move / rotate / scale. Edge selection moves from E to Y. Shortcut files
  that still have the old defaults are switched once; customised ones are left alone.
- **New:** toolbar toggles for rotate snapping and scale snapping.
  - Rotate snapping is on by default in 15° steps; set the step in Preferences > Selection.
  - Holding Shift while dragging does the opposite.
  - Scale snapping sizes in grid steps; turn it off for free scaling.
- Pivot at entity origin: the gizmo uses the entity's origin, so models rotate around their
  exported pivot. Option in Preferences > Selection.
- **New:** toolbar toggle (and option in Preferences > Selection) to put the gizmo on the last
  selected object when several are selected, instead of the centre of the selection. Point
  entities use their origin, brushes the centre of their bounds.
- **Fixed:** the move/rotate/scale gizmo of a selected light appeared at the map origin instead of
  on the light.
- **New:** the texture browser's refresh button also reloads all models from disk (re-exported
  .md3 / .ase files show up without reopening the map). "Refresh Models" now always reloads,
  including models inside .pk3s.

### Interface
- Icon size slider in Preferences > Layout (50-400%) for high-DPI / 4K screens.

### Build
- GitHub Actions builds a Windows (MSYS2 MINGW64) package on every push to `hotspot`.
