/*
   Player preview: spawn points (info_player_deathmatch, ...) drawn as a standing player model.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

class ArchiveFile;
class EntityClass;

/// For "models/players/<name>/editor_preview[_<skin>].md3" returns a generated in-memory model
/// (the player assembled in its idle pose); 0 for any other path.
ArchiveFile* PlayerPreview_open( const char* name );

/// Gives the spawn point entity classes the preview model, if enabled. Called after entity
/// definitions are loaded.
void PlayerPreview_applyTo( EntityClass& eclass );

void PlayerPreview_Construct();
