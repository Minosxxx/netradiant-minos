/*
   Player preview: spawn points (info_player_deathmatch, ...) drawn as a standing player model.

   The model path "models/players/<name>/editor_preview.md3" doesn't exist in the game's files:
   the model is assembled on demand from the player's lower / upper / head .md3, animation.cfg and
   default skin, so nothing is copied out of the game's pk3s.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "playerpreview.h"
#include "playerpreview_core.h"

#include "iarchive.h"
#include "ifilesystem.h"
#include "idatastream.h"
#include "itextstream.h"
#include "eclasslib.h"
#include "preferencesystem.h"
#include "stringio.h"
#include "string/string.h"
#include "stream/stringstream.h"
#include "generic/callback.h"

#include "preferences.h"
#include "mainframe.h"

#include <cstring>

namespace
{

CopiedString g_spawnPreviewModel( "sarge" ); // player model folder name; empty: the plain box

class MemoryInputStream : public InputStream
{
	const std::vector<uint8_t>& m_data;
	std::size_t m_pos = 0;
public:
	MemoryInputStream( const std::vector<uint8_t>& data ) : m_data( data ){
	}
	size_type read( byte_type* buffer, size_type length ) override {
		const size_type n = std::min<size_type>( length, m_data.size() - m_pos );
		std::memcpy( buffer, m_data.data() + m_pos, n );
		m_pos += n;
		return n;
	}
};

class MemoryArchiveFile : public ArchiveFile
{
	std::string m_name;
	std::vector<uint8_t> m_data;
	MemoryInputStream m_stream;
public:
	MemoryArchiveFile( const char* name, std::vector<uint8_t>&& data ) : m_name( name ), m_data( std::move( data ) ), m_stream( m_data ){
	}
	void release() override {
		delete this;
	}
	std::size_t size() const override {
		return m_data.size();
	}
	const char* getName() const override {
		return m_name.c_str();
	}
	InputStream& getInputStream() override {
		return m_stream;
	}
};

bool read_game_file( const std::string& path, std::vector<uint8_t>& data ){
	void* buffer = nullptr;
	const std::size_t size = GlobalFileSystem().loadFile( path.c_str(), &buffer );
	if ( buffer == nullptr )
		return false;
	data.assign( static_cast<uint8_t*>( buffer ), static_cast<uint8_t*>( buffer ) + size );
	GlobalFileSystem().freeFile( buffer );
	return true;
}

/// A 32 x 32 x 56 box where the player stands, if the player model can't be built.
std::vector<uint8_t> fallback_box( const std::string& name ){
	playerpreview::Md3Surface s;
	s.name = "box";
	const float x0 = -16, x1 = 16, y0 = -16, y1 = 16, z0 = -24, z1 = 32;
	const playerpreview::Vec3 c[8] = { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y1, z0 }, { x0, y1, z0 },
	                                   { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } };
	const int quads[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
	const playerpreview::Vec3 normals[6] = { { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { -1, 0, 0 } };
	for ( int q = 0; q < 6; ++q )
	{
		const int base = int( s.xyz.size() );
		for ( int k = 0; k < 4; ++k )
		{
			s.xyz.push_back( c[quads[q][k]] );
			s.normals.push_back( normals[q] );
			s.st.push_back( k == 1 || k == 2 ? 1.f : 0.f );
			s.st.push_back( k >= 2 ? 1.f : 0.f );
		}
		for ( int t : { 0, 2, 1, 0, 3, 2 } ) // MD3 triangles are clockwise from the front
			s.triangles.push_back( base + t );
	}
	return playerpreview::write_md3( name, { s } );
}

} // namespace

ArchiveFile* PlayerPreview_open( const char* name ){
	std::string folder = playerpreview::preview_folder( name );
	if ( folder.empty() ) {
		// editor_preview_<skin>.md3: same model, another skin (team colours)
		std::string path( name );
		const size_t slash = path.find_last_of( "/\\" );
		const std::string file = slash == std::string::npos ? path : path.substr( slash + 1 );
		const std::string prefix = "editor_preview_";
		if ( slash == std::string::npos || file.size() <= prefix.size() + 4 || string_equal_nocase_n( file.c_str(), prefix.c_str(), prefix.size() ) == false )
			return nullptr;
		const std::string skin = file.substr( prefix.size(), file.size() - prefix.size() - 4 );
		folder = playerpreview::preview_folder( path.substr( 0, slash + 1 ) + "editor_preview.md3" );
		if ( folder.empty() )
			return nullptr;
		std::vector<uint8_t> data;
		std::string error;
		if ( !playerpreview::build_player_preview( read_game_file, folder, skin, data, error ) ) {
			globalWarningStream() << "Player preview " << name << ": " << error.c_str() << '\n';
			data = fallback_box( name );
		}
		return new MemoryArchiveFile( name, std::move( data ) );
	}
	std::vector<uint8_t> data;
	std::string error;
	if ( !playerpreview::build_player_preview( read_game_file, folder, "default", data, error ) ) {
		globalWarningStream() << "Player preview " << name << ": " << error.c_str() << '\n';
		data = fallback_box( name );
	}
	return new MemoryArchiveFile( name, std::move( data ) );
}

void PlayerPreview_applyTo( EntityClass& eclass ){
	if ( string_empty( g_spawnPreviewModel.c_str() ) || !eclass.fixedsize || !string_empty( eclass.modelpath() ) )
		return;
	if ( !string_equal( g_pGameDescription->mGameType.c_str(), "q3" ) )
		return;
	const char* name = eclass.name();
	const char* skin = nullptr;
	if ( string_equal_nocase( name, "info_player_deathmatch" ) || string_equal_nocase( name, "info_player_start" ) )
		skin = "";
	else if ( string_equal_nocase( name, "team_CTF_redplayer" ) || string_equal_nocase( name, "team_CTF_redspawn" ) )
		skin = "_red";
	else if ( string_equal_nocase( name, "team_CTF_blueplayer" ) || string_equal_nocase( name, "team_CTF_bluespawn" ) )
		skin = "_blue";
	if ( skin == nullptr )
		return;
	eclass.m_modelpath = StringStream<128>( "models/players/", g_spawnPreviewModel.c_str(), "/editor_preview", skin, ".md3" );
}

namespace
{
void PlayerPreview_constructPreferences( PreferencesPage& page ){
	page.appendEntry( "Spawn point player model (restart; empty: box)", g_spawnPreviewModel );
}
void PlayerPreview_constructPage( PreferenceGroup& group ){
	PreferencesPage page( group.createPage( "Spawn Points", "Spawn Point Display" ) );
	PlayerPreview_constructPreferences( page );
}
}

void PlayerPreview_Construct(){
	GlobalPreferenceSystem().registerPreference( "SpawnPreviewModel", CopiedStringImportStringCaller( g_spawnPreviewModel ), CopiedStringExportStringCaller( g_spawnPreviewModel ) );
	PreferencesDialog_addDisplayPage( FreeCaller1<PreferenceGroup&, PlayerPreview_constructPage>() );
}
