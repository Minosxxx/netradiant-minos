/*
   Lighting preview: compiles the map with q3map2 in the background at a chosen quality
   and shows the result lit, the way the game renders it.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

void LightPreview_Construct();
void LightPreview_show();
/// Model files were reloaded from disk; the next preview update rebuilds the BSP.
void LightPreview_modelsChanged();
