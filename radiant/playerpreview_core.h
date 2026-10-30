/*
   Player preview models: builds one static .md3 of a Quake 3 player model (lower + upper + head)
   standing in its idle pose, assembled the way cgame does it (CG_PositionRotatedEntityOnTag),
   with the default skin's shaders applied. Used to draw spawn points as a player in the editor.

   No editor dependencies, so it can be tested standalone.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace playerpreview
{

/// Reads a file from the game's file system: returns false if it doesn't exist.
using ReadFile = std::function<bool( const std::string& path, std::vector<uint8_t>& data )>;

struct Vec3
{
	float x = 0, y = 0, z = 0;
};

struct Axis
{
	Vec3 v[3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
};

struct Placement
{
	Vec3 origin;
	Axis axis;
	Vec3 apply( const Vec3& p ) const { // origin + sum p[i] * axis[i]
		return { origin.x + p.x * axis.v[0].x + p.y * axis.v[1].x + p.z * axis.v[2].x,
		         origin.y + p.x * axis.v[0].y + p.y * axis.v[1].y + p.z * axis.v[2].y,
		         origin.z + p.x * axis.v[0].z + p.y * axis.v[1].z + p.z * axis.v[2].z };
	}
	Vec3 rotate( const Vec3& p ) const {
		return { p.x * axis.v[0].x + p.y * axis.v[1].x + p.z * axis.v[2].x,
		         p.x * axis.v[0].y + p.y * axis.v[1].y + p.z * axis.v[2].y,
		         p.x * axis.v[0].z + p.y * axis.v[1].z + p.z * axis.v[2].z };
	}
};

/// q_math MatrixMultiply: out[i][j] = sum_k a[i][k] * b[k][j]
inline Axis axis_multiply( const Axis& a, const Axis& b ){
	const auto get = []( const Vec3& v, int i ){ return i == 0 ? v.x : i == 1 ? v.y : v.z; };
	Axis out;
	for ( int i = 0; i < 3; ++i )
	{
		float r[3];
		for ( int j = 0; j < 3; ++j )
			r[j] = get( a.v[i], 0 ) * get( b.v[0], j ) + get( a.v[i], 1 ) * get( b.v[1], j ) + get( a.v[i], 2 ) * get( b.v[2], j );
		out.v[i] = { r[0], r[1], r[2] };
	}
	return out;
}

struct Md3Surface
{
	std::string name;
	std::string shader;
	std::vector<int32_t> triangles;          // 3 per triangle
	std::vector<float> st;                   // 2 per vertex
	std::vector<Vec3> xyz;                   // the chosen frame only
	std::vector<Vec3> normals;
};

struct Md3Tag
{
	std::string name;
	Placement placement;
};

struct Md3Model
{
	int numFrames = 0;
	std::vector<Md3Surface> surfaces;        // vertices of frame `frame`
	std::vector<Md3Tag> tags;                // tags of frame `frame`
};

namespace detail
{
inline int32_t i32( const std::vector<uint8_t>& d, size_t o ){
	int32_t v;
	std::memcpy( &v, d.data() + o, 4 );
	return v;
}
inline int16_t i16( const std::vector<uint8_t>& d, size_t o ){
	int16_t v;
	std::memcpy( &v, d.data() + o, 2 );
	return v;
}
inline float f32( const std::vector<uint8_t>& d, size_t o ){
	float v;
	std::memcpy( &v, d.data() + o, 4 );
	return v;
}
inline std::string str( const std::vector<uint8_t>& d, size_t o, size_t n ){
	size_t len = 0;
	while ( len < n && d[o + len] != 0 )
		++len;
	return std::string( reinterpret_cast<const char*>( d.data() + o ), len );
}
inline std::string lower( std::string s ){
	for ( char& c : s )
		c = char( std::tolower( static_cast<unsigned char>( c ) ) );
	return s;
}
constexpr double c_pi = 3.14159265358979323846;

inline Vec3 decode_normal( int16_t packed ){
	const double lat = ( ( packed >> 8 ) & 0xff ) * ( 2 * c_pi / 256.0 );
	const double lng = ( packed & 0xff ) * ( 2 * c_pi / 256.0 );
	return { float( std::cos( lat ) * std::sin( lng ) ), float( std::sin( lat ) * std::sin( lng ) ), float( std::cos( lng ) ) };
}
inline int16_t encode_normal( const Vec3& n ){
	if ( n.x == 0 && n.y == 0 )
		return n.z > 0 ? 0 : int16_t( 128 );
	int lat = int( std::lround( std::atan2( n.y, n.x ) * 256.0 / ( 2 * c_pi ) ) ) & 0xff;
	int lng = int( std::lround( std::acos( std::max( -1.f, std::min( 1.f, n.z ) ) ) * 256.0 / ( 2 * c_pi ) ) ) & 0xff;
	return int16_t( ( lat << 8 ) | lng );
}
} // namespace detail

/// Parses \p data, keeping vertices and tags of \p frame (clamped to the frames available).
inline bool parse_md3( const std::vector<uint8_t>& d, int frame, Md3Model& model, std::string& error ){
	using namespace detail;
	if ( d.size() < 108 || std::memcmp( d.data(), "IDP3", 4 ) != 0 ) {
		error = "not an MD3 file";
		return false;
	}
	model.numFrames = i32( d, 76 );
	const int numTags = i32( d, 80 ), numSurfaces = i32( d, 84 );
	const int ofsTags = i32( d, 96 ), ofsSurfaces = i32( d, 100 );
	if ( model.numFrames < 1 ) {
		error = "MD3 has no frames";
		return false;
	}
	frame = std::max( 0, std::min( frame, model.numFrames - 1 ) );
	const auto inside = [&]( size_t offset, size_t length ){ return offset + length <= d.size(); };

	if ( !inside( ofsTags + size_t( model.numFrames ) * numTags * 112, 0 ) ) {
		error = "MD3 tags out of range";
		return false;
	}
	for ( int t = 0; t < numTags; ++t )
	{
		const size_t o = ofsTags + ( size_t( frame ) * numTags + t ) * 112;
		Md3Tag tag;
		tag.name = str( d, o, 64 );
		tag.placement.origin = { f32( d, o + 64 ), f32( d, o + 68 ), f32( d, o + 72 ) };
		for ( int a = 0; a < 3; ++a )
			tag.placement.axis.v[a] = { f32( d, o + 76 + a * 12 ), f32( d, o + 80 + a * 12 ), f32( d, o + 84 + a * 12 ) };
		model.tags.push_back( tag );
	}

	size_t s = ofsSurfaces;
	for ( int i = 0; i < numSurfaces; ++i )
	{
		if ( !inside( s, 108 ) || std::memcmp( d.data() + s, "IDP3", 4 ) != 0 ) {
			error = "bad MD3 surface";
			return false;
		}
		Md3Surface surface;
		surface.name = str( d, s + 4, 64 );
		const int numShaders = i32( d, s + 76 ), numVerts = i32( d, s + 80 ), numTris = i32( d, s + 84 );
		const int ofsTris = i32( d, s + 88 ), ofsShaders = i32( d, s + 92 ), ofsSt = i32( d, s + 96 ), ofsXyz = i32( d, s + 100 ), ofsEnd = i32( d, s + 104 );
		const int surfFrames = i32( d, s + 72 );
		if ( !inside( s + ofsEnd, 0 ) || !inside( s + ofsXyz, size_t( surfFrames ) * numVerts * 8 )
		  || !inside( s + ofsTris, size_t( numTris ) * 12 ) || !inside( s + ofsSt, size_t( numVerts ) * 8 ) ) {
			error = "MD3 surface out of range";
			return false;
		}
		if ( numShaders > 0 && inside( s + ofsShaders, 68 ) )
			surface.shader = str( d, s + ofsShaders, 64 );
		for ( int t = 0; t < numTris * 3; ++t )
			surface.triangles.push_back( i32( d, s + ofsTris + t * 4 ) );
		for ( int v = 0; v < numVerts * 2; ++v )
			surface.st.push_back( f32( d, s + ofsSt + v * 4 ) );
		const int f = std::max( 0, std::min( frame, surfFrames - 1 ) );
		for ( int v = 0; v < numVerts; ++v )
		{
			const size_t o = s + ofsXyz + ( size_t( f ) * numVerts + v ) * 8;
			surface.xyz.push_back( { i16( d, o ) / 64.f, i16( d, o + 2 ) / 64.f, i16( d, o + 4 ) / 64.f } );
			surface.normals.push_back( decode_normal( i16( d, o + 6 ) ) );
		}
		model.surfaces.push_back( std::move( surface ) );
		s += ofsEnd;
	}
	return true;
}

/// Frames from animation.cfg, as cgame numbers them (legs frames are counted without the torso-only
/// animations). Returns false if the file has too few animations.
struct IdleFrames
{
	int legs = 0;
	int torso = 0;
};

inline bool parse_animation_cfg( const std::string& text, IdleFrames& frames ){
	enum { TORSO_GESTURE = 6, TORSO_STAND = 11, LEGS_WALKCR = 13, LEGS_IDLE = 22 };
	std::vector<int> numbers;
	size_t i = 0;
	while ( i < text.size() )
	{
		if ( text.compare( i, 2, "//" ) == 0 ) {
			while ( i < text.size() && text[i] != '\n' )
				++i;
			continue;
		}
		if ( std::isspace( static_cast<unsigned char>( text[i] ) ) ) {
			++i;
			continue;
		}
		size_t j = i;
		while ( j < text.size() && !std::isspace( static_cast<unsigned char>( text[j] ) ) )
			++j;
		const std::string token = text.substr( i, j - i );
		i = j;
		const bool number = std::isdigit( static_cast<unsigned char>( token[0] ) ) || ( token[0] == '-' && token.size() > 1 );
		if ( number )
			numbers.push_back( std::atoi( token.c_str() ) );
		else if ( !numbers.empty() )
			break; // text after the animation table
		else if ( token == "headoffset" ) { // keyword with three numbers
			for ( int k = 0; k < 3; ++k )
			{
				while ( i < text.size() && std::isspace( static_cast<unsigned char>( text[i] ) ) )
					++i;
				while ( i < text.size() && !std::isspace( static_cast<unsigned char>( text[i] ) ) )
					++i;
			}
		}
		else if ( token == "sex" || token == "footsteps" ) { // keyword with one value
			while ( i < text.size() && std::isspace( static_cast<unsigned char>( text[i] ) ) )
				++i;
			while ( i < text.size() && !std::isspace( static_cast<unsigned char>( text[i] ) ) )
				++i;
		}
	}
	if ( numbers.size() < size_t( ( LEGS_IDLE + 1 ) * 4 ) )
		return false;
	const auto first = [&]( int anim ){ return numbers[anim * 4]; };
	const int skip = first( LEGS_WALKCR ) - first( TORSO_GESTURE );
	frames.torso = first( TORSO_STAND );
	frames.legs = first( LEGS_IDLE ) - skip;
	return true;
}

/// "surface,path" lines of a .skin file -> surface name (lower case) -> shader name (no extension).
inline std::vector<std::pair<std::string, std::string>> parse_skin( const std::string& text ){
	std::vector<std::pair<std::string, std::string>> out;
	size_t start = 0;
	while ( start < text.size() )
	{
		size_t end = text.find_first_of( "\r\n", start );
		if ( end == std::string::npos )
			end = text.size();
		std::string line = text.substr( start, end - start );
		start = end + 1;
		const size_t comma = line.find( ',' );
		if ( comma == std::string::npos )
			continue;
		const auto trim = []( std::string s ){
			while ( !s.empty() && std::isspace( static_cast<unsigned char>( s.back() ) ) ) s.pop_back();
			while ( !s.empty() && std::isspace( static_cast<unsigned char>( s.front() ) ) ) s.erase( s.begin() );
			return s;
		};
		std::string surface = detail::lower( trim( line.substr( 0, comma ) ) );
		std::string path = trim( line.substr( comma + 1 ) );
		if ( surface.empty() || path.empty() || surface.compare( 0, 4, "tag_" ) == 0 )
			continue;
		std::replace( path.begin(), path.end(), '\\', '/' );
		const size_t dot = path.find_last_of( '.' );
		if ( dot != std::string::npos && path.find( '/', dot ) == std::string::npos )
			path.erase( dot );
		out.emplace_back( surface, path );
	}
	return out;
}

inline const Md3Tag* find_tag( const Md3Model& model, const char* name ){
	for ( const Md3Tag& tag : model.tags )
		if ( detail::lower( tag.name ) == name )
			return &tag;
	return nullptr;
}

/// Writes a one-frame MD3 of \p surfaces (already in model space).
inline std::vector<uint8_t> write_md3( const std::string& name, const std::vector<Md3Surface>& surfaces ){
	using namespace detail;
	std::vector<uint8_t> out;
	const auto put32 = [&]( int32_t v ){ const uint8_t* p = reinterpret_cast<const uint8_t*>( &v ); out.insert( out.end(), p, p + 4 ); };
	const auto putf = [&]( float v ){ const uint8_t* p = reinterpret_cast<const uint8_t*>( &v ); out.insert( out.end(), p, p + 4 ); };
	const auto put16 = [&]( int16_t v ){ const uint8_t* p = reinterpret_cast<const uint8_t*>( &v ); out.insert( out.end(), p, p + 2 ); };
	const auto putstr = [&]( const std::string& s, size_t n ){
		std::string t = s.substr( 0, n - 1 );
		out.insert( out.end(), t.begin(), t.end() );
		out.insert( out.end(), n - t.size(), 0 );
	};
	const auto set32 = [&]( size_t o, int32_t v ){ std::memcpy( out.data() + o, &v, 4 ); };

	Vec3 mins{ 1e9f, 1e9f, 1e9f }, maxs{ -1e9f, -1e9f, -1e9f };
	for ( const Md3Surface& s : surfaces )
		for ( const Vec3& p : s.xyz )
		{
			mins = { std::min( mins.x, p.x ), std::min( mins.y, p.y ), std::min( mins.z, p.z ) };
			maxs = { std::max( maxs.x, p.x ), std::max( maxs.y, p.y ), std::max( maxs.z, p.z ) };
		}
	if ( surfaces.empty() )
		mins = maxs = Vec3();

	// header
	out.insert( out.end(), { 'I', 'D', 'P', '3' } );
	put32( 15 );
	putstr( name, 64 );
	put32( 0 );                          // flags
	put32( 1 );                          // frames
	put32( 0 );                          // tags
	put32( int32_t( surfaces.size() ) );
	put32( 0 );                          // skins
	put32( 108 );                        // ofsFrames
	put32( 108 + 56 );                   // ofsTags
	put32( 108 + 56 );                   // ofsSurfaces
	put32( 0 );                          // ofsEnd, set below
	// frame
	putf( mins.x ); putf( mins.y ); putf( mins.z );
	putf( maxs.x ); putf( maxs.y ); putf( maxs.z );
	putf( 0 ); putf( 0 ); putf( 0 );
	float radius = 0;
	for ( const Vec3& c : { mins, maxs } )
		radius = std::max( radius, std::sqrt( c.x * c.x + c.y * c.y + c.z * c.z ) );
	putf( radius );
	putstr( "editor", 16 );

	for ( const Md3Surface& s : surfaces )
	{
		const size_t start = out.size();
		const int numVerts = int( s.xyz.size() ), numTris = int( s.triangles.size() / 3 );
		out.insert( out.end(), { 'I', 'D', 'P', '3' } );
		putstr( s.name, 64 );
		put32( 0 );                      // flags
		put32( 1 );                      // frames
		put32( 1 );                      // shaders
		put32( numVerts );
		put32( numTris );
		const int ofsTris = 108, ofsShaders = ofsTris + numTris * 12, ofsSt = ofsShaders + 68,
		          ofsXyz = ofsSt + numVerts * 8, ofsEnd = ofsXyz + numVerts * 8;
		put32( ofsTris ); put32( ofsShaders ); put32( ofsSt ); put32( ofsXyz ); put32( ofsEnd );
		for ( int32_t t : s.triangles )
			put32( t );
		putstr( s.shader, 64 );
		put32( 0 );
		for ( float f : s.st )
			putf( f );
		for ( int v = 0; v < numVerts; ++v )
		{
			const auto q = []( float c ){ return int16_t( std::max( -32768.0, std::min( 32767.0, std::floor( c * 64.0 + 0.5 ) ) ) ); };
			put16( q( s.xyz[v].x ) ); put16( q( s.xyz[v].y ) ); put16( q( s.xyz[v].z ) );
			put16( encode_normal( s.normals[v] ) );
		}
		(void)start;
	}
	set32( 104, int32_t( out.size() ) );
	return out;
}

/// Builds the standing player for \p folder ("models/players/sarge"). \p skin is "default" normally.
inline bool build_player_preview( const ReadFile& read, const std::string& folder, const std::string& skin,
                                  std::vector<uint8_t>& out, std::string& error ){
	IdleFrames frames;
	{
		std::vector<uint8_t> cfg;
		if ( read( folder + "/animation.cfg", cfg ) )
			parse_animation_cfg( std::string( cfg.begin(), cfg.end() ), frames );
	}

	Md3Model parts[3];
	const char* names[3] = { "lower", "upper", "head" };
	const int partFrames[3] = { frames.legs, frames.torso, 0 };
	for ( int p = 0; p < 3; ++p )
	{
		std::vector<uint8_t> data;
		const std::string path = folder + "/" + names[p] + ".md3";
		if ( !read( path, data ) ) {
			error = "missing " + path;
			return false;
		}
		if ( !parse_md3( data, partFrames[p], parts[p], error ) ) {
			error = path + ": " + error;
			return false;
		}
		std::vector<uint8_t> skinData;
		if ( read( folder + "/" + names[p] + "_" + skin + ".skin", skinData ) || read( folder + "/" + names[p] + "_default.skin", skinData ) ) {
			const auto remaps = parse_skin( std::string( skinData.begin(), skinData.end() ) );
			for ( Md3Surface& s : parts[p].surfaces )
				for ( const auto& [surface, shader] : remaps )
					if ( surface == detail::lower( s.name ) )
						s.shader = shader;
		}
	}

	// cgame: legs at the entity origin; torso on the legs' tag_torso; head on the torso's tag_head
	Placement legs;
	Placement torso = legs, head = legs;
	if ( const Md3Tag* tag = find_tag( parts[0], "tag_torso" ) ) {
		torso.origin = legs.apply( tag->placement.origin );
		torso.axis = axis_multiply( tag->placement.axis, legs.axis );
	}
	if ( const Md3Tag* tag = find_tag( parts[1], "tag_head" ) ) {
		head.origin = torso.apply( tag->placement.origin );
		head.axis = axis_multiply( tag->placement.axis, torso.axis );
	}
	const Placement placements[3] = { legs, torso, head };

	std::vector<Md3Surface> surfaces;
	for ( int p = 0; p < 3; ++p )
		for ( Md3Surface s : parts[p].surfaces )
		{
			for ( size_t v = 0; v < s.xyz.size(); ++v )
			{
				s.xyz[v] = placements[p].apply( s.xyz[v] );
				Vec3 n = placements[p].rotate( s.normals[v] );
				const float len = std::sqrt( n.x * n.x + n.y * n.y + n.z * n.z );
				if ( len > 0 )
					n = { n.x / len, n.y / len, n.z / len };
				s.normals[v] = n;
			}
			s.name = std::string( names[p] ) + "_" + s.name;
			surfaces.push_back( std::move( s ) );
		}
	out = write_md3( folder + "/editor_preview.md3", surfaces );
	return true;
}

/// "models/players/<name>/editor_preview.md3" (any case, either slash) -> "models/players/<name>", else "".
inline std::string preview_folder( const std::string& path ){
	std::string p = detail::lower( path );
	std::replace( p.begin(), p.end(), '\\', '/' );
	const std::string suffix = "/editor_preview.md3";
	if ( p.size() <= suffix.size() || p.compare( p.size() - suffix.size(), suffix.size(), suffix ) != 0 )
		return "";
	if ( p.compare( 0, 15, "models/players/" ) != 0 )
		return "";
	std::string folder = path.substr( 0, path.size() - suffix.size() );
	std::replace( folder.begin(), folder.end(), '\\', '/' );
	return folder;
}

} // namespace playerpreview
