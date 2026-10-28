/*
   Light preview: compiled BSP loader. See lightpreview_bsp.h.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "lightpreview_bsp.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lightpreview
{

namespace
{

class Reader
{
	const std::vector<uint8_t>& m_data;
public:
	Reader( const std::vector<uint8_t>& data ) : m_data( data ){
	}
	bool has( std::size_t offset, std::size_t size ) const {
		return offset <= m_data.size() && size <= m_data.size() - offset;
	}
	int32_t i32( std::size_t offset ) const {
		int32_t v;
		std::memcpy( &v, &m_data[offset], 4 ); // BSP files are little-endian, as are all supported editor platforms
		return v;
	}
	float f32( std::size_t offset ) const {
		float v;
		std::memcpy( &v, &m_data[offset], 4 );
		return v;
	}
	const uint8_t* ptr( std::size_t offset ) const {
		return &m_data[offset];
	}
};

struct Lump
{
	std::size_t offset, length;
};

// ---------------------------------------------------------------------------------------
// Quake 3 family "IBSP" v46 (Quake 3) / v47 (RTCW, ET): identical surface/vertex layout

enum
{
	Q3_LUMP_ENTITIES = 0,
	Q3_LUMP_SHADERS = 1,
	Q3_LUMP_MODELS = 7,
	Q3_LUMP_DRAWVERTS = 10,
	Q3_LUMP_DRAWINDEXES = 11,
	Q3_LUMP_SURFACES = 13,
	Q3_LUMP_LIGHTMAPS = 14,
	Q3_NUM_LUMPS = 17,
};

const std::size_t Q3_SHADER_SIZE = 72;    // char name[64]; int surfaceFlags; int contentFlags
const std::size_t Q3_DRAWVERT_SIZE = 44;  // xyz[3], st[2], lightmap[2], normal[3] floats; color[4] bytes
const std::size_t Q3_SURFACE_SIZE = 104;
const std::size_t Q3_MODEL_SIZE = 40;
const int Q3_LIGHTMAP_SIZE = 128;

Vertex read_q3_vertex( const Reader& r, std::size_t o ){
	Vertex v;
	for ( int i = 0; i < 3; ++i ) v.xyz[i] = r.f32( o + 4 * i );
	for ( int i = 0; i < 2; ++i ) v.st[i] = r.f32( o + 12 + 4 * i );
	for ( int i = 0; i < 2; ++i ) v.lm[i] = r.f32( o + 20 + 4 * i );
	for ( int i = 0; i < 3; ++i ) v.normal[i] = r.f32( o + 28 + 4 * i );
	std::memcpy( v.color, r.ptr( o + 40 ), 4 );
	return v;
}

Vertex lerp( const Vertex& a, const Vertex& b, float t ){
	Vertex v;
	for ( int i = 0; i < 3; ++i ) v.xyz[i] = a.xyz[i] + ( b.xyz[i] - a.xyz[i] ) * t;
	for ( int i = 0; i < 2; ++i ) v.st[i] = a.st[i] + ( b.st[i] - a.st[i] ) * t;
	for ( int i = 0; i < 2; ++i ) v.lm[i] = a.lm[i] + ( b.lm[i] - a.lm[i] ) * t;
	for ( int i = 0; i < 3; ++i ) v.normal[i] = a.normal[i] + ( b.normal[i] - a.normal[i] ) * t;
	for ( int i = 0; i < 4; ++i ) v.color[i] = uint8_t( std::lround( a.color[i] + ( b.color[i] - a.color[i] ) * t ) );
	return v;
}

/// quadratic Bezier through three control points
Vertex bezier( const Vertex& a, const Vertex& b, const Vertex& c, float t ){
	return lerp( lerp( a, b, t ), lerp( b, c, t ), t );
}

/// Tessellates a Q3 patch (grid of biquadratic Bezier control points) into triangles.
void tessellate_patch( const std::vector<Vertex>& control, int width, int height, int subdivisions,
                       std::vector<Vertex>& outVerts, std::vector<uint32_t>& outIndexes ){
	if ( width < 3 || height < 3 || ( width & 1 ) == 0 || ( height & 1 ) == 0 )
		return;
	const int n = subdivisions < 1 ? 1 : subdivisions;
	const int gw = ( width - 1 ) / 2 * n + 1;
	const int gh = ( height - 1 ) / 2 * n + 1;
	const uint32_t base = uint32_t( outVerts.size() );
	for ( int gy = 0; gy < gh; ++gy )
	{
		const int py = std::min( gy / n, ( height - 1 ) / 2 - 1 );
		const float ty = float( gy - py * n ) / n;
		for ( int gx = 0; gx < gw; ++gx )
		{
			const int px = std::min( gx / n, ( width - 1 ) / 2 - 1 );
			const float tx = float( gx - px * n ) / n;
			Vertex rows[3];
			for ( int r = 0; r < 3; ++r )
			{
				const Vertex* row = &control[std::size_t( py * 2 + r ) * width + px * 2];
				rows[r] = bezier( row[0], row[1], row[2], tx );
			}
			Vertex v = bezier( rows[0], rows[1], rows[2], ty );
			const float len = std::sqrt( v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] + v.normal[2] * v.normal[2] );
			if ( len > 0 )
				for ( float& c : v.normal ) c /= len;
			outVerts.push_back( v );
		}
	}
	for ( int gy = 0; gy + 1 < gh; ++gy )
		for ( int gx = 0; gx + 1 < gw; ++gx )
		{
			const uint32_t i0 = base + gy * gw + gx, i1 = i0 + 1, i2 = i0 + gw, i3 = i2 + 1;
			for ( uint32_t i : { i0, i2, i1, i1, i2, i3 } )
				outIndexes.push_back( i );
		}
}

bool load_q3( const Reader& r, std::size_t fileSize, Bsp& bsp, std::string& error, int patchSubdivisions ){
	if ( !r.has( 8, Q3_NUM_LUMPS * 8 ) ) {
		error = "truncated header";
		return false;
	}
	Lump lumps[Q3_NUM_LUMPS];
	for ( int i = 0; i < Q3_NUM_LUMPS; ++i )
	{
		lumps[i].offset = std::size_t( r.i32( 8 + i * 8 ) );
		lumps[i].length = std::size_t( r.i32( 8 + i * 8 + 4 ) );
		if ( !r.has( lumps[i].offset, lumps[i].length ) ) {
			error = "lump " + std::to_string( i ) + " is outside the file";
			return false;
		}
	}
	( void )fileSize;

	{
		const Lump& l = lumps[Q3_LUMP_ENTITIES];
		bsp.entities.assign( reinterpret_cast<const char*>( r.ptr( l.offset ) ), l.length );
		const std::size_t nul = bsp.entities.find( '\0' );
		if ( nul != std::string::npos )
			bsp.entities.resize( nul );
	}
	{
		const Lump& l = lumps[Q3_LUMP_SHADERS];
		for ( std::size_t o = l.offset; o + Q3_SHADER_SIZE <= l.offset + l.length; o += Q3_SHADER_SIZE )
		{
			Shader s;
			const char* name = reinterpret_cast<const char*>( r.ptr( o ) );
			std::size_t len = 0;
			while ( len < 64 && name[len] != '\0' )
				++len;
			s.name.assign( name, len );
			s.surfaceFlags = r.i32( o + 64 );
			s.contentFlags = r.i32( o + 68 );
			bsp.shaders.push_back( s );
		}
	}
	std::vector<Vertex> drawVerts;
	{
		const Lump& l = lumps[Q3_LUMP_DRAWVERTS];
		drawVerts.reserve( l.length / Q3_DRAWVERT_SIZE );
		for ( std::size_t o = l.offset; o + Q3_DRAWVERT_SIZE <= l.offset + l.length; o += Q3_DRAWVERT_SIZE )
			drawVerts.push_back( read_q3_vertex( r, o ) );
	}
	std::vector<int32_t> drawIndexes;
	{
		const Lump& l = lumps[Q3_LUMP_DRAWINDEXES];
		for ( std::size_t o = l.offset; o + 4 <= l.offset + l.length; o += 4 )
			drawIndexes.push_back( r.i32( o ) );
	}
	{
		const Lump& l = lumps[Q3_LUMP_MODELS];
		if ( l.length >= Q3_MODEL_SIZE )
			for ( int i = 0; i < 3; ++i ) {
				bsp.mins[i] = r.f32( l.offset + 4 * i );
				bsp.maxs[i] = r.f32( l.offset + 12 + 4 * i );
			}
	}
	{
		const Lump& l = lumps[Q3_LUMP_LIGHTMAPS];
		const std::size_t page = std::size_t( Q3_LIGHTMAP_SIZE ) * Q3_LIGHTMAP_SIZE * 3;
		for ( std::size_t o = l.offset; o + page <= l.offset + l.length; o += page )
		{
			LightmapPage lm;
			lm.width = lm.height = Q3_LIGHTMAP_SIZE;
			lm.rgb.assign( r.ptr( o ), r.ptr( o ) + page );
			bsp.lightmaps.push_back( std::move( lm ) );
		}
	}

	bsp.vertices.reserve( drawVerts.size() );
	const Lump& l = lumps[Q3_LUMP_SURFACES];
	for ( std::size_t o = l.offset; o + Q3_SURFACE_SIZE <= l.offset + l.length; o += Q3_SURFACE_SIZE )
	{
		const int shaderNum = r.i32( o + 0 );
		const int surfaceType = r.i32( o + 8 );
		const int firstVert = r.i32( o + 12 );
		const int numVerts = r.i32( o + 16 );
		const int firstIndex = r.i32( o + 20 );
		const int numIndexes = r.i32( o + 24 );
		const int lightmapNum = r.i32( o + 28 );
		const int patchWidth = r.i32( o + 96 );
		const int patchHeight = r.i32( o + 100 );

		if ( firstVert < 0 || numVerts < 0 || std::size_t( firstVert ) + numVerts > drawVerts.size() )
			continue;
		if ( shaderNum < 0 || shaderNum >= int( bsp.shaders.size() ) )
			continue;

		Surface s;
		s.shader = shaderNum;
		s.lightmap = lightmapNum;
		s.type = surfaceType;
		s.firstIndex = int( bsp.indexes.size() );

		if ( surfaceType == 2 ) { // patch
			std::vector<Vertex> control( drawVerts.begin() + firstVert, drawVerts.begin() + firstVert + numVerts );
			if ( patchWidth * patchHeight != numVerts )
				continue;
			tessellate_patch( control, patchWidth, patchHeight, patchSubdivisions, bsp.vertices, bsp.indexes );
		}
		else if ( surfaceType == 1 || surfaceType == 3 ) { // planar, triangle soup
			if ( firstIndex < 0 || numIndexes < 0 || std::size_t( firstIndex ) + numIndexes > drawIndexes.size() )
				continue;
			const uint32_t base = uint32_t( bsp.vertices.size() );
			bsp.vertices.insert( bsp.vertices.end(), drawVerts.begin() + firstVert, drawVerts.begin() + firstVert + numVerts );
			for ( int i = 0; i < numIndexes; ++i )
			{
				const int32_t idx = drawIndexes[firstIndex + i];
				if ( idx < 0 || idx >= numVerts ) { // corrupt index, drop the rest of this surface
					bsp.indexes.resize( std::size_t( s.firstIndex ) );
					break;
				}
				bsp.indexes.push_back( base + uint32_t( idx ) );
			}
		}
		else{
			continue; // flares and unknown types aren't drawn
		}
		s.numIndexes = int( bsp.indexes.size() ) - s.firstIndex;
		if ( s.numIndexes > 0 )
			bsp.surfaces.push_back( s );
	}
	return true;
}

} // namespace

bool load_bsp( const std::vector<uint8_t>& data, Bsp& bsp, std::string& error, int patchSubdivisions ){
	bsp = Bsp();
	const Reader r( data );
	if ( !r.has( 0, 8 ) ) {
		error = "file too small";
		return false;
	}
	const std::string ident( reinterpret_cast<const char*>( r.ptr( 0 ) ), 4 );
	const int version = r.i32( 4 );
	if ( ident == "IBSP" && ( version == 46 || version == 47 ) )
		return load_q3( r, data.size(), bsp, error, patchSubdivisions );
	error = "unsupported BSP format " + ident + " version " + std::to_string( version );
	return false;
}

} // namespace lightpreview
