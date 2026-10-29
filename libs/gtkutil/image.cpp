/*
   Copyright (C) 2001-2006, William Joseph.
   All Rights Reserved.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "image.h"

#include "string/string.h"
#include "stream/stringstream.h"
#include "stream/textstream.h"


namespace
{
CopiedString g_bitmapsPath;
}

void BitmapsPath_set( const char* path ){
	g_bitmapsPath = path;
}

QPixmap new_local_image( const char* filename ){
	const auto fullPath = StringStream( g_bitmapsPath, filename );
	return QPixmap( QString( fullPath.c_str() ) );
}

QIcon new_local_icon( const char* filename ){
	const auto fullPath = StringStream( g_bitmapsPath, filename );
	// Qt never enlarges raster icons, so supply crisp 2x/3x/4x versions for big toolbar icon sizes
	// (Preferences > Layout > Icon size); in-between sizes are scaled down from the next larger one
	const QPixmap pixmap( fullPath.c_str() );
	if ( pixmap.isNull() )
		return QIcon( fullPath.c_str() );
	QIcon icon( pixmap );
	for ( int factor = 2; factor <= 4; ++factor )
		icon.addPixmap( pixmap.scaled( pixmap.size() * factor, Qt::KeepAspectRatio, Qt::FastTransformation ) );
	return icon;
}
