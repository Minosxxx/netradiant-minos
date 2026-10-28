/*
   Light preview: compiled BSP loader.

   Reads the parts of a compiled map needed to draw it lit: shaders, draw surfaces,
   vertices with lightmap coordinates, and lightmap pages. Patches are tessellated here.
   No editor dependencies, so it can be tested standalone.

   Formats are pluggable (see BspFormat); currently: Quake 3 / RTCW / ET "IBSP" (versions 46, 47).

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lightpreview
{

struct Vertex
{
	float xyz[3];
	float st[2];        // diffuse texture coords
	float lm[2];        // lightmap coords (0..1 within the lightmap page)
	float normal[3];
	uint8_t color[4];   // vertex colour (vertex-lit surfaces), already as stored by the compiler
};

struct Surface
{
	int shader;          // index into Bsp::shaders
	int lightmap;        // lightmap page, or < 0 for vertex lit / no lightmap
	int firstIndex;      // into Bsp::indexes (triangles)
	int numIndexes;
	int type;            // 1 planar, 2 patch, 3 triangle soup, 4 flare
};

struct Shader
{
	std::string name;
	int surfaceFlags;
	int contentFlags;
};

struct LightmapPage
{
	int width = 0, height = 0;
	std::vector<uint8_t> rgb;  // raw bytes as written by the compiler (before the engine's overbright shift)
};

struct Bsp
{
	std::vector<Shader> shaders;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indexes; // triangle list, absolute vertex indexes
	std::vector<Surface> surfaces;
	std::vector<LightmapPage> lightmaps;
	std::string entities;
	float mins[3] = { 0, 0, 0 }, maxs[3] = { 0, 0, 0 };
};

/// Loads \p data (a whole .bsp file). On failure returns false and sets \p error.
/// If the compiler wrote external lightmaps (lm_0000.tga ...) instead of internal ones,
/// Bsp::lightmaps stays empty and the caller loads those images.
bool load_bsp( const std::vector<uint8_t>& data, Bsp& bsp, std::string& error, int patchSubdivisions = 8 );

} // namespace lightpreview
