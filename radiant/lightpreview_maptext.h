/*
   Light preview: change detection for exported .map text.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QList>

namespace lightpreview
{

/// Point entities whose geometry q3map2 bakes into the BSP during -bsp, so -onlyents can't update them.
inline bool entity_is_baked_geometry( const QByteArray& keys ){
	for ( const QByteArray& line : keys.split( '\n' ) )
	{
		if ( !line.startsWith( "\"classname\"" ) )
			continue;
		const QByteArray value = line.mid( 11 ).trimmed();
		return value == "\"misc_model\"" || value == "\"_decal\"" || value == "\"_skybox\"";
	}
	return false;
}

/// Hashes of an exported .map: everything, and only what the BSP stage depends on
/// (brush/patch contents, the keys of entities that own brushes, and point entities whose
/// geometry is baked in: misc_model, _decal, _skybox).
inline void hash_map_text( const QByteArray& text, QByteArray& fullHash, QByteArray& geometryHash ){
	fullHash = QCryptographicHash::hash( text, QCryptographicHash::Sha1 );
	QCryptographicHash geometry( QCryptographicHash::Sha1 );
	int depth = 0;
	QByteArray entityKeys;
	bool entityHasBrushes = false;
	for ( const QByteArray& rawLine : text.split( '\n' ) )
	{
		const QByteArray line = rawLine.trimmed();
		if ( line.isEmpty() || line.startsWith( "//" ) )
			continue;
		if ( line == "{" ) {
			++depth;
			if ( depth == 1 ) {
				entityKeys.clear();
				entityHasBrushes = false;
			}
			if ( depth == 2 )
				entityHasBrushes = true;
			continue;
		}
		if ( line == "}" ) {
			if ( depth == 1 && ( entityHasBrushes || entity_is_baked_geometry( entityKeys ) ) )
				geometry.addData( entityKeys );
			--depth;
			continue;
		}
		if ( depth == 1 )
			entityKeys += line + '\n';
		else if ( depth >= 2 )
			geometry.addData( line + '\n' );
	}
	geometryHash = geometry.result();
}

} // namespace lightpreview
