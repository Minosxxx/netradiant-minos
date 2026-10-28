/*
   Lighting preview window.

   Pipeline: the map (or active region) is exported to the settings folder, compiled with the
   gamepack's q3map2 (-bsp, no vis) and lit at the chosen quality preset, then the BSP is shown
   in a view that reproduces the game's lightmap/overbright/gamma pipeline (lightpreview_view).
   If only entities changed since the last compile (e.g. a light was moved or recoloured), the
   BSP stage is skipped and just the entity lump is updated (-bsp -onlyents), then relit.

   Game specifics live in lightpreview_profile.h.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "lightpreview.h"
#include "lightpreview_bsp.h"
#include "lightpreview_profile.h"
#include "lightpreview_view.h"
#include "lightpreview_maptext.h"

#include <map>

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QShortcut>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QRegularExpression>

#include "ishaders.h"
#include "iimage.h"
#include "iscenegraph.h"
#include "itextstream.h"
#include "preferencesystem.h"
#include "stringio.h"
#include "texturelib.h"
#include "os/path.h"
#include "stream/stringstream.h"
#include "generic/callback.h"
#include "signal/isignal.h"

#include "build.h"
#include "camwindow.h"
#include "commands.h"
#include "gtkutil/accelerator.h"
#include "image.h"
#include "mainframe.h"
#include "map.h"
#include "qe3.h"

namespace
{

using namespace lightpreview;

// ---------------------------------------------------------------------------------------- settings

const int c_presetCount = 4;

struct Settings
{
	int quality = 0;
	bool autoUpdate = true;
	bool followCamera = true;
	int viewMode = 0;
	CopiedString presetArgs[c_presetCount];
	CopiedString bspArgs;
	int mapOverBrightBits = 2;
	int overBrightBits = 1;
	float gamma = 1;
	float intensity = 1;
	bool hardwareGamma = true;
};

Settings g_settings;
const GameProfile* g_profile = nullptr;

void settings_reset_to_profile(){
	const GameProfile& p = *g_profile;
	for ( int i = 0; i < c_presetCount; ++i )
		g_settings.presetArgs[i] = i < int( p.presets.size() ) ? p.presets[i].lightArgs.c_str() : "";
	g_settings.bspArgs = p.bspArgs.c_str();
	g_settings.mapOverBrightBits = p.engine.mapOverBrightBits;
	g_settings.overBrightBits = p.engine.overBrightBits;
	g_settings.gamma = p.engine.gamma;
	g_settings.intensity = p.engine.intensity;
	g_settings.hardwareGamma = p.engine.hardwareGamma;
}

EngineParams settings_engine(){
	EngineParams e;
	e.mapOverBrightBits = g_settings.mapOverBrightBits;
	e.overBrightBits = g_settings.overBrightBits;
	e.gamma = g_settings.gamma;
	e.intensity = g_settings.intensity;
	e.hardwareGamma = g_settings.hardwareGamma;
	return e;
}

QString preset_name( int i ){
	if ( g_profile != nullptr && i < int( g_profile->presets.size() ) )
		return QString::fromStdString( g_profile->presets[i].name );
	return QString( "Preset %1" ).arg( i + 1 );
}

// ---------------------------------------------------------------------------------------- materials

std::map<std::string, QImage> g_imageCache;

QImage load_image_cached( const std::string& name ){
	auto it = g_imageCache.find( name );
	if ( it != g_imageCache.end() )
		return it->second;
	QImage image;
	if ( Image* loaded = QERApp_LoadImage( nullptr, name.c_str() ) ) {
		image = QImage( loaded->getRGBAPixels(), int( loaded->getWidth() ), int( loaded->getHeight() ), QImage::Format_RGBA8888 ).copy();
		loaded->release();
	}
	g_imageCache[name] = image;
	return image;
}

std::string strip_extension( const std::string& s ){
	const std::size_t slash = s.find_last_of( "/\\" ), dot = s.find_last_of( '.' );
	return ( dot != std::string::npos && ( slash == std::string::npos || dot > slash ) ) ? s.substr( 0, dot ) : s;
}

Material material_for( const std::string& shaderName ){
	Material m;
	std::string image = shaderName;
	if ( IShader* shader = GlobalShaderSystem().getShaderForName( shaderName.c_str() ) ) {
		const int flags = shader->getFlags();
		m.sky = ( flags & QER_SKY ) != 0;
		m.translucent = ( flags & QER_TRANS ) != 0;
		m.alpha = m.translucent ? shader->getTrans() : 1.0f;
		m.alphaTest = ( flags & QER_ALPHATEST ) != 0;
		m.invisible = ( flags & ( QER_NODRAW | QER_FOG | QER_CLIP ) ) != 0;
		if ( shader->getTexture() != nullptr && shader->getTexture()->name != nullptr && *shader->getTexture()->name != '\0' )
			image = shader->getTexture()->name;
		shader->DecRef();
	}
	m.image = load_image_cached( strip_extension( image ) );
	if ( m.image.isNull() && image != shaderName )
		m.image = load_image_cached( strip_extension( shaderName ) );
	return m;
}

// ---------------------------------------------------------------------------------------- window

class PreviewWindow : public QWidget
{
	View* m_view;
	QComboBox* m_quality;
	QComboBox* m_mode;
	QCheckBox* m_auto;
	QCheckBox* m_follow;
	QPushButton* m_compile;
	QLabel* m_status;
	QPlainTextEdit* m_log;
	QWidget* m_settingsPanel;
	QLineEdit* m_presetEdits[c_presetCount];
	QLineEdit* m_bspArgsEdit;
	QSpinBox* m_mapObb;
	QSpinBox* m_obb;
	QDoubleSpinBox* m_gamma;
	QDoubleSpinBox* m_intensity;
	QCheckBox* m_hwGamma;

	QProcess m_process;
	struct Step
	{
		QString label;
		QString program;
		QStringList args;
	};
	std::vector<Step> m_steps;
	std::size_t m_step = 0;
	QElapsedTimer m_stepClock;
	QElapsedTimer m_totalClock;
	QStringList m_stepTimes;
	bool m_leaked = false;

	QTimer m_debounce;
	QTimer m_followTimer;
	bool m_exporting = false;
	bool m_pending = false;
	bool m_bspValid = false;
	QByteArray m_lastFullHash, m_lastGeometryHash;   // of the last successful compile
	QByteArray m_runFullHash, m_runGeometryHash;     // of the compile in progress
	int m_lastQuality = -1;
	int m_runQuality = -1;

	QString m_dir, m_mapPath, m_bspPath;

public:
	PreviewWindow() : QWidget( MainFrame_getWindow(), Qt::Window ){
		setWindowTitle( "Lighting Preview" );
		m_dir = QString( SettingsPath_get() ) + "lightpreview/";
		QDir().mkpath( m_dir );
		m_mapPath = m_dir + "preview.map";
		m_bspPath = m_dir + "preview.bsp";

		auto* root = new QVBoxLayout( this );
		root->setContentsMargins( 6, 6, 6, 6 );

		{ // toolbar
			auto* bar = new QHBoxLayout;
			root->addLayout( bar );
			m_compile = new QPushButton( "Compile" );
			m_compile->setToolTip( "Compile and light the map now (F5)" );
			bar->addWidget( m_compile );
			bar->addWidget( new QLabel( "Quality" ) );
			m_quality = new QComboBox;
			for ( int i = 0; i < c_presetCount; ++i )
				m_quality->addItem( preset_name( i ) );
			m_quality->setCurrentIndex( std::clamp( g_settings.quality, 0, c_presetCount - 1 ) );
			bar->addWidget( m_quality );
			m_auto = new QCheckBox( "Auto update" );
			m_auto->setToolTip( "Recompile automatically shortly after the map changes" );
			m_auto->setChecked( g_settings.autoUpdate );
			bar->addWidget( m_auto );
			bar->addSpacing( 12 );
			bar->addWidget( new QLabel( "Show" ) );
			m_mode = new QComboBox;
			m_mode->addItems( { "Lit", "Lighting only", "Fullbright" } );
			m_mode->setCurrentIndex( std::clamp( g_settings.viewMode, 0, 2 ) );
			bar->addWidget( m_mode );
			m_follow = new QCheckBox( "Follow editor camera" );
			m_follow->setChecked( g_settings.followCamera );
			bar->addWidget( m_follow );
			auto* toEditor = new QToolButton;
			toEditor->setText( "Editor camera here" );
			toEditor->setToolTip( "Move the editor's 3D camera to this preview's position" );
			bar->addWidget( toEditor );
			bar->addStretch( 1 );
			auto* settingsBtn = new QToolButton;
			settingsBtn->setText( "Settings" );
			settingsBtn->setCheckable( true );
			bar->addWidget( settingsBtn );
			auto* logBtn = new QToolButton;
			logBtn->setText( "Log" );
			logBtn->setCheckable( true );
			bar->addWidget( logBtn );

			QObject::connect( m_compile, &QPushButton::clicked, [this](){ requestCompile( true ); } );
			QObject::connect( m_quality, QOverload<int>::of( &QComboBox::currentIndexChanged ), [this]( int i ){
				g_settings.quality = i;
				requestCompile( true );
			} );
			QObject::connect( m_auto, &QCheckBox::toggled, [this]( bool on ){
				g_settings.autoUpdate = on;
				if ( on )
					m_debounce.start();
			} );
			QObject::connect( m_mode, QOverload<int>::of( &QComboBox::currentIndexChanged ), [this]( int i ){
				g_settings.viewMode = i;
				m_view->setMode( ViewMode( i ) );
			} );
			QObject::connect( m_follow, &QCheckBox::toggled, [this]( bool on ){
				g_settings.followCamera = on;
				if ( on ) syncCamera();
			} );
			QObject::connect( toEditor, &QToolButton::clicked, [this](){ cameraToEditor(); } );
			QObject::connect( settingsBtn, &QToolButton::toggled, [this]( bool on ){ m_settingsPanel->setVisible( on ); } );
			QObject::connect( logBtn, &QToolButton::toggled, [this]( bool on ){ m_log->setVisible( on ); } );
		}

		auto* split = new QSplitter( Qt::Horizontal );
		root->addWidget( split, 1 );
		auto* vsplit = new QSplitter( Qt::Vertical );
		split->addWidget( vsplit );
		m_view = new View;
		m_view->materialFor = material_for;
		m_view->setEngine( settings_engine() );
		m_view->setMode( ViewMode( m_mode->currentIndex() ) );
		m_view->onUserCamera = [this](){ m_follow->setChecked( false ); };
		vsplit->addWidget( m_view );
		m_log = new QPlainTextEdit;
		m_log->setReadOnly( true );
		m_log->setMaximumBlockCount( 4000 );
		m_log->setFont( QFont( "monospace" ) );
		m_log->setVisible( false );
		vsplit->addWidget( m_log );
		vsplit->setStretchFactor( 0, 3 );
		vsplit->setStretchFactor( 1, 1 );

		m_settingsPanel = buildSettingsPanel();
		m_settingsPanel->setVisible( false );
		split->addWidget( m_settingsPanel );
		split->setStretchFactor( 0, 1 );

		m_status = new QLabel( "Ready." );
		m_status->setTextInteractionFlags( Qt::TextSelectableByMouse );
		root->addWidget( m_status );

		auto* f5 = new QShortcut( QKeySequence( Qt::Key_F5 ), this );
		QObject::connect( f5, &QShortcut::activated, [this](){ requestCompile( true ); } );

		m_process.setProcessChannelMode( QProcess::MergedChannels );
		m_process.setWorkingDirectory( m_dir );
		QObject::connect( &m_process, &QProcess::readyReadStandardOutput, [this](){
			const QString text = QString::fromLocal8Bit( m_process.readAllStandardOutput() );
			if ( text.contains( "LEAKED" ) )
				m_leaked = true;
			m_log->moveCursor( QTextCursor::End );
			m_log->insertPlainText( text );
			m_log->moveCursor( QTextCursor::End );
		} );
		QObject::connect( &m_process, QOverload<int, QProcess::ExitStatus>::of( &QProcess::finished ), [this]( int code, QProcess::ExitStatus status ){
			stepFinished( status == QProcess::NormalExit && code == 0 );
		} );
		QObject::connect( &m_process, &QProcess::errorOccurred, [this]( QProcess::ProcessError error ){
			if ( error == QProcess::FailedToStart ) {
				appendLog( "Could not start " + m_process.program() + "\n" );
				stepFinished( false );
			}
		} );

		m_debounce.setSingleShot( true );
		m_debounce.setInterval( 700 );
		QObject::connect( &m_debounce, &QTimer::timeout, [this](){ debounceFired(); } );

		m_followTimer.setInterval( 33 );
		QObject::connect( &m_followTimer, &QTimer::timeout, [this](){
			if ( m_follow->isChecked() && isVisible() )
				syncCamera();
		} );
		m_followTimer.start();

		resize( 1100, 720 );
	}

	void mapChanged(){
		if ( m_exporting || !isVisible() )
			return;
		if ( m_view->hasMap() && !isRunning() )
			m_view->setOverlayText( m_auto->isChecked() ? QString() : QString( "Map changed since this preview. Press Compile (F5)." ) );
		if ( m_auto->isChecked() )
			m_debounce.start();
	}

	void requestCompile( bool forced ){
		if ( isRunning() ) {
			m_pending = true;
			if ( forced ) { // restart with the latest map/settings
				m_process.kill();
				m_process.waitForFinished( 2000 );
			}
			return;
		}
		startCompile( forced );
	}

	void showAndCompile(){
		show();
		raise();
		activateWindow();
		syncCamera();
		if ( !m_view->hasMap() && !isRunning() )
			requestCompile( true );
	}

private:
	bool isRunning() const {
		return m_process.state() != QProcess::NotRunning || ( !m_steps.empty() && m_step < m_steps.size() );
	}

	void appendLog( const QString& text ){
		m_log->moveCursor( QTextCursor::End );
		m_log->insertPlainText( text );
	}

	QWidget* buildSettingsPanel(){
		auto* panel = new QWidget;
		panel->setMinimumWidth( 320 );
		auto* v = new QVBoxLayout( panel );
		v->setContentsMargins( 6, 0, 0, 0 );
		{
			auto* box = new QGroupBox( "Quality presets (q3map2 -light options)" );
			v->addWidget( box );
			auto* form = new QFormLayout( box );
			for ( int i = 0; i < c_presetCount; ++i )
			{
				m_presetEdits[i] = new QLineEdit( g_settings.presetArgs[i].c_str() );
				form->addRow( preset_name( i ), m_presetEdits[i] );
				QObject::connect( m_presetEdits[i], &QLineEdit::editingFinished, [this, i](){
					g_settings.presetArgs[i] = m_presetEdits[i]->text().toUtf8().constData();
					if ( i == m_quality->currentIndex() )
						requestCompile( true );
				} );
			}
			m_bspArgsEdit = new QLineEdit( g_settings.bspArgs.c_str() );
			m_bspArgsEdit->setToolTip( "Extra q3map2 -bsp options for the preview compile (vis is skipped)" );
			form->addRow( "BSP stage", m_bspArgsEdit );
			QObject::connect( m_bspArgsEdit, &QLineEdit::editingFinished, [this](){
				g_settings.bspArgs = m_bspArgsEdit->text().toUtf8().constData();
				m_bspValid = false; // force a full BSP stage next time
			} );
		}
		{
			auto* box = new QGroupBox( "Game renderer" );
			v->addWidget( box );
			auto* form = new QFormLayout( box );
			form->addRow( new QLabel( QString::fromStdString( g_profile->title ) ) );
			m_mapObb = new QSpinBox; m_mapObb->setRange( 0, 4 ); m_mapObb->setValue( g_settings.mapOverBrightBits );
			m_obb = new QSpinBox; m_obb->setRange( 0, 2 ); m_obb->setValue( g_settings.overBrightBits );
			m_gamma = new QDoubleSpinBox; m_gamma->setRange( 0.5, 3.0 ); m_gamma->setSingleStep( 0.05 ); m_gamma->setValue( g_settings.gamma );
			m_intensity = new QDoubleSpinBox; m_intensity->setRange( 1.0, 4.0 ); m_intensity->setSingleStep( 0.05 ); m_intensity->setValue( g_settings.intensity );
			m_hwGamma = new QCheckBox( "Fullscreen / hardware gamma" );
			m_hwGamma->setToolTip( "Overbright needs hardware gamma; vanilla Quake 3 has none in windowed mode" );
			m_hwGamma->setChecked( g_settings.hardwareGamma );
			form->addRow( "r_mapOverBrightBits", m_mapObb );
			form->addRow( "r_overBrightBits", m_obb );
			form->addRow( "r_gamma", m_gamma );
			form->addRow( "r_intensity", m_intensity );
			form->addRow( m_hwGamma );
			const auto apply = [this](){
				g_settings.mapOverBrightBits = m_mapObb->value();
				g_settings.overBrightBits = m_obb->value();
				g_settings.gamma = float( m_gamma->value() );
				g_settings.intensity = float( m_intensity->value() );
				g_settings.hardwareGamma = m_hwGamma->isChecked();
				m_view->setEngine( settings_engine() );
			};
			QObject::connect( m_mapObb, QOverload<int>::of( &QSpinBox::valueChanged ), apply );
			QObject::connect( m_obb, QOverload<int>::of( &QSpinBox::valueChanged ), apply );
			QObject::connect( m_gamma, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), apply );
			QObject::connect( m_intensity, QOverload<double>::of( &QDoubleSpinBox::valueChanged ), apply );
			QObject::connect( m_hwGamma, &QCheckBox::toggled, apply );
		}
		{
			auto* reset = new QPushButton( "Reset to game defaults" );
			v->addWidget( reset );
			QObject::connect( reset, &QPushButton::clicked, [this](){
				settings_reset_to_profile();
				for ( int i = 0; i < c_presetCount; ++i )
					m_presetEdits[i]->setText( g_settings.presetArgs[i].c_str() );
				m_bspArgsEdit->setText( g_settings.bspArgs.c_str() );
				m_mapObb->setValue( g_settings.mapOverBrightBits );
				m_obb->setValue( g_settings.overBrightBits );
				m_gamma->setValue( g_settings.gamma );
				m_intensity->setValue( g_settings.intensity );
				m_hwGamma->setChecked( g_settings.hardwareGamma );
				m_view->setEngine( settings_engine() );
			} );
			auto* reload = new QPushButton( "Reload textures" );
			v->addWidget( reload );
			QObject::connect( reload, &QPushButton::clicked, [this](){
				g_imageCache.clear();
				loadBsp();
			} );
		}
		auto* help = new QLabel( "Preview view: drag to look, WASD to move, Q/E down/up, Shift faster, wheel changes speed. "
		                         "Moving the preview camera turns off Follow." );
		help->setWordWrap( true );
		help->setStyleSheet( "color: gray;" );
		v->addWidget( help );
		v->addStretch( 1 );
		return panel;
	}

	// ---- camera

	void syncCamera(){
		CamWnd* cam = GlobalCamera_getCamWndPtr();
		if ( cam == nullptr )
			return;
		const Vector3& o = Camera_getOrigin( *cam );
		const Vector3& a = Camera_getAngles( *cam );
		CameraState c = m_view->camera();
		const bool same = c.origin[0] == o[0] && c.origin[1] == o[1] && c.origin[2] == o[2]
		               && c.pitch == a[CAMERA_PITCH] && c.yaw == a[CAMERA_YAW] && c.fov == Camera_getFieldOfView();
		if ( same )
			return;
		c.origin[0] = o[0]; c.origin[1] = o[1]; c.origin[2] = o[2];
		c.pitch = a[CAMERA_PITCH];
		c.yaw = a[CAMERA_YAW];
		c.fov = Camera_getFieldOfView();
		m_view->setCamera( c );
	}

	void cameraToEditor(){
		CamWnd* cam = GlobalCamera_getCamWndPtr();
		if ( cam == nullptr )
			return;
		const CameraState& c = m_view->camera();
		Camera_setOrigin( *cam, Vector3( c.origin[0], c.origin[1], c.origin[2] ) );
		Vector3 angles = Camera_getAngles( *cam );
		angles[CAMERA_PITCH] = c.pitch;
		angles[CAMERA_YAW] = c.yaw;
		Camera_setAngles( *cam, angles );
	}

	// ---- compiling

	void debounceFired(){
		if ( !m_auto->isChecked() || !isVisible() )
			return;
		if ( QGuiApplication::mouseButtons() != Qt::NoButton ) { // still dragging something: wait
			m_debounce.start();
			return;
		}
		requestCompile( false );
	}

	bool q3map2Command( QString& program, QStringList& args ){
		CopiedString command;
		bsp_init();
		build_set_variable( "MonitorAddress", "" ); // no network monitor for preview compiles
		const bool fromGamepack = build_evaluate_tool( "q3map2", command );
		bsp_shutdown();
		QString cmd;
		if ( fromGamepack ) {
			cmd = QString::fromUtf8( command.c_str() );
		}
		else{ // gamepack has no q3map2 tool: build one from the profile
			cmd = QString( "\"%1q3map2.%2\" -game %3 -fs_basepath \"%4\"" )
			      .arg( AppPath_get() ).arg( RADIANT_EXECUTABLE )
			      .arg( QString::fromStdString( g_profile->q3map2Game ) ).arg( EnginePath_get() );
			if ( !string_equal( gamename_get(), basegame_get() ) )
				cmd += QString( " -fs_game %1" ).arg( gamename_get() );
		}
		QStringList parts = QProcess::splitCommand( cmd );
		if ( parts.isEmpty() )
			return false;
		program = parts.takeFirst();
		args = parts;
		return true;
	}

	void startCompile( bool forced ){
		m_pending = false;

		// export
		m_exporting = true;
		const bool exported = Map_ExportForPreview( m_mapPath.toUtf8().constData() );
		m_exporting = false;
		if ( !exported ) {
			m_status->setText( "Could not write " + m_mapPath );
			return;
		}
		QFile file( m_mapPath );
		if ( !file.open( QIODevice::ReadOnly ) ) {
			m_status->setText( "Could not read " + m_mapPath );
			return;
		}
		hash_map_text( file.readAll(), m_runFullHash, m_runGeometryHash );
		file.close();

		const int quality = m_quality->currentIndex();
		if ( !forced && m_bspValid && m_runFullHash == m_lastFullHash && quality == m_lastQuality ) {
			m_view->setOverlayText( QString() );
			return; // nothing changed
		}
		m_runQuality = quality;

		QString program;
		QStringList base;
		if ( !q3map2Command( program, base ) ) {
			m_status->setText( "No q3map2 command available for this game" );
			return;
		}

		m_steps.clear();
		const bool entitiesOnly = m_bspValid && m_runGeometryHash == m_lastGeometryHash && QFileInfo::exists( m_bspPath );
		if ( entitiesOnly ) {
			m_steps.push_back( { "entities", program, base + QStringList{ "-bsp", "-onlyents", m_mapPath } } );
		}
		else{
			m_steps.push_back( { "bsp", program, base + QStringList{ "-bsp" }
			                     + QProcess::splitCommand( QString::fromUtf8( g_settings.bspArgs.c_str() ) ) + QStringList{ m_mapPath } } );
		}
		m_steps.push_back( { "light", program, base + QStringList{ "-light" }
		                     + QProcess::splitCommand( QString::fromUtf8( g_settings.presetArgs[quality].c_str() ) ) + QStringList{ m_bspPath } } );

		m_log->clear();
		m_stepTimes.clear();
		m_leaked = false;
		m_step = 0;
		m_totalClock.start();
		m_compile->setText( "Cancel" );
		QObject::disconnect( m_compile, nullptr, nullptr, nullptr );
		QObject::connect( m_compile, &QPushButton::clicked, [this](){ cancel(); } );
		runStep();
	}

	void runStep(){
		const Step& s = m_steps[m_step];
		const QString what = QString( "Compiling (%1): %2…" ).arg( preset_name( m_runQuality ), s.label );
		m_status->setText( what );
		m_view->setOverlayText( what );
		appendLog( "> " + s.program + " " + s.args.join( ' ' ) + "\n" );
		m_stepClock.start();
		m_process.start( s.program, s.args );
	}

	void cancel(){
		m_pending = false;
		m_steps.clear();
		m_process.kill();
		m_process.waitForFinished( 2000 );
		finishUi();
		m_status->setText( "Cancelled." );
		m_view->setOverlayText( QString() );
	}

	void finishUi(){
		m_compile->setText( "Compile" );
		QObject::disconnect( m_compile, nullptr, nullptr, nullptr );
		QObject::connect( m_compile, &QPushButton::clicked, [this](){ requestCompile( true ); } );
	}

	void stepFinished( bool ok ){
		if ( m_steps.empty() || m_step >= m_steps.size() )
			return; // cancelled
		m_stepTimes << QString( "%1 %2 s" ).arg( m_steps[m_step].label ).arg( m_stepClock.elapsed() / 1000.0, 0, 'f', 1 );
		if ( !ok ) {
			const bool restart = m_pending;
			if ( m_steps[m_step].label != "light" )
				m_bspValid = false;
			m_steps.clear();
			finishUi();
			if ( restart ) {
				startCompile( true );
				return;
			}
			m_status->setText( "Compile failed during " + m_stepTimes.last().section( ' ', 0, 0 ) + ". See the log." );
			m_view->setOverlayText( "Compile failed: see the log." );
			m_log->setVisible( true );
			return;
		}
		if ( ++m_step < m_steps.size() ) {
			runStep();
			return;
		}
		m_steps.clear();
		finishUi();
		m_bspValid = true;
		m_lastFullHash = m_runFullHash;
		m_lastGeometryHash = m_runGeometryHash;
		m_lastQuality = m_runQuality;
		const bool loaded = loadBsp();
		if ( loaded ) {
			QString status = QString( "%1 preview in %2 s (%3)" )
			                 .arg( preset_name( m_runQuality ) )
			                 .arg( m_totalClock.elapsed() / 1000.0, 0, 'f', 1 )
			                 .arg( m_stepTimes.join( ", " ) );
			status += ". " + lightSummary();
			if ( g_region_active )
				status += ". Region only: the region's walls block sun and sky light from outside it";
			if ( m_leaked )
				status += ". Warning: the map leaks";
			m_status->setText( status );
			m_view->setOverlayText( QString() );
		}
		if ( m_pending )
			requestCompile( false );
	}

	/// What q3map2 reported creating, e.g. "Lights: 12 point, 1 spot, 0 area, 901 sun/sky (sky: textures/x)".
	QString lightSummary() const {
		const QString log = m_log->toPlainText();
		const auto count = [&]( const char* label ){
			const QRegularExpression re( QString( "(\\d+)\\s+%1" ).arg( QRegularExpression::escape( label ) ) );
			QString last = "?";
			for ( auto it = re.globalMatch( log ); it.hasNext(); )
				last = it.next().captured( 1 );
			return last;
		};
		QString s = QString( "Lights: %1 point, %2 spot, %3 area, %4 sun/sky" )
		            .arg( count( "point lights" ), count( "spotlights" ), count( "diffuse (area) lights" ), count( "sun/sky lights" ) );
		QStringList skies;
		const QRegularExpression skyRe( "^(?:Sky|Sun): (\\S+)", QRegularExpression::MultilineOption );
		for ( auto it = skyRe.globalMatch( log ); it.hasNext(); )
		{
			const QString name = it.next().captured( 1 );
			if ( !skies.contains( name ) )
				skies << name;
		}
		if ( !skies.isEmpty() )
			s += " (from " + skies.join( ", " ) + ")";
		return s;
	}

	bool loadBsp(){
		QFile file( m_bspPath );
		if ( !file.open( QIODevice::ReadOnly ) ) {
			m_status->setText( "Could not open " + m_bspPath );
			return false;
		}
		const QByteArray bytes = file.readAll();
		std::vector<uint8_t> data( bytes.begin(), bytes.end() );
		Bsp bsp;
		std::string error;
		if ( !load_bsp( data, bsp, error ) ) {
			m_status->setText( "Could not read the compiled map: " + QString::fromStdString( error ) );
			return false;
		}
		bool needsLightmaps = false;
		for ( const Surface& s : bsp.surfaces )
			needsLightmaps |= s.lightmap >= int( bsp.lightmaps.size() );
		if ( needsLightmaps )
			appendLog( "Note: this compile wrote external lightmaps, which the preview doesn't load yet; "
			           "remove -external / -lightmapsize from the preset. Those surfaces are shown vertex lit.\n" );
		m_view->setMap( bsp );
		return true;
	}
};

PreviewWindow* g_window = nullptr;

struct SceneHook {} g_sceneHook;

void sceneChanged( SceneHook& ){
	if ( g_window != nullptr )
		g_window->mapChanged();
}

void compileCommand(){
	LightPreview_show();
	g_window->requestCompile( true );
}

} // namespace

void LightPreview_show(){
	if ( g_window == nullptr )
		g_window = new PreviewWindow;
	g_window->showAndCompile();
}

void LightPreview_Construct(){
	g_profile = &profile_for_basegame( basegame_get() );
	settings_reset_to_profile();

	GlobalCommands_insert( "LightPreview", FreeCaller<LightPreview_show>() );
	GlobalCommands_insert( "LightPreviewCompile", FreeCaller<compileCommand>() );

	// keys must outlive registration: keep them in a static list
	static std::vector<std::string> keys;
	const std::string prefix = "LightPreview_" + g_profile->id + "_";
	const auto key = [&]( const std::string& name ) -> const char* {
		keys.reserve( 32 );
		keys.push_back( prefix + name );
		return keys.back().c_str();
	};
	GlobalPreferenceSystem().registerPreference( key( "Quality" ), IntImportStringCaller( g_settings.quality ), IntExportStringCaller( g_settings.quality ) );
	GlobalPreferenceSystem().registerPreference( key( "Auto" ), BoolImportStringCaller( g_settings.autoUpdate ), BoolExportStringCaller( g_settings.autoUpdate ) );
	GlobalPreferenceSystem().registerPreference( key( "Follow" ), BoolImportStringCaller( g_settings.followCamera ), BoolExportStringCaller( g_settings.followCamera ) );
	GlobalPreferenceSystem().registerPreference( key( "ViewMode" ), IntImportStringCaller( g_settings.viewMode ), IntExportStringCaller( g_settings.viewMode ) );
	for ( int i = 0; i < c_presetCount; ++i )
		GlobalPreferenceSystem().registerPreference( key( "Preset" + std::to_string( i ) ), CopiedStringImportStringCaller( g_settings.presetArgs[i] ), CopiedStringExportStringCaller( g_settings.presetArgs[i] ) );
	GlobalPreferenceSystem().registerPreference( key( "BspArgs" ), CopiedStringImportStringCaller( g_settings.bspArgs ), CopiedStringExportStringCaller( g_settings.bspArgs ) );
	GlobalPreferenceSystem().registerPreference( key( "MapOverBrightBits" ), IntImportStringCaller( g_settings.mapOverBrightBits ), IntExportStringCaller( g_settings.mapOverBrightBits ) );
	GlobalPreferenceSystem().registerPreference( key( "OverBrightBits" ), IntImportStringCaller( g_settings.overBrightBits ), IntExportStringCaller( g_settings.overBrightBits ) );
	GlobalPreferenceSystem().registerPreference( key( "Gamma" ), FloatImportStringCaller( g_settings.gamma ), FloatExportStringCaller( g_settings.gamma ) );
	GlobalPreferenceSystem().registerPreference( key( "Intensity" ), FloatImportStringCaller( g_settings.intensity ), FloatExportStringCaller( g_settings.intensity ) );
	GlobalPreferenceSystem().registerPreference( key( "HardwareGamma" ), BoolImportStringCaller( g_settings.hardwareGamma ), BoolExportStringCaller( g_settings.hardwareGamma ) );

	AddSceneChangeCallback( ReferenceCaller<SceneHook, sceneChanged>( g_sceneHook ) );
}
