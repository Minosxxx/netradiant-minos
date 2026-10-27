/*
   Hotspot texturing: rectangle definitions, parsing and shape matching.
   Header-only and free of editor dependencies so it can be unit tested.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace hotspot
{

enum class Mode { Fit, TileH, TileV };

struct Rect
{
	double x, y, w, h; // in pixels of the reference image size
	Mode mode;
};

struct Definition
{
	double width = 0, height = 0; // reference image size; 0 = use the shader's size
	double inset = 0;              // pixels to shrink every rect by, to avoid bleeding from neighbours
	std::vector<Rect> rects;
};

/*
   .hotspot file format, one statement per line, '#' or '//' starts a comment:

     size  <width> <height>          reference image size the rects are measured in (optional)
     inset <pixels>                  shrink rects on every side (optional, default 0)
     rect  <x> <y> <w> <h> [mode]    mode: fit (default) | tile_h | tile_v

   tile_h rects should span the full texture width (tile_v: full height),
   because they rely on the texture wrapping to repeat.
 */
inline bool parse( const char* text, Definition& def ){
	def = Definition();
	const char* p = text;
	while ( *p != '\0' )
	{
		const char* eol = p;
		while ( *eol != '\0' && *eol != '\n' && *eol != '\r' )
			++eol;
		// copy line, strip comments
		char line[512];
		std::size_t len = std::size_t( eol - p );
		if ( len >= sizeof( line ) )
			len = sizeof( line ) - 1;
		std::memcpy( line, p, len );
		line[len] = '\0';
		for ( char* c = line; *c != '\0'; ++c ){
			if ( *c == '#' || ( c[0] == '/' && c[1] == '/' ) ) {
				*c = '\0';
				break;
			}
		}
		// tokenize
		char* tok[8];
		int n = 0;
		for ( char* c = line; *c != '\0' && n < 8; )
		{
			while ( *c == ' ' || *c == '\t' )
				++c;
			if ( *c == '\0' )
				break;
			tok[n++] = c;
			while ( *c != '\0' && *c != ' ' && *c != '\t' )
				++c;
			if ( *c != '\0' )
				*c++ = '\0';
		}
		if ( n > 0 ) {
			if ( !std::strcmp( tok[0], "size" ) && n >= 3 ) {
				def.width = std::strtod( tok[1], nullptr );
				def.height = std::strtod( tok[2], nullptr );
			}
			else if ( !std::strcmp( tok[0], "inset" ) && n >= 2 ) {
				def.inset = std::strtod( tok[1], nullptr );
			}
			else if ( !std::strcmp( tok[0], "rect" ) && n >= 5 ) {
				Rect r;
				r.x = std::strtod( tok[1], nullptr );
				r.y = std::strtod( tok[2], nullptr );
				r.w = std::strtod( tok[3], nullptr );
				r.h = std::strtod( tok[4], nullptr );
				r.mode = Mode::Fit;
				if ( n >= 6 ) {
					if ( !std::strcmp( tok[5], "tile_h" ) )
						r.mode = Mode::TileH;
					else if ( !std::strcmp( tok[5], "tile_v" ) )
						r.mode = Mode::TileV;
				}
				if ( r.w > 0 && r.h > 0 )
					def.rects.push_back( r );
			}
		}
		p = eol;
		while ( *p == '\n' || *p == '\r' )
			++p;
	}
	return !def.rects.empty();
}

/// How badly a face of size faceW x faceH (world units) suits a rect. 0 = perfect.
/// Fit rects compare aspect ratios on a log scale (2x too wide == 2x too tall).
/// A tile_h rect is perfect for any face at least as wide (relative to height) as the rect itself,
/// and gets worse the narrower the face is; tile_v likewise vertically.
inline double score( const Rect& r, double faceW, double faceH ){
	const double faceAspect = std::log( faceW / faceH );
	const double rectAspect = std::log( r.w / r.h );
	switch ( r.mode )
	{
	case Mode::TileH:
		return faceAspect >= rectAspect ? 0.0 : rectAspect - faceAspect;
	case Mode::TileV:
		return faceAspect <= rectAspect ? 0.0 : faceAspect - rectAspect;
	default:
		return std::fabs( faceAspect - rectAspect );
	}
}

/// Picks the best rect for a face. Rects that are (near) equally good are chosen between
/// using \p variation, so repeated identical faces don't all look the same.
/// Returns -1 if there are no rects.
inline int pick( const Definition& def, double faceW, double faceH, unsigned int variation ){
	if ( def.rects.empty() || !( faceW > 0 ) || !( faceH > 0 ) )
		return -1;
	double best = 1e30;
	for ( const Rect& r : def.rects )
		best = std::fmin( best, score( r, faceW, faceH ) );
	const double tolerance = 0.02;
	int candidates[64];
	int count = 0;
	for ( int i = 0; i < int( def.rects.size() ) && count < 64; ++i )
		if ( score( def.rects[i], faceW, faceH ) <= best + tolerance )
			candidates[count++] = i;
	return candidates[variation % unsigned( count )];
}

/// Texture coordinates (normalised, 0..1 = one texture repeat) for a face, given
/// the face extents along its horizontal (u) and vertical (v) axes, in world units.
/// Outputs s,t for three reference points: (uMin, vMax), (uMax, vMax), (uMin, vMin),
/// i.e. top-left, top-right, bottom-left of the face as seen from the front.
struct Mapping
{
	double st[3][2];
};

inline Mapping map( const Definition& def, const Rect& rect, double imageW, double imageH,
                    double uMin, double uMax, double vMin, double vMax ){
	const double W = def.width > 0 ? def.width : imageW;
	const double H = def.height > 0 ? def.height : imageH;
	double x = rect.x + def.inset, y = rect.y + def.inset;
	double w = rect.w - 2 * def.inset, h = rect.h - 2 * def.inset;
	if ( w <= 0 || h <= 0 ) { // inset too big, ignore it
		x = rect.x; y = rect.y; w = rect.w; h = rect.h;
	}
	const double faceW = uMax - uMin, faceH = vMax - vMin;

	// pixel coordinates of the three reference points
	double s0, s1, t0, t2;
	switch ( rect.mode )
	{
	case Mode::TileH: {
		// fit height, keep pixels square, anchor horizontally in world space so neighbouring faces line up
		const double k = h / faceH;           // pixels per world unit
		s0 = x + uMin * k;
		s1 = x + uMax * k;
		t0 = y;
		t2 = y + h;
		break;
	}
	case Mode::TileV: {
		const double k = w / faceW;
		s0 = x;
		s1 = x + w;
		t0 = y - vMax * k;                     // t grows downward while v grows upward
		t2 = y - vMin * k;
		break;
	}
	default:
		s0 = x;
		s1 = x + w;
		t0 = y;
		t2 = y + h;
		break;
	}

	Mapping m;
	m.st[0][0] = s0 / W; m.st[0][1] = t0 / H; // top-left
	m.st[1][0] = s1 / W; m.st[1][1] = t0 / H; // top-right
	m.st[2][0] = s0 / W; m.st[2][1] = t2 / H; // bottom-left
	return m;
}

} // namespace hotspot
