/*
   Hotspot texturing.

   Select faces (or whole brushes) and run "Hotspot Texture": each face gets the rectangle
   of its texture's hotspot sheet whose shape matches it best, fitted to the face.
   Rectangles marked tile_h / tile_v repeat along one axis instead of stretching.

   Rectangles are read from "<texture>.hotspot" next to the texture, e.g.
   textures/base/concrete_hs.tga  ->  textures/base/concrete_hs.hotspot
   (see hotspot_core.h for the format).

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "hotspot.h"
#include "hotspot_core.h"

#include <map>
#include <string>

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

namespace
{

struct CachedDefinition
{
	bool found = false;
	hotspot::Definition def;
};

bool load_text( const std::string& path, std::string& text ){
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

std::string strip_extension( const char* name ){
	std::string s( name );
	const std::size_t slash = s.find_last_of( "/\\" );
	const std::size_t dot = s.find_last_of( '.' );
	if ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) )
		s.erase( dot );
	return s;
}

class HotspotApplier
{
	std::map<std::string, CachedDefinition> m_cache;
public:
	std::size_t m_done = 0;
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
		bool ok = load_text( strip_extension( shader.c_str() ) + ".hotspot", text );
		if ( !ok && face.getShader().state() != nullptr ) {
			const char* image = face.getShader().state()->getTexture().name;
			if ( image != nullptr && *image != '\0' )
				ok = load_text( strip_extension( image ) + ".hotspot", text );
		}
		if ( ok ) {
			cached.found = hotspot::parse( text.c_str(), cached.def );
			if ( !cached.found )
				globalWarningStream() << "Hotspot: no rects in " << shader.c_str() << ".hotspot\n";
		}
		return cached;
	}

	void apply( Face& face ){
		const CachedDefinition& cached = definitionFor( face );
		if ( !cached.found ) {
			++m_noDefinition;
			m_missing[face.GetShader()] = true;
			return;
		}
		const Winding& winding = face.getWinding();
		if ( winding.numpoints < 3 ) {
			++m_degenerate;
			return;
		}

		// face axes: v = "up" on the face, u = "right" when looking at the face from the front
		const DoubleVector3 n = face.plane3().normal();
		const DoubleVector3 worldUp = std::fabs( n.z() ) < 0.7071
		                              ? DoubleVector3( 0, 0, 1 )   // walls and slopes: up is +Z
		                              : DoubleVector3( 0, 1, 0 );  // floors and ceilings: up is +Y
		DoubleVector3 v = worldUp - n * vector3_dot( worldUp, n );
		if ( vector3_length( v ) < 1e-6 ) {
			++m_degenerate;
			return;
		}
		v = vector3_normalised( v );
		const DoubleVector3 u = vector3_normalised( vector3_cross( v, n ) );
		const double dist = vector3_dot( n, winding[0].vertex );

		double uMin = 1e30, uMax = -1e30, vMin = 1e30, vMax = -1e30;
		for ( Winding::const_iterator i = winding.begin(); i != winding.end(); ++i )
		{
			const double pu = vector3_dot( ( *i ).vertex, u );
			const double pv = vector3_dot( ( *i ).vertex, v );
			uMin = std::fmin( uMin, pu ); uMax = std::fmax( uMax, pu );
			vMin = std::fmin( vMin, pv ); vMax = std::fmax( vMax, pv );
		}
		if ( uMax - uMin < 1e-3 || vMax - vMin < 1e-3 ) {
			++m_degenerate;
			return;
		}

		// position-based variation, so identical faces in different places can pick different
		// equally good rects (e.g. two identical narrow shells on the sheet)
		const Vector3& c = face.centroid();
		const unsigned int variation = unsigned( std::lround( std::fabs( c.x() * 0.37 + c.y() * 0.61 + c.z() * 0.89 ) ) / 8 );

		const hotspot::Definition& def = cached.def;
		const int index = hotspot::pick( def, uMax - uMin, vMax - vMin, variation );
		if ( index < 0 ) {
			++m_degenerate;
			return;
		}
		const std::size_t imageW = face.getShader().width();
		const std::size_t imageH = face.getShader().height();
		const hotspot::Mapping m = hotspot::map( def, def.rects[index], double( imageW ), double( imageH ), uMin, uMax, vMin, vMax );

		const auto point = [&]( double pu, double pv ){
			return u * pu + v * pv + n * dist;
		};
		const DoubleVector3 points[3] = { point( uMin, vMax ), point( uMax, vMax ), point( uMin, vMin ) };
		const DoubleVector3 st[3] = { DoubleVector3( m.st[0][0], m.st[0][1], 0 ),
		                              DoubleVector3( m.st[1][0], m.st[1][1], 0 ),
		                              DoubleVector3( m.st[2][0], m.st[2][1], 0 ) };

		TextureProjection projection;
		face.GetTexdef( projection );
		Texdef_from_ST( projection, points, st, imageW, imageH );
		projection.m_brushprimit_texdef.removeScale( imageW, imageH );
		face.SetTexdef( projection );
		++m_done;
	}
};

class FaceApply
{
	HotspotApplier& m_applier;
public:
	FaceApply( HotspotApplier& applier ) : m_applier( applier ){
	}
	void operator()( Face& face ) const {
		m_applier.apply( face );
	}
};

} // namespace

void Select_HotspotTexture(){
	UndoableCommand undo( "hotspotTexture" );
	HotspotApplier applier;

	if ( GlobalSelectionSystem().Mode() != SelectionSystem::eComponent ) {
		Scene_ForEachSelectedBrush_ForEachFace( GlobalSceneGraph(), FaceApply( applier ) );
	}
	Scene_ForEachSelectedBrushFace( GlobalSceneGraph(), FaceApply( applier ) );

	globalOutputStream() << "Hotspot: " << applier.m_done << " face(s) textured";
	if ( applier.m_noDefinition != 0 )
		globalOutputStream() << ", " << applier.m_noDefinition << " skipped (no .hotspot file)";
	if ( applier.m_degenerate != 0 )
		globalOutputStream() << ", " << applier.m_degenerate << " skipped (degenerate)";
	globalOutputStream() << "\n";
	for ( const auto& missing : applier.m_missing )
		globalOutputStream() << "  no hotspot rects for " << missing.first.c_str() << " (expected " << strip_extension( missing.first.c_str() ).c_str() << ".hotspot)\n";

	SceneChangeNotify();
}

void Hotspot_registerCommands(){
	GlobalCommands_insert( "HotspotTexture", FreeCaller<Select_HotspotTexture>(), QKeySequence( "Ctrl+Shift+H" ) );
}
