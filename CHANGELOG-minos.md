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

### Hotspot texturing (Surface Inspector)
- Hotspot Apply: each selected face gets the closest-shaped rectangle of the texture's hotspot
  sheet. The top row tiles horizontally.
- Grouped mode: parallel faces are treated as one shape.
- Hotspot Editor window to draw and tag the rectangles; saves a `.hotspot` file next to the texture.
- Textures without a `.hotspot` file are treated as one horizontal tiling strip.
- Keep aspect: "Width" / "Height" fit buttons fit one axis and keep the texture's aspect ratio.

### Editing
- **New:** W / E / R = move / rotate / scale. Edge selection moves from E to Y. Shortcut files
  that still have the old defaults are switched once; customised ones are left alone.
- **New:** toolbar toggles for rotate snapping and scale snapping.
  - Rotate snapping is on by default in 15° steps; set the step in Preferences > Selection.
  - Holding Shift while dragging does the opposite.
  - Scale snapping sizes in grid steps; turn it off for free scaling.
- Pivot at entity origin: the gizmo uses the entity's origin, so models rotate around their
  exported pivot. Option in Preferences > Selection.
- **New:** the texture browser's refresh button also reloads all models from disk (re-exported
  .md3 / .ase files show up without reopening the map). "Refresh Models" now always reloads,
  including models inside .pk3s.

### Interface
- Icon size slider in Preferences > Layout (50-400%) for high-DPI / 4K screens.

### Build
- GitHub Actions builds a Windows (MSYS2 MINGW64) package on every push to `hotspot`.

## Companion tool: MD3 Exporter for Maya (separate package)
- Exports static meshes from Maya 2022-2025 as Quake 3 `.md3`.
- Export groups (pivot locators): everything under a group is merged into one model, measured from
  the locator. Created with "Create Export Group", which asks for the model's name.
- Per-scene game folder (asked for when unknown) and models folder, kept inside `baseq3/models`.
- Validation of the renderer limits (1000 verts / 2000 tris per surface, 32 surfaces, ±512 units)
  with automatic surface splitting.
- Shader names from texture paths or material names, with per-material overrides; `tag_*` locators
  are exported as tags.
- The window follows the scene (new, deleted or renamed groups, undo, opening another scene).
- Maya-style dark interface with icons. The export group and material lists are darker than the
  panel, with alternating row shades.
