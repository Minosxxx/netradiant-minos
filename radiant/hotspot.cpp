/*
   Hotspot texturing.

   Select faces (or whole brushes) and run "Hotspot Texture": each face gets the rectangle
   of its texture's hotspot sheet whose shape matches it best, fitted to the face.
   Rectangles marked tile_h / tile_v repeat along one axis instead of stretching.

   "Hotspot Texture (Grouped)" treats all selected faces that share a texture and face the
   same way as one surface: the rectangle is chosen for their combined extents and the
   texture runs continuously across them.

   Rectangles are read from "<texture>.hotspot" next to the texture, e.g.
   textures/base/concrete_hs.tga  ->  textures/base/concrete_hs.hotspot
   (see hotspot_core.h for the format, or use the Hotspot Editor).

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "hotspot.h"
#include "hotspot_core.h"

#include <map>
#include <string>
#include <vector>

#include "ifilesystem.h"
#include "iarchive.h"
#include "iselection.h"
#include "iundo.h"
#include "itextstream.h"
#include "iscenegraph.h"
#include "texturelib.h"

#include "brush.h"
#include "brush_primit.h"
#include "map.h"
#include "commands.h"

bool Hotspot_loadText( const std::string& path, std::string& text ){
	ArchiveTextFile* file = GlobalFileSystem().openTextFile( path.c_str() );
	if ( file == nullptr )
		return false;
	char buffer[4096];
	std::size_t n;
	while ( ( n = file->getInputStream().read( buffer, sizeof( buffer ) ) ) > 0 )
		text.append( buffer, n );
	file->release();
	return true;
}

std::string Hotspot_stripExtension( const char* name ){
	std::string s( name );
	const std::size_t slash = s.find_last_of( "/\\" );
	const std::size_t dot = s.find_last_of( '.' );
	if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) )
		s.erase( dot );
	return s;
}

namespace
{

struct CachedDefinition
{
	bool found = false;
	hotspot::Definition def;
};

/// Face axes: v = "up" on the face, u = "right" when looking at the face from the front.
bool face_axes( const DoubleVector3& n, DoubleVector3& u, DoubleVector3& v ){
	const DoubleVector3 worldUp = std::fabs( n.z() ) < 0.7071
	                              ? DoubleVector3( 0, 0, 1 )   // walls and slopes: up is +Z
	                              : DoubleVector3( 0, 1, 0 );  // floors and ceilings: up is +Y
	v = worldUp - n * vector3_dot( worldUp, n );
	if ( vector3_length( v ) < 1e-6 )
		return false;
	v = vector3_normalised( v );
	u = vector3_normalised( vector3_cross( v, n ) );
	return true;
}

class HotspotApplier
{
	std::map<std::string, CachedDefinition> m_cache;
public:
	std::size_t m_done = 0;
	std::size_t m_groups = 0;
	std::size_t m_noDefinition = 0;
	std::size_t m_degenerate = 0;
	std::map<std::string, bool> m_missing; // shaders without a .hotspot file, for reporting

	const CachedDefinition& definitionFor( const Face& face ){
		const std::string shader = face.GetShader();
		auto it = m_cache.find( shader );
		if ( it != m_cache.end() )
			return it->second;

		CachedDefinition& cached = m_cache[shader];
		std::string text;
		// 1) next to the shader name   2) next to the editor image the shader uses
		bool ok = Hotspot_loadText( Hotspot_stripExtension( shader.c_str() ) + ".hotspot", text );
		if ( !ok && face.getShader().state() != nullptr ) {
			const char* image = face.getShader().state()->getTexture().name;
			if ( image != nullptr && *image != '\0' )
				ok = Hotspot_loadText( Hotspot_stripExtension( image ) + ".hotspot", text );
		}
		if ( ok ) {
			cached.found = hotspot::parse( text.c_str(), cached.def );
			if ( !cached.found )
				globalWarningStream() << "Hotspot: no rects in " << shader.c_str() << ".hotspot\n";
		}
		return cached;
	}

	/// Textures \p faces as one surface. All faces must share a shader and face the same way.
	void applyGroup( const std::vector<Face*>& faces ){
		Face& first = *faces.front();
		const CachedDefinition& cached = definitionFor( first );
		if ( !cached.found ) {
			m_noDefinition += faces.size();
			m_missing[first.GetShader()] = true;
			return;
		}

		const DoubleVector3 n = first.plane3().normal();
		DoubleVector3 u, v;
		if ( !face_axes( n, u, v ) ) {
			m_degenerate += faces.size();
			return;
		}

		// combined extents of all faces, measured along the shared axes
		double uMin = 1e30, uMax = -1e30, vMin = 1e30, vMax = -1e30;
		Vector3 centre( 0, 0, 0 );
		for ( Face* face : faces )
		{
			const Winding& winding = face->getWinding();
			for ( Winding::const_iterator i = winding.begin(); i != winding.end(); ++i )
			{
				const double pu = vector3_dot( ( *i ).vertex, u );
				const double pv = vector3_dot( ( *i ).vertex, v );
				uMin = std::fmin( uMin, pu ); uMax = std::fmax( uMax, pu );
				vMin = std::fmin( vMin, pv ); vMax = std::fmax( vMax, pv );
			}
			centre += face->centroid();
		}
		if ( uMax - uMin < 1e-3 || vMax - vMin < 1e-3 ) {
			m_degenerate += faces.size();
			return;
		}
		centre /= float( faces.size() );

		// position-based variation, so identical faces in different places can pick different
		// equally good rects (e.g. two identical narrow shells on the sheet)
		const unsigned int variation = unsigned( std::lround( std::fabs( centre.x() * 0.37 + centre.y() * 0.61 + centre.z() * 0.89 ) ) / 8 );

		const hotspot::Definition& def = cached.def;
		const int index = hotspot::pick( def, uMax - uMin, vMax - vMin, variation );
		if ( index < 0 ) {
			m_degenerate += faces.size();
			return;
		}
		const std::size_t imageW = first.getShader().width();
		const std::size_t imageH = first.getShader().height();
		const hotspot::Mapping m = hotspot::map( def, def.rects[index], double( imageW ), double( imageH ), uMin, uMax, vMin, vMax );
		const DoubleVector3 st[3] = { DoubleVector3( m.st[0][0], m.st[0][1], 0 ),
		                              DoubleVector3( m.st[1][0], m.st[1][1], 0 ),
		                              DoubleVector3( m.st[2][0], m.st[2][1], 0 ) };

		for ( Face* face : faces )
		{
			if ( face->getWinding().numpoints < 3 ) {
				++m_degenerate;
				continue;
			}
			// same u/v mapping, projected onto this face's own plane (parallel faces may be offset)
			const DoubleVector3 fn = face->plane3().normal();
			const double dist = vector3_dot( fn, face->getWinding()[0].vertex );
			const auto point = [&]( double pu, double pv ){
				const DoubleVector3 p = u * pu + v * pv;
				// slide along the group normal, so the point keeps its u/v coordinates
				return p + n * ( ( dist - vector3_dot( fn, p ) ) / vector3_dot( fn, n ) );
			};
			const DoubleVector3 points[3] = { point( uMin, vMax ), point( uMax, vMax ), point( uMin, vMin ) };

			TextureProjection projection;
			face->GetTexdef( projection );
			Texdef_from_ST( projection, points, st, imageW, imageH );
			projection.m_brushprimit_texdef.removeScale( imageW, imageH );
			face->SetTexdef( projection );
			++m_done;
		}
		++m_groups;
	}
};

class FaceCollect
{
	std::vector<Face*>& m_faces;
public:
	FaceCollect( std::vector<Face*>& faces ) : m_faces( faces ){
	}
	void operator()( Face& face ) const {
		for ( Face* f : m_faces )
			if ( f == &face )
				return;
		m_faces.push_back( &face );
	}
};

void hotspot_apply( bool grouped ){
	std::vector<Face*> faces;
	if ( GlobalSelectionSystem().Mode() != SelectionSystem::eComponent ) {
		Scene_ForEachSelectedBrush_ForEachFace( GlobalSceneGraph(), FaceCollect( faces ) );
	}
	Scene_ForEachSelectedBrushFace( GlobalSceneGraph(), FaceCollect( faces ) );
	if ( faces.empty() ) {
		globalOutputStream() << "Hotspot: nothing selected\n";
		return;
	}

	UndoableCommand undo( grouped ? "hotspotTextureGrouped" : "hotspotTexture" );
	HotspotApplier applier;

	if ( grouped ) {
		// group by texture + facing direction
		std::vector<std::vector<Face*>> groups;
		for ( Face* face : faces )
		{
			bool placed = false;
			for ( auto& group : groups )
			{
				Face* g = group.front();
				if ( string_equal( g->GetShader(), face->GetShader() )
				  && vector3_dot( g->plane3().normal(), face->plane3().normal() ) > 0.9999 ) {
					group.push_back( face );
					placed = true;
					break;
				}
			}
			if ( !placed )
				groups.push_back( { face } );
		}
		for ( const auto& group : groups )
			applier.applyGroup( group );
	}
	else{
		for ( Face* face : faces )
			applier.applyGroup( { face } );
	}

	globalOutputStream() << "Hotspot: " << applier.m_done << " face(s) textured";
	if ( grouped )
		globalOutputStream() << " in " << applier.m_groups << " group(s)";
	if ( applier.m_noDefinition != 0 )
		globalOutputStream() << ", " << applier.m_noDefinition << " skipped (no .hotspot file)";
	if ( applier.m_degenerate != 0 )
		globalOutputStream() << ", " << applier.m_degenerate << " skipped (degenerate)";
	globalOutputStream() << "\n";
	for ( const auto& missing : applier.m_missing )
		globalOutputStream() << "  no hotspot rects for " << missing.first.c_str() << " (expected " << Hotspot_stripExtension( missing.first.c_str() ).c_str() << ".hotspot)\n";

	SceneChangeNotify();
}

} // namespace

void Select_HotspotTexture(){
	hotspot_apply( false );
}

void Select_HotspotTextureGrouped(){
	hotspot_apply( true );
}

void Hotspot_registerCommands(){
	GlobalCommands_insert( "HotspotTexture", FreeCaller<Select_HotspotTexture>(), QKeySequence( "Ctrl+Shift+H" ) );
	GlobalCommands_insert( "HotspotTextureGrouped", FreeCaller<Select_HotspotTextureGrouped>(), QKeySequence( "Ctrl+Alt+H" ) );
	GlobalCommands_insert( "HotspotEditor", FreeCaller<HotspotEditor_show>() );
}
