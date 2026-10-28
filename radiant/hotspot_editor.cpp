/*
   Hotspot editor window: editor glue around hotspot::EditorWidget.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "hotspot.h"
#include "hotspot_editor_widget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ifilesystem.h"
#include "ishaders.h"
#include "iimage.h"
#include "itextstream.h"
#include "texturelib.h"

#include "image.h"
#include "mainframe.h"
#include "texwindow.h"

namespace
{

QWidget* g_hotspotWindow = nullptr;
hotspot::EditorWidget* g_hotspotEditor = nullptr;

/// Name of the image the editor shows for \p shaderName (shaders may use a different editor image).
std::string image_name_for( const char* shaderName ){
	std::string image = shaderName;
	IShader* shader = GlobalShaderSystem().getShaderForName( shaderName );
	if ( shader != nullptr ) {
		if ( shader->getTexture() != nullptr && shader->getTexture()->name != nullptr && *shader->getTexture()->name != '\0' )
			image = shader->getTexture()->name;
		shader->DecRef();
	}
	return Hotspot_stripExtension( image.c_str() );
}

bool is_directory_root( const char* root ){
	const std::size_t n = std::strlen( root );
	return n != 0 && ( root[n - 1] == '/' || root[n - 1] == '\\' );
}

/// Where the .hotspot file for \p shaderName lives, or should be written.
QString hotspot_save_path( const QString& shaderName ){
	const std::string name = shaderName.toStdString();
	const std::string relative = Hotspot_stripExtension( name.c_str() ) + ".hotspot";

	// 1) an existing loose .hotspot file
	const char* root = GlobalFileSystem().findFile( relative.c_str() );
	if ( is_directory_root( root ) )
		return QString::fromStdString( std::string( root ) + relative );

	// 2) the folder the texture image is in (if it's a loose file, not inside a pk3)
	const std::string image = image_name_for( name.c_str() );
	for ( const char* ext : { "tga", "png", "jpg", "jpeg", "dds", "webp", "bmp", "ktx", "crn", "pcx", "ktx2" } )
	{
		root = GlobalFileSystem().findFile( ( image + "." + ext ).c_str() );
		if ( is_directory_root( root ) )
			return QString::fromStdString( std::string( root ) + relative );
	}

	// 3) the game's own folder
	return QString::fromStdString( std::string( EnginePath_get() ) + gamename_get() + "/" + relative );
}

bool hotspot_load( const QString& shaderName, QImage& image, std::string& text ){
	const std::string name = shaderName.toStdString();
	const std::string imageName = image_name_for( name.c_str() );
	Image* loaded = QERApp_LoadImage( nullptr, imageName.c_str() );
	if ( loaded == nullptr && imageName != Hotspot_stripExtension( name.c_str() ) )
		loaded = QERApp_LoadImage( nullptr, Hotspot_stripExtension( name.c_str() ).c_str() );
	if ( loaded == nullptr ) {
		globalWarningStream() << "Hotspot editor: could not load image for " << name.c_str() << "\n";
		return false;
	}
	image = QImage( loaded->getRGBAPixels(), int( loaded->getWidth() ), int( loaded->getHeight() ), QImage::Format_RGBA8888 ).copy();
	loaded->release();

	text.clear();
	if ( !Hotspot_loadText( Hotspot_stripExtension( name.c_str() ) + ".hotspot", text ) )
		Hotspot_loadText( imageName + ".hotspot", text );
	return true;
}

bool hotspot_save( const QString& shaderName, const std::string& text, QString& result ){
	const QString path = hotspot_save_path( shaderName );
	QDir().mkpath( QFileInfo( path ).absolutePath() );
	QFile file( path );
	if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) ) {
		result = path + " (" + file.errorString() + ")";
		return false;
	}
	file.write( text.c_str(), qint64( text.size() ) );
	file.close();
	globalOutputStream() << "Hotspot editor: saved " << path.toUtf8().constData() << "\n";
	result = path;
	return true;
}

} // namespace

void HotspotEditor_show(){
	if ( g_hotspotWindow == nullptr ) {
		g_hotspotWindow = new QWidget( MainFrame_getWindow(), Qt::Window );
		g_hotspotWindow->setWindowTitle( "Hotspot Editor" );
		auto* layout = new QVBoxLayout( g_hotspotWindow );
		layout->setContentsMargins( 6, 6, 6, 6 );
		g_hotspotEditor = new hotspot::EditorWidget;
		layout->addWidget( g_hotspotEditor );

		g_hotspotEditor->browserTexture = [](){ return QString( TextureBrowser_GetSelectedShader() ); };
		g_hotspotEditor->load = hotspot_load;
		g_hotspotEditor->save = hotspot_save;
		g_hotspotEditor->savePath = hotspot_save_path;
		g_hotspotEditor->apply = Select_HotspotTexture;
		g_hotspotEditor->applyGrouped = Select_HotspotTextureGrouped;

		g_hotspotWindow->resize( 1150, 780 );
	}
	g_hotspotWindow->show();
	g_hotspotWindow->raise();
	g_hotspotWindow->activateWindow();
	if ( g_hotspotEditor->canvas()->image().isNull() )
		g_hotspotEditor->openTexture( TextureBrowser_GetSelectedShader() );
}
