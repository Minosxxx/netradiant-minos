# netradiant-minos

A custom build of [NetRadiant-custom](https://github.com/Garux/netradiant-custom) 1.6.0n for
Quake III Arena / ioquake3 mapping, with a live lighting preview, hotspot texturing, FBX export and
quality-of-life changes to the transform tools.

| | |
|---|---|
| **Source** | branch [`hotspot`](https://github.com/Minosxxx/netradiant-minos/tree/hotspot) (`master` mirrors upstream) |
| **Windows build** | [Actions](https://github.com/Minosxxx/netradiant-minos/actions) → latest green `build` run → **Windows-x86_64** artifact |
| **Full feature list** | [FEATURES.md](https://github.com/Minosxxx/netradiant-minos/blob/hotspot/FEATURES.md) |
| **Changelog** | [CHANGELOG-minos.md](https://github.com/Minosxxx/netradiant-minos/blob/hotspot/CHANGELOG-minos.md) |

## Features

### Lighting preview (View > Lighting Preview)
- Background q3map2 compile shown lit with vanilla Quake 3's renderer maths (overbright bits, gamma,
  intensity), so it matches the game.
- Draft → Final quality presets, auto update after edits, fast `-onlyents` relights when only lights
  change.
- Your `-light` options (`-gamma`, `-compensate`, `-dirty`, `-bounce`...) and the game's `r_` settings.
- Real sky boxes, sun and sky light; Lit / Lighting only / Fullbright views.
- Follows the editor camera or flies on its own; "Editor camera here" jumps the editor to its view.
- Leak warning with **Show leak in editor**.
- Picks up texture and model changes from the editor's refresh.

### Hotspot texturing (Surface Inspector)
- **Apply** gives each selected face the closest-shaped rectangle of a texture's hotspot sheet;
  **Grouped** treats parallel faces as one shape.
- `fit`, `tile_h` and `tile_v` rectangles; textures without a sheet tile as one horizontal strip.
- **Hotspot Editor** to draw, tag and auto-detect rectangles, saved as a `.hotspot` file.
- **Keep aspect: Width / Height** fit one axis and keep the texture's proportions.

### Transform tools
- **W / E / R** = move / rotate / scale.
- Rotate snapping (15° steps, Shift for the opposite) and scale snapping, as toolbar toggles.
- Gizmo on a model's origin, so models rotate around their exported pivot.
- Optional gizmo on the last selected object instead of the selection's centre.

### Models and entities
- Spawn points drawn as a standing player (Sarge by default, red / blue skins for CTF), built from
  the game's own model files.
- The texture browser's refresh button also reloads models from disk.

### FBX export (File menu)
- Export the selection or the whole map, with presets for Maya, Blender, raw Quake coordinates and
  metric Y-up.
- One object per entity and per model, materials with linked textures, tool faces optional.

### Interface
- Icon size slider (50–400%) for 4K screens.

---

## About NetRadiant-custom (upstream README)

The open-source, cross-platform level editor for id Tech based games.

NetRadiant-custom is a fork of NetRadiant (GtkRadiant 1.4&rarr;massive rewrite&rarr;1.5&rarr;NetRadiant&rarr;this)

---
![screenshot](https://github.com/Garux/netradiant-custom/blob/readme_files/radDarkShot.png?raw=true)
---

### Downloads

Ready-to-use packages are available in the [Releases section](https://github.com/Garux/netradiant-custom/releases).

### Supported games

Main focus is on Quake, Quake3 and Quake Live.

Though other normally supported games should work too. Releases include configs for the following games: Alien Arena, Darkplaces, Doom 3, Doombringer, Wolfenstein: Enemy Territory, Heretic II, Half-Life, Jedi Knight Jedi Academy, Jedi Knight II: Jedi Outcast, Kingpin, Neverball, Nexuiz, Open Arena, Project::OSiRiON, Prey, Quake II, Q3Rally, Quake 4, Quetoo, Smokin' Guns, Soldier of Fortune II - Double Helix, Star Trek Voyager : Elite Force, Tremulous, Turtle Arena, UFO:Alien Invasion, Unreal Arena, Unvanquished, Urban Terror, Warfork, Warsow, Return To Castle Wolfenstein, World of Padman, Xonotic, ZEQ2 Lite.

### Features

Development is focused on smoothing and tweaking editing process.

##### Random feature highlights

* WASD camera binds
* Fully supported editing in 3D view (brush and entity creation, all manipulating tools)
* Uniform merge algorithm, merging selected brushes, components and clipper points
* Free and robust vertex editing, also providing abilities to remove and insert vertices
* UV Tool (edits texture alignment of selected face or patch)
* Autocaulk
* Model browser
* Brush faces extrusion
* Left mouse button click tunnel selector, paint selector
* Numerous mouse shortcuts (see help->General->Mouse Shortcuts)
* Focus camera on selected (Tab)
* Snapped modes of manipulators
* Draggable renderable transform origin for manipulators
* Quick vertices drag / brush faces shear shortcut
* Simple shader editor
* Texture painting by drag
* Seamless brush face<->face, patch<->face texture paste
* Customizable keyboard shortcuts
* Customizable GUI themes, fonts
* MeshTex plugin
* Patch thicken
* All patch prefabs are created aligned to active projection
* Filters toolbar with extra functions on right mouse button click
* Viewports zoom in to mouse pointer
* \'all Supported formats\' default option in open dialogs
* Opening *.map, sent via cmd line (can assign *.map files in OS to be opened with radiant)
* Texture browser: show alpha transparency option
* Texture browser: search in directories and tags trees
* Texture browser: search in currently shown textures
* CSG Tool (aka shell modifier)
* Working region compilations (build a map with region enabled = compile regioned part only)
* QE tool in a component mode: perform drag w/o hitting any handle too
* Map info dialog: + Total patches, Ingame entities, Group entities, Ingame group entities counts
* Connected entities selector/walker
* Build->customize: list available build variables
* 50x faster light radius rendering
* Light power is adjustable by mouse drag
* Anisotropic textures filtering
* Optional MSAA in viewports
* New very fast entity names rendering system
* Support \'stupid quake bug\'
* Arbitrary texture projections for brushes and curves
* Fully working texture lock, supporting any affine transformation
* Texture locking during vertex and edge manipulations
* Brush resize (QE tool): reduce selected faces amount to most wanted ones
* Support brush formats, as toggleable preference: Axial projection, Brush primitives, Valve 220
* Autodetect brush type on map opening
* Automatic AP, BP and Valve220 brush types conversion on map Import and Paste
* New bbox styled manipulator, allowing any affine transform (move, rotate, scale, skew)
* rendering of Q3 shader based skyboxes
* Incredible number of fixes and options


##### Q3Map2:

* q3map_remapshader remaps anything fine, on all stages
* Automatic map packager (complete Q3 support)
* Report full / full pk3 path on file syntax errors
* Allowed simultaneous samples+filter use, makes sense
* -brightness 0..alot, def 1: mimics q3map_lightmapBrightness globally
* -contrast -255..255, def 0: lighting contrast
* -saturation light option
* -bouncecolorratio 0..1 (ratio of colorizing light sample by texture)
* -nolm - no lightmaps
* -novertex works, (0..1) sets globally
* -vertexscale
* New area lights backsplash algorithm (utilizing area lights instead of point ones)
* -backsplash (float)scale (float)distance: adjust area lights globally (real area lights have no backsplash)
* New slightly less careful, but much faster lightmaps packing algorithm (allocating... process)
* -extlmhacksize zero effort external lightmaps for Q3
* Valve220 mapformat autodetection and support
* Consistent brush content deduction with mixed face parameters
* Model shaders paths deduction
* Fixed model autoclip, added 20 new clipping modes
* Support negative misc_model scale
* Assimp model loading library (40+ formats)
* -json bsp export/import
* -mergebsp injects one bsp to another
* No shaderlist.txt mode: load all shaders

###### see changelog-custom.txt for more

### [COMPILING](/COMPILING)
