/*
   Light preview: per-game profiles.

   A profile says how to compile a preview (q3map2 arguments, quality presets) and how the
   game's renderer turns lightmap bytes into screen colours, so the preview can match in-game.

   To support another game: add an entry to game_profiles() and, if its renderer differs from
   vanilla Quake 3, give it its own EngineModel (see Quake3EngineModel below).

   No editor dependencies, so it can be tested standalone.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace lightpreview
{

/// Renderer settings that change how lighting looks (cvars of the game).
struct EngineParams
{
	int mapOverBrightBits = 2;  // r_mapOverBrightBits
	int overBrightBits = 1;     // r_overBrightBits
	float gamma = 1.0f;         // r_gamma
	float intensity = 1.0f;     // r_intensity
	bool hardwareGamma = true;  // overbright needs hardware gamma; vanilla Q3 also disables it in windowed mode
};

/// Vanilla Quake 3 (id tech 3, 1.32) lighting pipeline, ported from tr_bsp.c / tr_image.c.
/// ioquake3 uses the same code with the same defaults.
struct Quake3EngineModel
{
	EngineParams p;

	/// tr.overbrightBits as set by R_SetColorMappings()
	int effectiveOverbrightBits() const {
		if ( !p.hardwareGamma )
			return 0;
		return std::clamp( p.overBrightBits, 0, 2 ); // 24-bit colour allows up to 2
	}

	/// R_ColorShiftLightingBytes(): applied to lightmaps and vertex colours at map load
	void shiftLightingBytes( const uint8_t in[3], uint8_t out[3] ) const {
		const int shift = std::max( 0, p.mapOverBrightBits - effectiveOverbrightBits() );
		int r = in[0] << shift, g = in[1] << shift, b = in[2] << shift;
		if ( ( r | g | b ) > 255 ) { // normalize by colour instead of saturating to white
			const int max = std::max( { r, g, b } );
			r = r * 255 / max;
			g = g * 255 / max;
			b = b * 255 / max;
		}
		out[0] = uint8_t( r ); out[1] = uint8_t( g ); out[2] = uint8_t( b );
	}

	float clampedGamma() const {
		return std::clamp( p.gamma, 0.5f, 3.0f );
	}

	/// s_gammatable[]: the hardware gamma ramp (includes the overbright shift)
	uint8_t gammaTable( int i, bool withOverbright = true ) const {
		const float g = clampedGamma();
		int v = ( g == 1.0f ) ? i : int( 255 * std::pow( i / 255.0f, 1.0f / g ) + 0.5f );
		if ( withOverbright )
			v <<= effectiveOverbrightBits();
		return uint8_t( std::clamp( v, 0, 255 ) );
	}

	/// s_intensitytable[]
	uint8_t intensityTable( int i ) const {
		const float intensity = std::max( 1.0f, p.intensity );
		return uint8_t( std::min( 255, int( i * intensity ) ) );
	}

	/// R_LightScaleTexture() for mipmapped (diffuse) textures
	uint8_t textureByte( uint8_t v ) const {
		return p.hardwareGamma ? intensityTable( v ) : gammaTable( intensityTable( v ), false );
	}

	/// R_LightScaleTexture() for lightmaps (only_gamma)
	uint8_t lightmapByteAfterShift( uint8_t v ) const {
		return p.hardwareGamma ? v : gammaTable( v, false );
	}

	/// The screen colour for a framebuffer value (what the hardware gamma ramp does).
	uint8_t displayByte( uint8_t framebuffer ) const {
		return p.hardwareGamma ? gammaTable( framebuffer ) : framebuffer;
	}

	/// Converts a raw lightmap page (as written by q3map2) to what the engine uploads.
	void prepareLightmap( const uint8_t* rgbIn, std::size_t pixels, std::vector<uint8_t>& rgbOut ) const {
		rgbOut.resize( pixels * 3 );
		for ( std::size_t i = 0; i < pixels; ++i )
		{
			shiftLightingBytes( rgbIn + i * 3, &rgbOut[i * 3] );
			for ( int c = 0; c < 3; ++c )
				rgbOut[i * 3 + c] = lightmapByteAfterShift( rgbOut[i * 3 + c] );
		}
	}

	/// Expected screen colour of a lightmapped, textured pixel (CPU reference used by tests):
	/// stage 1 draws the lightmap, stage 2 multiplies the texture in (GL_DST_COLOR, GL_ZERO).
	uint8_t screenByte( uint8_t rawLightmap, uint8_t textureRaw ) const {
		uint8_t in[3] = { rawLightmap, rawLightmap, rawLightmap }, lm[3];
		shiftLightingBytes( in, lm );
		const int fb = ( lightmapByteAfterShift( lm[0] ) * textureByte( textureRaw ) + 127 ) / 255;
		return displayByte( uint8_t( fb ) );
	}
};

struct QualityPreset
{
	std::string name;
	std::string lightArgs; // q3map2 -light arguments
};

struct GameProfile
{
	std::string id;
	std::string title;
	std::vector<std::string> basegames;  // which gamepacks use this profile (by basegame folder)
	std::string q3map2Game;              // -game for q3map2 when the gamepack defines no q3map2 build tool
	std::string bspArgs;                 // extra -bsp arguments for the preview compile
	EngineParams engine;
	std::vector<QualityPreset> presets;  // ordered draft .. final
};

inline const std::vector<GameProfile>& game_profiles(){
	static const std::vector<GameProfile> profiles = {
		{
			"quake3", "Quake III Arena (vanilla / ioquake3)",
			{ "baseq3", "missionpack" },
			"quake3",
			"-meta",
			EngineParams{},  // r_mapOverBrightBits 2, r_overBrightBits 1, r_gamma 1, r_intensity 1, fullscreen
			{
				{ "Draft",  "-faster -nogrid" },
				{ "Medium", "-fast -nogrid -bounce 1" },
				{ "High",   "-fast -nogrid -samples 2 -filter -bounce 2" },
				{ "Final",  "-fast -samples 3 -filter -bounce 8 -patchshadows" },
			},
		},
		// Add more games here, e.g. { "wolfet", "Wolfenstein: Enemy Territory", { "etmain" }, "et", ... }
	};
	return profiles;
}

/// Splits the options of a q3map2 -light command into those that change how lighting looks
/// (kept for every preview quality) and those that only trade speed for quality (replaced by presets).
/// \p args are the tokens after "-light", without the map file.
inline void split_light_options( const std::vector<std::string>& args, std::string& look, std::string& quality ){
	static const char* const qualityFlags[] = { "-fast", "-faster", "-fastbounce", "-fastgrid", "-fastallocate", "-slowallocate",
	                                            "-cheap", "-cheapgrid", "-filter", "-nogrid", "-bouncegrid", "-fastlightmapsearch" };
	static const char* const qualityWithValue[] = { "-samples", "-super", "-bounce", "-threads" };
	static const char* const dropWithValue[] = { "-connect" };
	static const char* const drop[] = { "-v", "-light" };
	look.clear();
	quality.clear();
	const auto append = []( std::string& to, const std::string& token ){
		if ( !to.empty() )
			to += ' ';
		to += token.find_first_of( " \t" ) != std::string::npos ? '"' + token + '"' : token;
	};
	const auto in = []( const std::string& token, const auto& list ){
		for ( const char* f : list )
			if ( token == f )
				return true;
		return false;
	};
	for ( std::size_t i = 0; i < args.size(); ++i )
	{
		const std::string& a = args[i];
		if ( in( a, drop ) )
			continue;
		if ( in( a, dropWithValue ) ) {
			++i;
			continue;
		}
		if ( in( a, qualityFlags ) ) {
			append( quality, a );
		}
		else if ( in( a, qualityWithValue ) ) {
			append( quality, a );
			if ( i + 1 < args.size() )
				append( quality, args[++i] );
		}
		else{
			append( look, a );
		}
	}
}

/// Picks the profile for a gamepack's basegame folder; falls back to the first (Quake 3).
inline const GameProfile& profile_for_basegame( const char* basegame ){
	for ( const GameProfile& p : game_profiles() )
		for ( const std::string& b : p.basegames )
			if ( basegame != nullptr && b == basegame )
				return p;
	return game_profiles().front();
}

} // namespace lightpreview
