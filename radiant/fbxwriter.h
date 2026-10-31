/*
   Minimal binary FBX 7.4 writer: static meshes with UVs, normals, per-polygon materials and diffuse
   textures. Enough for Maya, Blender, 3ds Max, Unity and Unreal to import the geometry.

   No editor dependencies, so it can be tested standalone.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <cstring>
#include <string>
#include <vector>

namespace fbx
{

struct Material
{
	std::string name;
	std::string texture;   // absolute path of the image, or empty
};

/// \p path relative to the folder \p dir (both absolute, '/' separators); \p path itself if there is
/// no relative route (e.g. another Windows drive).
inline std::string relative_path( const std::string& path, std::string dir ){
	if ( !dir.empty() && dir.back() != '/' )
		dir += '/';
	const auto lower = []( std::string s ){
		for ( char& c : s ) c = char( c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c );
		return s;
	};
	const bool windows = path.size() > 1 && path[1] == ':';
	const std::string a = windows ? lower( path ) : path, b = windows ? lower( dir ) : dir;
	if ( windows && ( b.size() < 2 || a[0] != b[0] ) )
		return path;
	size_t common = 0;   // length of the shared folder prefix
	for ( size_t i = 0; i < a.size() && i < b.size() && a[i] == b[i]; ++i )
		if ( a[i] == '/' )
			common = i + 1;
	if ( common == 0 )
		return path;
	std::string up;
	for ( size_t i = common; i < b.size(); ++i )
		if ( b[i] == '/' )
			up += "../";
	return up + path.substr( common );
}

struct Mesh
{
	std::string name;
	double translation[3] = { 0, 0, 0 };   // the object's pivot; positions are relative to it
	std::vector<double> positions;          // xyz
	std::vector<int32_t> polygonVertexIndex; // FBX style: the last index of each polygon is -(i + 1)
	std::vector<double> normals;            // xyz per polygon vertex
	std::vector<double> uvs;                // uv pairs
	std::vector<int32_t> uvIndex;           // per polygon vertex
	std::vector<int32_t> polygonMaterial;   // per polygon, index into `materials`
	std::vector<int> materials;             // scene material indices used by this mesh
};

struct Axes
{
	int up = 1, upSign = 1;          // 0 x, 1 y, 2 z
	int front = 2, frontSign = 1;
	int coord = 0, coordSign = 1;
	double unitScale = 1.0;          // centimetres per unit (1 = cm, 100 = m)
};

struct Scene
{
	std::vector<Mesh> meshes;
	std::vector<Material> materials;
	Axes axes;
	std::string creator = "NetRadiant";
	std::string folder;              // where the .fbx goes, for relative texture paths
};

namespace detail
{

class Node
{
public:
	std::string name;
	std::vector<uint8_t> props;
	uint32_t propCount = 0;
	std::vector<Node> children;

	explicit Node( std::string n ) : name( std::move( n ) ){
	}

	Node& add( const std::string& n ){
		children.emplace_back( n );
		return children.back();
	}

	template<typename T> void raw( const T& v ){
		const uint8_t* p = reinterpret_cast<const uint8_t*>( &v );
		props.insert( props.end(), p, p + sizeof( T ) );
	}
	Node& i16( int16_t v ){ props.push_back( 'Y' ); raw( v ); ++propCount; return *this; }
	Node& b( bool v ){ props.push_back( 'C' ); props.push_back( v ? 1 : 0 ); ++propCount; return *this; }
	Node& i32( int32_t v ){ props.push_back( 'I' ); raw( v ); ++propCount; return *this; }
	Node& i64( int64_t v ){ props.push_back( 'L' ); raw( v ); ++propCount; return *this; }
	Node& f64( double v ){ props.push_back( 'D' ); raw( v ); ++propCount; return *this; }
	Node& str( const std::string& s ){
		props.push_back( 'S' );
		raw( uint32_t( s.size() ) );
		props.insert( props.end(), s.begin(), s.end() );
		++propCount;
		return *this;
	}
	Node& bytes( const std::vector<uint8_t>& s ){
		props.push_back( 'R' );
		raw( uint32_t( s.size() ) );
		props.insert( props.end(), s.begin(), s.end() );
		++propCount;
		return *this;
	}
	Node& f64s( const std::vector<double>& a ){
		props.push_back( 'd' );
		raw( uint32_t( a.size() ) ); raw( uint32_t( 0 ) ); raw( uint32_t( a.size() * 8 ) );
		const uint8_t* p = reinterpret_cast<const uint8_t*>( a.data() );
		props.insert( props.end(), p, p + a.size() * 8 );
		++propCount;
		return *this;
	}
	Node& i32s( const std::vector<int32_t>& a ){
		props.push_back( 'i' );
		raw( uint32_t( a.size() ) ); raw( uint32_t( 0 ) ); raw( uint32_t( a.size() * 4 ) );
		const uint8_t* p = reinterpret_cast<const uint8_t*>( a.data() );
		props.insert( props.end(), p, p + a.size() * 4 );
		++propCount;
		return *this;
	}

	void write( std::vector<uint8_t>& out ) const {
		const size_t start = out.size();
		const auto put32 = [&]( uint32_t v ){ const uint8_t* p = reinterpret_cast<const uint8_t*>( &v ); out.insert( out.end(), p, p + 4 ); };
		put32( 0 );                          // end offset, patched below
		put32( propCount );
		put32( uint32_t( props.size() ) );
		out.push_back( uint8_t( name.size() ) );
		out.insert( out.end(), name.begin(), name.end() );
		out.insert( out.end(), props.begin(), props.end() );
		if ( !children.empty() ) {
			for ( const Node& c : children )
				c.write( out );
			out.insert( out.end(), 13, 0 );   // null record ends the nested list
		}
		const uint32_t end = uint32_t( out.size() );
		std::memcpy( out.data() + start, &end, 4 );
	}
};

/// "P" entry of a Properties70 block
inline Node& P( Node& props, const char* name, const char* type, const char* label, const char* flags ){
	return props.add( "P" ).str( name ).str( type ).str( label ).str( flags );
}

/// "name\x00\x01Class" object names
inline std::string object_name( const std::string& name, const char* cls ){
	std::string s = name;
	s.push_back( '\0' );
	s.push_back( '\x01' );
	return s + cls;
}

inline std::string file_name_only( const std::string& path ){
	const size_t slash = path.find_last_of( "/\\" );
	return slash == std::string::npos ? path : path.substr( slash + 1 );
}

} // namespace detail

/// The binary file contents.
inline std::vector<uint8_t> write_binary( const Scene& scene ){
	using detail::Node;
	using detail::P;
	using detail::object_name;
	std::vector<Node> top;

	{ // FBXHeaderExtension
		Node h( "FBXHeaderExtension" );
		h.add( "FBXHeaderVersion" ).i32( 1003 );
		h.add( "FBXVersion" ).i32( 7400 );
		h.add( "EncryptionType" ).i32( 0 );
		Node& t = h.add( "CreationTimeStamp" );
		t.add( "Version" ).i32( 1000 );
		t.add( "Year" ).i32( 1970 ); t.add( "Month" ).i32( 1 ); t.add( "Day" ).i32( 1 );
		t.add( "Hour" ).i32( 10 ); t.add( "Minute" ).i32( 0 ); t.add( "Second" ).i32( 0 ); t.add( "Millisecond" ).i32( 0 );
		h.add( "Creator" ).str( scene.creator );
		top.push_back( h );
	}
	// fixed id and time, as other writers use; the footer below matches them
	top.emplace_back( "FileId" );
	top.back().bytes( { 0x28, 0xb3, 0x2a, 0xeb, 0xb6, 0x24, 0xcc, 0xc2, 0xbf, 0xc8, 0xb0, 0x2a, 0xa9, 0x2b, 0xfc, 0xf1 } );
	top.emplace_back( "CreationTime" );
	top.back().str( "1970-01-01 10:00:00:000" );
	top.emplace_back( "Creator" );
	top.back().str( scene.creator );

	{ // GlobalSettings
		Node g( "GlobalSettings" );
		g.add( "Version" ).i32( 1000 );
		Node& p = g.add( "Properties70" );
		const Axes& a = scene.axes;
		P( p, "UpAxis", "int", "Integer", "" ).i32( a.up );
		P( p, "UpAxisSign", "int", "Integer", "" ).i32( a.upSign );
		P( p, "FrontAxis", "int", "Integer", "" ).i32( a.front );
		P( p, "FrontAxisSign", "int", "Integer", "" ).i32( a.frontSign );
		P( p, "CoordAxis", "int", "Integer", "" ).i32( a.coord );
		P( p, "CoordAxisSign", "int", "Integer", "" ).i32( a.coordSign );
		P( p, "OriginalUpAxis", "int", "Integer", "" ).i32( a.up );
		P( p, "OriginalUpAxisSign", "int", "Integer", "" ).i32( a.upSign );
		P( p, "UnitScaleFactor", "double", "Number", "" ).f64( a.unitScale );
		P( p, "OriginalUnitScaleFactor", "double", "Number", "" ).f64( a.unitScale );
		P( p, "AmbientColor", "ColorRGB", "Color", "" ).f64( 0 ).f64( 0 ).f64( 0 );
		P( p, "DefaultCamera", "KString", "", "" ).str( "Producer Perspective" );
		P( p, "TimeMode", "enum", "", "" ).i32( 11 );
		P( p, "TimeSpanStart", "KTime", "Time", "" ).i64( 0 );
		P( p, "TimeSpanStop", "KTime", "Time", "" ).i64( 46186158000 );
		P( p, "CustomFrameRate", "double", "Number", "" ).f64( 24 );
		top.push_back( g );
	}

	top.emplace_back( "Documents" );
	top.back().add( "Count" ).i32( 0 );
	top.emplace_back( "References" );

	{ // Definitions
		Node d( "Definitions" );
		d.add( "Version" ).i32( 100 );
		int texturedMaterials = 0;
		for ( const Material& m : scene.materials )
			texturedMaterials += m.texture.empty() ? 0 : 1;
		const int count = 1 + 2 * int( scene.meshes.size() ) + int( scene.materials.size() ) + 2 * texturedMaterials;
		d.add( "Count" ).i32( count );
		const auto type = [&]( const char* name, int n ){
			Node& o = d.add( "ObjectType" ).str( name );
			o.add( "Count" ).i32( n );
		};
		type( "GlobalSettings", 1 );
		type( "Model", int( scene.meshes.size() ) );
		type( "Geometry", int( scene.meshes.size() ) );
		type( "Material", int( scene.materials.size() ) );
		if ( texturedMaterials > 0 ) {
			type( "Texture", texturedMaterials );
			type( "Video", texturedMaterials );
		}
		top.push_back( d );
	}

	// ids: deterministic, unique, non-zero
	int64_t nextId = 1000000;
	std::vector<int64_t> materialIds, textureIds, videoIds;
	Node objects( "Objects" );
	Node connections( "Connections" );
	const auto connect = [&]( int64_t child, int64_t parent ){
		connections.add( "C" ).str( "OO" ).i64( child ).i64( parent );
	};

	for ( const Material& m : scene.materials )
	{
		const int64_t id = nextId++;
		materialIds.push_back( id );
		Node& mat = objects.add( "Material" ).i64( id ).str( object_name( m.name, "Material" ) ).str( "" );
		mat.add( "Version" ).i32( 102 );
		mat.add( "ShadingModel" ).str( "lambert" );
		mat.add( "MultiLayer" ).i32( 0 );
		Node& p = mat.add( "Properties70" );
		P( p, "DiffuseColor", "Color", "", "A" ).f64( 0.8 ).f64( 0.8 ).f64( 0.8 );
		P( p, "DiffuseFactor", "Number", "", "A" ).f64( 1.0 );
		P( p, "Diffuse", "Vector3D", "Vector", "" ).f64( 0.8 ).f64( 0.8 ).f64( 0.8 );
		if ( m.texture.empty() ) {
			textureIds.push_back( 0 );
			videoIds.push_back( 0 );
			continue;
		}
		const int64_t tid = nextId++, vid = nextId++;
		textureIds.push_back( tid );
		videoIds.push_back( vid );
		const std::string file = detail::file_name_only( m.texture );
		Node& video = objects.add( "Video" ).i64( vid ).str( object_name( m.name, "Video" ) ).str( "Clip" );
		video.add( "Type" ).str( "Clip" );
		P( video.add( "Properties70" ), "Path", "KString", "XRefUrl", "" ).str( m.texture );
		video.add( "UseMipMap" ).i32( 0 );
		video.add( "Filename" ).str( m.texture );
		const std::string relative = relative_path( m.texture, scene.folder );
		video.add( "RelativeFilename" ).str( relative );
		Node& tex = objects.add( "Texture" ).i64( tid ).str( object_name( m.name, "Texture" ) ).str( "" );
		tex.add( "Type" ).str( "TextureVideoClip" );
		tex.add( "Version" ).i32( 202 );
		tex.add( "TextureName" ).str( object_name( m.name, "Texture" ) );
		Node& tp = tex.add( "Properties70" );
		P( tp, "UseMaterial", "bool", "", "" ).i32( 1 );
		tex.add( "Media" ).str( object_name( m.name, "Video" ) );
		tex.add( "FileName" ).str( m.texture );
		tex.add( "RelativeFilename" ).str( relative );
		tex.add( "ModelUVTranslation" ).f64( 0 ).f64( 0 );
		tex.add( "ModelUVScaling" ).f64( 1 ).f64( 1 );
		tex.add( "Texture_Alpha_Source" ).str( "None" );
		tex.add( "Cropping" ).i32( 0 ).i32( 0 ).i32( 0 ).i32( 0 );
		(void)file;
	}

	for ( const Mesh& mesh : scene.meshes )
	{
		const int64_t gid = nextId++, mid = nextId++;
		Node& geo = objects.add( "Geometry" ).i64( gid ).str( object_name( mesh.name, "Geometry" ) ).str( "Mesh" );
		geo.add( "Vertices" ).f64s( mesh.positions );
		geo.add( "PolygonVertexIndex" ).i32s( mesh.polygonVertexIndex );
		geo.add( "GeometryVersion" ).i32( 124 );
		{
			Node& n = geo.add( "LayerElementNormal" ).i32( 0 );
			n.add( "Version" ).i32( 101 );
			n.add( "Name" ).str( "" );
			n.add( "MappingInformationType" ).str( "ByPolygonVertex" );
			n.add( "ReferenceInformationType" ).str( "Direct" );
			n.add( "Normals" ).f64s( mesh.normals );
		}
		{
			Node& u = geo.add( "LayerElementUV" ).i32( 0 );
			u.add( "Version" ).i32( 101 );
			u.add( "Name" ).str( "map1" );
			u.add( "MappingInformationType" ).str( "ByPolygonVertex" );
			u.add( "ReferenceInformationType" ).str( "IndexToDirect" );
			u.add( "UV" ).f64s( mesh.uvs );
			u.add( "UVIndex" ).i32s( mesh.uvIndex );
		}
		{
			Node& m = geo.add( "LayerElementMaterial" ).i32( 0 );
			m.add( "Version" ).i32( 101 );
			m.add( "Name" ).str( "" );
			m.add( "MappingInformationType" ).str( "ByPolygon" );
			m.add( "ReferenceInformationType" ).str( "IndexToDirect" );
			m.add( "Materials" ).i32s( mesh.polygonMaterial );
		}
		{
			Node& l = geo.add( "Layer" ).i32( 0 );
			l.add( "Version" ).i32( 100 );
			for ( const char* type : { "LayerElementNormal", "LayerElementUV", "LayerElementMaterial" } )
			{
				Node& e = l.add( "LayerElement" );
				e.add( "Type" ).str( type );
				e.add( "TypedIndex" ).i32( 0 );
			}
		}

		Node& model = objects.add( "Model" ).i64( mid ).str( object_name( mesh.name, "Model" ) ).str( "Mesh" );
		model.add( "Version" ).i32( 232 );
		Node& mp = model.add( "Properties70" );
		P( mp, "Lcl Translation", "Lcl Translation", "", "A" ).f64( mesh.translation[0] ).f64( mesh.translation[1] ).f64( mesh.translation[2] );
		P( mp, "DefaultAttributeIndex", "int", "Integer", "" ).i32( 0 );
		model.add( "Shading" ).b( true );
		model.add( "Culling" ).str( "CullingOff" );

		connect( mid, 0 );
		connect( gid, mid );
		for ( int m : mesh.materials )
			connect( materialIds[m], mid );
	}
	for ( size_t m = 0; m < scene.materials.size(); ++m )
	{
		if ( textureIds[m] == 0 )
			continue;
		connections.add( "C" ).str( "OP" ).i64( textureIds[m] ).i64( materialIds[m] ).str( "DiffuseColor" );
		connect( videoIds[m], textureIds[m] );
	}
	top.push_back( objects );
	top.push_back( connections );
	top.emplace_back( "Takes" );
	top.back().add( "Current" ).str( "" );

	std::vector<uint8_t> out;
	const char magic[] = "Kaydara FBX Binary  ";
	out.insert( out.end(), magic, magic + 20 );
	out.push_back( 0x00 ); out.push_back( 0x1a ); out.push_back( 0x00 );
	const uint32_t version = 7400;
	out.insert( out.end(), reinterpret_cast<const uint8_t*>( &version ), reinterpret_cast<const uint8_t*>( &version ) + 4 );
	for ( const Node& n : top )
		n.write( out );
	out.insert( out.end(), 13, 0 );   // end of the top level

	// footer
	const uint8_t footId[16] = { 0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66, 0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e };
	out.insert( out.end(), footId, footId + 16 );
	out.insert( out.end(), 4, 0 );
	size_t pad = ( ( out.size() + 15 ) & ~size_t( 15 ) ) - out.size();
	if ( pad == 0 )
		pad = 16;
	out.insert( out.end(), pad, 0 );
	out.insert( out.end(), reinterpret_cast<const uint8_t*>( &version ), reinterpret_cast<const uint8_t*>( &version ) + 4 );
	out.insert( out.end(), 120, 0 );
	const uint8_t footMagic[16] = { 0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e, 0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b };
	out.insert( out.end(), footMagic, footMagic + 16 );
	return out;
}

/// Builds meshes from polygons. Positions are welded per mesh; UVs per mesh.
class MeshBuilder
{
	Mesh& m_mesh;
	std::vector<int>& m_sceneMaterialOfSlot;
	std::map<std::array<int64_t, 3>, int32_t> m_positionIndex;   // welds positions (1/1024 unit)
	std::map<std::array<int64_t, 2>, int32_t> m_uvIndex;
public:
	struct Corner
	{
		double xyz[3];
		double normal[3];
		double uv[2];
	};

	MeshBuilder( Mesh& mesh ) : m_mesh( mesh ), m_sceneMaterialOfSlot( mesh.materials ){
	}

	int slotFor( int sceneMaterial ){
		for ( size_t i = 0; i < m_sceneMaterialOfSlot.size(); ++i )
			if ( m_sceneMaterialOfSlot[i] == sceneMaterial )
				return int( i );
		m_sceneMaterialOfSlot.push_back( sceneMaterial );
		return int( m_sceneMaterialOfSlot.size() - 1 );
	}

	/// \p corners in counter-clockwise order seen from the front (right-handed).
	void addPolygon( const std::vector<Corner>& corners, int sceneMaterial ){
		if ( corners.size() < 3 )
			return;
		const auto key = []( double v, double scale ){ return int64_t( std::llround( v * scale ) ); };
		for ( size_t i = 0; i < corners.size(); ++i )
		{
			const Corner& c = corners[i];
			const std::array<int64_t, 3> pk = { key( c.xyz[0], 1024 ), key( c.xyz[1], 1024 ), key( c.xyz[2], 1024 ) };
			auto found = m_positionIndex.find( pk );
			if ( found == m_positionIndex.end() ) {
				found = m_positionIndex.emplace( pk, int32_t( m_mesh.positions.size() / 3 ) ).first;
				m_mesh.positions.insert( m_mesh.positions.end(), { c.xyz[0], c.xyz[1], c.xyz[2] } );
			}
			const int32_t v = found->second;
			m_mesh.polygonVertexIndex.push_back( i + 1 == corners.size() ? -( v + 1 ) : v );
			m_mesh.normals.insert( m_mesh.normals.end(), { c.normal[0], c.normal[1], c.normal[2] } );
			const std::array<int64_t, 2> uk = { key( c.uv[0], 65536 ), key( c.uv[1], 65536 ) };
			auto u = m_uvIndex.find( uk );
			if ( u == m_uvIndex.end() ) {
				u = m_uvIndex.emplace( uk, int32_t( m_mesh.uvs.size() / 2 ) ).first;
				m_mesh.uvs.insert( m_mesh.uvs.end(), { c.uv[0], c.uv[1] } );
			}
			m_mesh.uvIndex.push_back( u->second );
		}
		m_mesh.polygonMaterial.push_back( slotFor( sceneMaterial ) );
	}
};

} // namespace fbx
