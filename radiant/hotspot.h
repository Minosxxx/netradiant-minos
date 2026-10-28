/*
   Hotspot texturing: fit selected faces to the best matching rectangle of their texture.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include <string>

void Select_HotspotTexture();
void Select_HotspotTextureGrouped();
void Hotspot_registerCommands();

/// Opens the hotspot rectangle editor window (hotspot_editor.cpp).
void HotspotEditor_show();

/// Reads a text file through the virtual filesystem.
bool Hotspot_loadText( const std::string& path, std::string& text );
std::string Hotspot_stripExtension( const char* name );
