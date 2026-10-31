/*
   FBX export: the selection or the whole map, as meshes with UVs, normals and textured materials.

   Objects: one per entity that has visible faces (worldspawn, func_group, brush entities), one per
   model (pivot at the entity origin). Brush faces stay polygons; patches are quads; models triangles.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "fbxexport.h"
#include "fbxwriter.h"

#include "ientity.h"
#include "ifilesystem.h"
#include "imodelgeometry.h"
#include "iscenegraph.h"
#include "iselection.h"
#include "ishaders.h"
#include "itextstream.h"
#include "qerplugin.h"
#include "preferencesystem.h"
#include "scenelib.h"
#include "stringio.h"
#include "os/path.h"
#include "stream/stringstream.h"
#include "generic/callback.h"

#include "brush.h"
#include "brushnode.h"
#include "patch.h"
#include "map.h"
#include "mainframe.h"
#include "commands.h"
#include "qe3.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QPushButton>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>

namespace
{

enum Preset { PresetMaya, PresetBlender, PresetQuake, PresetMetricYUp, PresetCount };

const char* const c_presetNames[PresetCount] = {
	"Maya: Y up, 1 unit = 1 cm",
	"Blender: Z up, 1 unit = 1/32 m",
	"Quake: Z up, 1 unit = 1 cm (raw map coordinates)",
	"Metric Y up: 1 unit = 1/32 m (other engines/tools)",
};

int g_fbxPreset = PresetMaya;
bool g_fbxToolFaces = false;
CopiedString g_fbxLastFolder;

struct Conversion
{
	bool yUp;
	double scale;
	fbx::Axes axes;
	void point( const Vector3& p, double out[3] ) const {
		if ( yUp ) { out[0] = p.x() * scale; out[1] = p.z() * scale; out[2] = -p.y() * scale; }
		else       { out[0] = p.x() * scale; out[1] = p.y() * scale; out[2] = p.z() * scale; }
	}
	void direction( const Vector3& n, double out[3] ) const {
		if ( yUp ) { out[0] = n.x(); out[1] = n.z(); out[2] = -n.y(); }
		else       { out[0] = n.x(); out[1] = n.y(); out[2] = n.z(); }
	}
};

Conversion conversion_for( int preset ){
	Conversion c;
	switch ( preset )
	{
	case PresetBlender:   // Blender's own axes: Z up, -Y forward; values in metres
		c.yUp = false; c.scale = 1.0 / 32.0;
		c.axes.up = 2; c.axes.upSign = 1; c.axes.front = 1; c.axes.frontSign = -1; c.axes.coord = 0; c.axes.coordSign = 1;
		c.axes.unitScale = 100.0;
		break;
	case PresetQuake:
		c.yUp = false; c.scale = 1.0;
		c.axes.up = 2; c.axes.upSign = 1; c.axes.front = 1; c.axes.frontSign = -1; c.axes.coord = 0; c.axes.coordSign = 1;
		c.axes.unitScale = 1.0;
		break;
	case PresetMetricYUp:
		c.yUp = true; c.scale = 1.0 / 32.0;
		c.axes.up = 1; c.axes.upSign = 1; c.axes.front = 2; c.axes.frontSign = 1; c.axes.coord = 0; c.axes.coordSign = 1;
		c.axes.unitScale = 100.0;
		break;
	default:              // Maya: Y up, centimetres, 1:1 with the MD3 exporter at scale 1
		c.yUp = true; c.scale = 1.0;
		c.axes.up = 1; c.axes.upSign = 1; c.axes.front = 2; c.axes.frontSign = 1; c.axes.coord = 0; c.axes.coordSign = 1;
		c.axes.unitScale = 1.0;
		break;
	}
	return c;
}

/// Caulk, clip, nodraw, triggers, hint/skip, sky, fog...
bool is_tool_shader( const char* name ){
	static std::map<std::string, bool> cache; // cleared per export
	const auto found = cache.find( name );
	if ( found != cache.end() )
		return found->second;
	bool tool = string_equal_prefix_nocase( name, "textures/common/" );
	if ( !tool ) {
		if ( IShader* shader = GlobalShaderSystem().getShaderForName( name ) ) {
			tool = ( shader->getFlags() & ( QER_NODRAW | QER_CLIP | QER_SKY | QER_FOG ) ) != 0;
			shader->DecRef();
		}
	}
	cache.emplace( name, tool );
	return tool;
}

/// The image file a shader shows, as an absolute path on disk ("" if it's inside a pk3 or missing).
std::string texture_file( const char* shaderName ){
	std::string image = shaderName;
	if ( IShader* shader = GlobalShaderSystem().getShaderForName( shaderName ) ) {
		if ( shader->getTexture() != nullptr && shader->getTexture()->name != nullptr && *shader->getTexture()->name != '\0' )
			image = shader->getTexture()->name;
		shader->DecRef();
	}
	const size_t slash = image.find_last_of( '/' ), dot = image.find_last_of( '.' );
	const std::string stem = ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) ) ? image.substr( 0, dot ) : image;
	for ( const char* ext : { ".tga", ".jpg", ".png", ".jpeg", ".dds", ".bmp" } )
	{
		const std::string candidate = stem + ext;
		const char* root = GlobalFileSystem().findFile( candidate.c_str() );
		if ( root == nullptr || *root == '\0' )
			continue;
		std::string folder = root;
		std::replace( folder.begin(), folder.end(), '\\', '/' );
		if ( folder.empty() || folder.back() != '/' )
			return "";   // inside a pk3: no file to point at
		return folder + candidate;
	}
	return "";
}

class Exporter
{
public:
	Conversion conv;
	bool toolFaces;
	bool selectedOnly;
	fbx::Scene scene;
	std::map<std::string, int> materialIndex;
	std::map<int, size_t> meshOfEntity;   // entity number -> scene mesh
	int entityCount = -1;
	std::map<std::string, int> nameCount;
	size_t faces = 0;

	int material( const char* shader ){
		const auto found = materialIndex.find( shader );
		if ( found != materialIndex.end() )
			return found->second;
		const int index = int( scene.materials.size() );
		scene.materials.push_back( { shader, texture_file( shader ) } );
		materialIndex.emplace( shader, index );
		return index;
	}

	std::string uniqueName( std::string name ){
		for ( char& c : name )
			if ( !( std::isalnum( static_cast<unsigned char>( c ) ) || c == '_' || c == '-' ) )
				c = '_';
		const int n = nameCount[name]++;
		return n == 0 ? name : name + "_" + std::to_string( n + 1 );
	}

	static std::string entity_name( Entity& entity ){
		const char* classname = entity.getClassName();
		if ( string_equal( classname, "worldspawn" ) )
			return "worldspawn";
		const char* targetname = entity.getKeyValue( "targetname" );
		if ( !string_empty( targetname ) )
			return std::string( classname ) + "_" + targetname;
		return classname;
	}

	fbx::Mesh& meshForEntity( int entityNumber, Entity& entity ){
		const auto found = meshOfEntity.find( entityNumber );
		if ( found != meshOfEntity.end() )
			return scene.meshes[found->second];
		scene.meshes.emplace_back();
		scene.meshes.back().name = uniqueName( entity_name( entity ) );
		meshOfEntity.emplace( entityNumber, scene.meshes.size() - 1 );
		return scene.meshes.back();
	}

	fbx::MeshBuilder::Corner corner( const Vector3& p, const Vector3& n, float s, float t, const double origin[3] ) const {
		fbx::MeshBuilder::Corner c;
		conv.point( p, c.xyz );
		for ( int i = 0; i < 3; ++i )
			c.xyz[i] -= origin[i];
		conv.direction( n, c.normal );
		c.uv[0] = s;
		c.uv[1] = 1.0 - t;   // Quake t runs down the image, FBX v up
		return c;
	}

	/// Puts \p corners in counter-clockwise order around \p normal.
	static void face_front( std::vector<fbx::MeshBuilder::Corner>& corners, const double normal[3] ){
		double n[3] = { 0, 0, 0 };   // Newell normal
		for ( size_t i = 0; i < corners.size(); ++i )
		{
			const double* a = corners[i].xyz;
			const double* b = corners[( i + 1 ) % corners.size()].xyz;
			n[0] += ( a[1] - b[1] ) * ( a[2] + b[2] );
			n[1] += ( a[2] - b[2] ) * ( a[0] + b[0] );
			n[2] += ( a[0] - b[0] ) * ( a[1] + b[1] );
		}
		if ( n[0] * normal[0] + n[1] * normal[1] + n[2] * normal[2] < 0 )
			std::reverse( corners.begin(), corners.end() );
	}

	void addBrush( Brush& brush, const Matrix4& toWorld, fbx::Mesh& mesh ){
		brush.evaluateBRep();
		fbx::MeshBuilder builder( mesh );
		const double zero[3] = { 0, 0, 0 };
		class Visitor : public BrushVisitor
		{
			Exporter& m_exporter;
			fbx::MeshBuilder& m_builder;
			const Matrix4& m_toWorld;
			const double* m_zero;
		public:
			Visitor( Exporter& e, fbx::MeshBuilder& b, const Matrix4& m, const double* z ) : m_exporter( e ), m_builder( b ), m_toWorld( m ), m_zero( z ){
			}
			void visit( Face& face ) const override {
				if ( !face.contributes() )
					return;
				const char* shader = face.GetShader();
				if ( !m_exporter.toolFaces && is_tool_shader( shader ) )
					return;
				const Winding& w = face.getWinding();
				const Vector3 normal = vector3_normalised( matrix4_transformed_direction( m_toWorld, face.plane3().normal() ) );
				std::vector<fbx::MeshBuilder::Corner> corners;
				for ( std::size_t i = 0; i < w.numpoints; ++i )
					corners.push_back( m_exporter.corner( matrix4_transformed_point( m_toWorld, Vector3( w[i].vertex ) ), normal, w[i].texcoord.x(), w[i].texcoord.y(), m_zero ) );
				double n[3];
				m_exporter.conv.direction( normal, n );
				face_front( corners, n );
				m_builder.addPolygon( corners, m_exporter.material( shader ) );
				++m_exporter.faces;
			}
		};
		brush.forEachFace( Visitor( *this, builder, toWorld, zero ) );
	}

	void addPatch( const Patch& patch, const Matrix4& toWorld, fbx::Mesh& mesh ){
		const char* shader = patch.GetShader();
		if ( !toolFaces && is_tool_shader( shader ) )
			return;
		const PatchTesselation& tess = patch.getTesselation();
		fbx::MeshBuilder builder( mesh );
		const int mat = material( shader );
		const double zero[3] = { 0, 0, 0 };
		for ( std::size_t s = 0; s < tess.m_numStrips; ++s )
		{
			const RenderIndex* strip = tess.m_indices.data() + s * tess.m_lenStrips;
			for ( std::size_t k = 0; k + 3 < tess.m_lenStrips; k += 2 )   // quad strip: (0 1 3 2), (2 3 5 4)...
			{
				std::vector<fbx::MeshBuilder::Corner> corners;
				double avg[3] = { 0, 0, 0 };
				for ( std::size_t i : { k, k + 1, k + 3, k + 2 } )
				{
					const ArbitraryMeshVertex& v = tess.m_vertices[strip[i]];
					const Vector3 n = vector3_normalised( matrix4_transformed_direction( toWorld, Vector3( v.normal ) ) );
					corners.push_back( corner( matrix4_transformed_point( toWorld, Vector3( v.vertex ) ), n, v.texcoord.s(), v.texcoord.t(), zero ) );
					for ( int a = 0; a < 3; ++a )
						avg[a] += corners.back().normal[a];
				}
				face_front( corners, avg );
				builder.addPolygon( corners, mat );
				++faces;
			}
		}
	}

	void addModel( const ModelGeometry& model, const Matrix4& toWorld, Entity& entity ){
		std::string name = entity.getKeyValue( "model" );
		const size_t slash = name.find_last_of( "/\\" ), dot = name.find_last_of( '.' );
		name = name.substr( slash == std::string::npos ? 0 : slash + 1 );
		if ( dot != std::string::npos && dot > ( slash == std::string::npos ? 0 : slash ) )
			name = name.substr( 0, name.find_last_of( '.' ) );
		scene.meshes.emplace_back();
		fbx::Mesh& mesh = scene.meshes.back();
		mesh.name = uniqueName( name.empty() ? "model" : name );
		conv.point( toWorld.t().vec3(), mesh.translation );   // pivot at the entity origin
		fbx::MeshBuilder builder( mesh );
		model.forEachTriangle( [&]( const char* shader, const ModelGeometry::Vertex& a, const ModelGeometry::Vertex& b, const ModelGeometry::Vertex& c ){
			std::vector<fbx::MeshBuilder::Corner> corners;
			double avg[3] = { 0, 0, 0 };
			for ( const ModelGeometry::Vertex* v : { &a, &b, &c } )
			{
				const Vector3 n = vector3_normalised( matrix4_transformed_direction( toWorld, Vector3( v->normal[0], v->normal[1], v->normal[2] ) ) );
				corners.push_back( corner( matrix4_transformed_point( toWorld, Vector3( v->xyz[0], v->xyz[1], v->xyz[2] ) ), n, v->st[0], v->st[1], mesh.translation ) );
				for ( int i = 0; i < 3; ++i )
					avg[i] += corners.back().normal[i];
			}
			face_front( corners, avg );
			builder.addPolygon( corners, material( shader ) );
			++faces;
		} );
	}

	void gather(){
		class Walker : public scene::Graph::Walker
		{
			Exporter& m_exporter;
			mutable std::vector<bool> m_selectedAbove;
			mutable Entity* m_entity = nullptr;
		public:
			Walker( Exporter& e ) : m_exporter( e ){
			}
			bool pre( const scene::Path& path, scene::Instance& instance ) const override {
				if ( !path.top().get().visible() )
					return false;   // hidden or filtered
				const bool selected = Instance_isSelected( instance ) || ( !m_selectedAbove.empty() && m_selectedAbove.back() );
				m_selectedAbove.push_back( selected );
				if ( path.size() == 2 ) {
					m_entity = Node_getEntity( path.top() );
					++m_exporter.entityCount;
				}
				if ( m_entity == nullptr || ( m_exporter.selectedOnly && !selected ) )
					return true;
				const Matrix4& toWorld = instance.localToWorld();
				if ( Brush* brush = Node_getBrush( path.top() ) )
					m_exporter.addBrush( *brush, toWorld, m_exporter.meshForEntity( m_exporter.entityCount, *m_entity ) );
				else if ( Patch* patch = Node_getPatch( path.top() ) )
					m_exporter.addPatch( *patch, toWorld, m_exporter.meshForEntity( m_exporter.entityCount, *m_entity ) );
				else if ( const ModelGeometry* model = NodeTypeCast<ModelGeometry>::cast( path.top() ) ) {
					// misc_model and the like (a "model" key with a file); not editor-only models such
					// as item or spawn point previews
					const char* key = m_entity->getKeyValue( "model" );
					if ( !string_empty( key ) && key[0] != '*' )
						m_exporter.addModel( *model, toWorld, *m_entity );
				}
				return true;
			}
			void post( const scene::Path& path, scene::Instance& instance ) const override {
				m_selectedAbove.pop_back();
			}
		};
		GlobalSceneGraph().traverse( Walker( *this ) );
		// drop entities that ended up with no faces (e.g. only tool faces)
		scene.meshes.erase( std::remove_if( scene.meshes.begin(), scene.meshes.end(), []( const fbx::Mesh& m ){ return m.polygonMaterial.empty(); } ), scene.meshes.end() );
	}
};

/// Preset and options; false if cancelled.
bool ask_options( bool selectedOnly ){
	QDialog dialog( MainFrame_getWindow() );
	dialog.setWindowTitle( selectedOnly ? "Export Selected to FBX" : "Export Map to FBX" );
	auto* layout = new QVBoxLayout( &dialog );
	auto* form = new QFormLayout;
	auto* preset = new QComboBox;
	for ( const char* name : c_presetNames )
		preset->addItem( name );
	preset->setCurrentIndex( std::max( 0, std::min( g_fbxPreset, int( PresetCount ) - 1 ) ) );
	form->addRow( "Target", preset );
	auto* tools = new QCheckBox( "Include tool faces (caulk, clip, nodraw, triggers, sky...)" );
	tools->setChecked( g_fbxToolFaces );
	form->addRow( tools );
	layout->addLayout( form );
	auto* note = new QLabel( "One object per entity (worldspawn, func_group, brush entities) and per model, "
	                         "with each model's pivot at its origin. Textures are linked by file path." );
	note->setWordWrap( true );
	layout->addWidget( note );
	auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
	buttons->button( QDialogButtonBox::Ok )->setText( "Export..." );
	QObject::connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
	QObject::connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
	layout->addWidget( buttons );
	if ( dialog.exec() != QDialog::Accepted )
		return false;
	g_fbxPreset = preset->currentIndex();
	g_fbxToolFaces = tools->isChecked();
	return true;
}

void export_fbx( bool selectedOnly ){
	if ( selectedOnly && GlobalSelectionSystem().countSelected() == 0 ) {
		QMessageBox::information( MainFrame_getWindow(), "Export Selected to FBX", "Nothing is selected." );
		return;
	}
	if ( !ask_options( selectedOnly ) )
		return;

	QString suggestion;
	{
		const QString mapName = Map_Unnamed( g_map ) ? QString( "map" ) : QFileInfo( Map_Name( g_map ) ).completeBaseName();
		const QString folder = !string_empty( g_fbxLastFolder.c_str() ) ? QString( g_fbxLastFolder.c_str() )
		                       : Map_Unnamed( g_map ) ? QString() : QFileInfo( Map_Name( g_map ) ).absolutePath();
		suggestion = ( folder.isEmpty() ? QString() : folder + "/" ) + mapName + ( selectedOnly ? "_selected.fbx" : ".fbx" );
	}
	QString path = QFileDialog::getSaveFileName( MainFrame_getWindow(), selectedOnly ? "Export Selected to FBX" : "Export Map to FBX",
	                                             suggestion, "FBX (*.fbx)" );
	if ( path.isEmpty() )
		return;
	if ( !path.endsWith( ".fbx", Qt::CaseInsensitive ) )
		path += ".fbx";
	g_fbxLastFolder = QFileInfo( path ).absolutePath().toUtf8().constData();

	ScopeDisableScreenUpdates disableScreenUpdates( "Processing...", "Exporting FBX" );
	Exporter exporter;
	exporter.conv = conversion_for( g_fbxPreset );
	exporter.toolFaces = g_fbxToolFaces;
	exporter.selectedOnly = selectedOnly;
	exporter.scene.axes = exporter.conv.axes;
	exporter.scene.creator = "NetRadiant-custom FBX export";
	exporter.scene.folder = QFileInfo( path ).absolutePath().toUtf8().constData();
	exporter.gather();
	if ( exporter.scene.meshes.empty() ) {
		QMessageBox::information( MainFrame_getWindow(), "FBX export", "Nothing to export: no visible faces"
		                          + QString( g_fbxToolFaces ? "." : " (tool faces are skipped)." ) );
		return;
	}
	const std::vector<uint8_t> data = fbx::write_binary( exporter.scene );
	QFile file( path );
	if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate )
	  || file.write( reinterpret_cast<const char*>( data.data() ), qint64( data.size() ) ) != qint64( data.size() ) ) {
		globalErrorStream() << "FBX export: could not write " << path.toUtf8().constData() << '\n';
		QMessageBox::warning( MainFrame_getWindow(), "FBX export", "Could not write " + path );
		return;
	}
	globalOutputStream() << "FBX export: wrote " << path.toUtf8().constData() << " (" << exporter.scene.meshes.size() << " object(s), "
	                     << exporter.faces << " face(s), " << exporter.scene.materials.size() << " material(s))\n";
}

void ExportSelectedFBX(){
	export_fbx( true );
}
void ExportMapFBX(){
	export_fbx( false );
}

} // namespace

void FbxExport_Construct(){
	GlobalCommands_insert( "ExportSelectedFBX", FreeCaller<ExportSelectedFBX>() );
	GlobalCommands_insert( "ExportMapFBX", FreeCaller<ExportMapFBX>() );
	GlobalPreferenceSystem().registerPreference( "FbxPreset", IntImportStringCaller( g_fbxPreset ), IntExportStringCaller( g_fbxPreset ) );
	GlobalPreferenceSystem().registerPreference( "FbxToolFaces", BoolImportStringCaller( g_fbxToolFaces ), BoolExportStringCaller( g_fbxToolFaces ) );
	GlobalPreferenceSystem().registerPreference( "FbxLastFolder", CopiedStringImportStringCaller( g_fbxLastFolder ), CopiedStringExportStringCaller( g_fbxLastFolder ) );
}
