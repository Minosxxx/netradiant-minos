/*
   Light preview: OpenGL view of a compiled map. See lightpreview_view.h.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#include "lightpreview_view.h"

#include <algorithm>
#include <cmath>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QGuiApplication>
#include <cstddef>

namespace
{
const float c_pi = 3.14159265358979f; // the M_ PI macro is missing on some compilers in strict C++ mode
}
#include <tuple>

namespace lightpreview
{

namespace
{

const char* const g_vertexShader = R"(
#version 120
attribute vec3 a_pos;
attribute vec2 a_st;
attribute vec2 a_lm;
attribute vec4 a_color;
uniform mat4 u_mvp;
varying vec2 v_st;
varying vec2 v_lm;
varying vec4 v_color;
varying vec3 v_pos;
void main(){
	v_pos = a_pos;
	v_st = a_st;
	v_lm = a_lm;
	v_color = a_color;
	gl_Position = u_mvp * vec4( a_pos, 1.0 );
}
)";

// Reproduces the id tech 3 pipeline with 8-bit framebuffer steps:
//   stage 1 writes the lightmap (or vertex colour) to the framebuffer,
//   stage 2 multiplies the texture in (blendFunc GL_DST_COLOR GL_ZERO),
//   the hardware gamma ramp then applies r_gamma and the overbright shift.
const char* const g_fragmentShader = R"(
#version 120
uniform sampler2D u_tex;
uniform sampler2D u_lightmap;
uniform int u_source;          // 0 lightmap, 1 vertex colour, 2 identity (unlit), 3 sky box (unlit)
uniform float u_identity;      // tr.identityLight
uniform bool u_whiteTexture;   // lighting-only view
uniform bool u_alphaTest;
uniform float u_alpha;
uniform float u_gamma;         // 1 when gamma is not done by the display ramp
uniform float u_overbright;    // 2^overbrightBits of the display ramp
varying vec2 v_st;
varying vec2 v_lm;
varying vec4 v_color;
varying vec3 v_pos;
uniform vec3 u_eye;
uniform sampler2D u_sky0, u_sky1, u_sky2, u_sky3, u_sky4, u_sky5; // rt lf bk ft up dn
// inverse of MakeSkyVec() in tr_sky.c: which box side a view direction hits, and where
vec4 skybox( vec3 d ){
	vec3 a = abs( d );
	float s, t;
	int face;
	if ( a.x >= a.y && a.x >= a.z ) {
		if ( d.x > 0.0 ) { face = 0; s = -d.y / a.x; t = d.z / a.x; }
		else             { face = 1; s =  d.y / a.x; t = d.z / a.x; }
	}
	else if ( a.y >= a.z ) {
		if ( d.y > 0.0 ) { face = 2; s =  d.x / a.y; t = d.z / a.y; }
		else             { face = 3; s = -d.x / a.y; t = d.z / a.y; }
	}
	else{
		if ( d.z > 0.0 ) { face = 4; s = -d.y / a.z; t = -d.x / a.z; }
		else             { face = 5; s = -d.y / a.z; t =  d.x / a.z; }
	}
	vec2 st = vec2( ( s + 1.0 ) * 0.5, 1.0 - ( t + 1.0 ) * 0.5 );
	if ( face == 0 ) return texture2D( u_sky0, st );
	if ( face == 1 ) return texture2D( u_sky1, st );
	if ( face == 2 ) return texture2D( u_sky2, st );
	if ( face == 3 ) return texture2D( u_sky3, st );
	if ( face == 4 ) return texture2D( u_sky4, st );
	return texture2D( u_sky5, st );
}
vec3 q( vec3 c ){ return floor( clamp( c, 0.0, 1.0 ) * 255.0 + 0.5 ) / 255.0; }
void main(){
	vec4 t = u_source == 3 ? skybox( v_pos - u_eye ) : u_whiteTexture ? vec4( 1.0 ) : texture2D( u_tex, v_st );
	if ( u_alphaTest && t.a < 0.5 )
		discard;
	vec3 light;
	if ( u_source == 0 )
		light = texture2D( u_lightmap, v_lm ).rgb;
	else if ( u_source == 1 )
		light = v_color.rgb;
	else
		light = vec3( u_identity );
	vec3 fb = q( q( light ) * t.rgb );
	vec3 g = ( u_gamma == 1.0 ) ? fb : floor( 255.0 * pow( fb, vec3( 1.0 / u_gamma ) ) + 0.5 ) / 255.0;
	gl_FragColor = vec4( min( vec3( 1.0 ), g * u_overbright ), t.a * u_alpha );
}
)";

struct GpuVertex
{
	float xyz[3];
	float st[2];
	float lm[2];
	uint8_t color[4];
};

QImage checker_image(){
	QImage img( 64, 64, QImage::Format_RGBA8888 );
	for ( int y = 0; y < 64; ++y )
		for ( int x = 0; x < 64; ++x )
		{
			const int v = ( ( x / 16 + y / 16 ) & 1 ) ? 150 : 110;
			img.setPixel( x, y, qRgba( v, v, v, 255 ) );
		}
	return img;
}

} // namespace

View::View( QWidget* parent ) : QOpenGLWidget( parent ){
	setFocusPolicy( Qt::StrongFocus );
	setMinimumSize( 320, 240 );
	m_moveTimer.setInterval( 16 );
	QObject::connect( &m_moveTimer, &QTimer::timeout, [this](){ tick(); } );
}

View::~View(){
	makeCurrent();
	freeGpu();
	delete m_program;
	if ( m_glReady ) {
		glDeleteTextures( 1, &m_white );
		glDeleteTextures( 1, &m_checker );
	}
	doneCurrent();
}

void View::setMap( const Bsp& bsp ){
	m_bsp = bsp;
	for ( GpuMaterial& m : m_materials )
		releaseMaterial( m );
	m_materials.clear();
	m_materials.resize( m_bsp.shaders.size() );
	for ( std::size_t i = 0; i < m_bsp.shaders.size(); ++i )
		if ( materialFor )
			m_materials[i].material = materialFor( m_bsp.shaders[i].name );
	m_mapDirty = m_texturesDirty = m_lightmapsDirty = true;
	update();
}

void View::reloadMaterials(){
	for ( std::size_t i = 0; i < m_materials.size() && i < m_bsp.shaders.size(); ++i )
		if ( materialFor )
			m_materials[i].material = materialFor( m_bsp.shaders[i].name );
	m_texturesDirty = true;
	update();
}

void View::clearMap(){
	m_bsp = Bsp();
	for ( GpuMaterial& m : m_materials )
		releaseMaterial( m );
	m_materials.clear();
	m_mapDirty = m_texturesDirty = m_lightmapsDirty = true;
	update();
}

void View::setEngine( const EngineParams& params ){
	m_engine.p = params;
	m_texturesDirty = m_lightmapsDirty = m_mapDirty = true; // texture scale, lightmap shift and vertex colours depend on it
	update();
}

void View::setMode( ViewMode mode ){
	m_mode = mode;
	update();
}

void View::setCamera( const CameraState& camera ){
	m_camera = camera;
	update();
}

void View::setOverlayText( const QString& text ){
	m_overlay = text;
	update();
}

void View::initializeGL(){
	initializeOpenGLFunctions();
	m_program = new QOpenGLShaderProgram;
	m_program->addShaderFromSourceCode( QOpenGLShader::Vertex, g_vertexShader );
	m_program->addShaderFromSourceCode( QOpenGLShader::Fragment, g_fragmentShader );
	m_program->bindAttributeLocation( "a_pos", 0 );
	m_program->bindAttributeLocation( "a_st", 1 );
	m_program->bindAttributeLocation( "a_lm", 2 );
	m_program->bindAttributeLocation( "a_color", 3 );
	m_program->link();

	QImage white( 1, 1, QImage::Format_RGBA8888 );
	white.fill( Qt::white );
	m_white = uploadImage( white, false, false );
	m_checker = uploadImage( checker_image(), true, false );
	m_glReady = true;
}

void View::resizeGL( int, int ){
}

GLuint View::uploadImage( const QImage& source, bool mipmaps, bool engineScale ){
	QImage image = source.convertToFormat( QImage::Format_RGBA8888 );
	if ( engineScale ) {
		for ( int y = 0; y < image.height(); ++y )
		{
			uchar* line = image.scanLine( y );
			for ( int x = 0; x < image.width(); ++x )
				for ( int c = 0; c < 3; ++c )
					line[x * 4 + c] = m_engine.textureByte( line[x * 4 + c] );
		}
	}
	GLuint tex = 0;
	glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_2D, tex );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits() );
	if ( mipmaps ) {
		glGenerateMipmap( GL_TEXTURE_2D );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST ); // Q3 default r_textureMode
	}
	else{
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	}
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mipmaps ? GL_REPEAT : GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mipmaps ? GL_REPEAT : GL_CLAMP_TO_EDGE );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 ); // GL default; Qt's glyph cache relies on it
	glBindTexture( GL_TEXTURE_2D, 0 );
	return tex;
}

void View::freeGpu(){
	if ( !m_glReady )
		return;
	if ( m_vbo.isCreated() ) m_vbo.destroy();
	if ( m_ibo.isCreated() ) m_ibo.destroy();
	m_indexCount = 0;
	m_batches.clear();
	if ( !m_lightmapTextures.empty() )
		glDeleteTextures( GLsizei( m_lightmapTextures.size() ), m_lightmapTextures.data() );
	m_lightmapTextures.clear();
	for ( GpuMaterial& m : m_materials )
		releaseMaterial( m );
	deleteOrphans();
}

void View::releaseMaterial( GpuMaterial& m ){
	if ( m.texture != 0 )
		m_orphanTextures.push_back( m.texture );
	m.texture = 0;
	for ( GLuint& t : m.sky )
		if ( t != 0 ) {
			m_orphanTextures.push_back( t );
			t = 0;
		}
}

void View::deleteOrphans(){
	if ( !m_orphanTextures.empty() && m_glReady )
		glDeleteTextures( GLsizei( m_orphanTextures.size() ), m_orphanTextures.data() );
	m_orphanTextures.clear();
}

void View::uploadMap(){
	if ( m_vbo.isCreated() ) m_vbo.destroy();
	if ( m_ibo.isCreated() ) m_ibo.destroy();
	m_indexCount = 0;
	m_batches.clear();
	if ( m_bsp.surfaces.empty() )
		return;

	std::vector<GpuVertex> verts( m_bsp.vertices.size() );
	for ( std::size_t i = 0; i < verts.size(); ++i )
	{
		const Vertex& v = m_bsp.vertices[i];
		GpuVertex& g = verts[i];
		std::copy( v.xyz, v.xyz + 3, g.xyz );
		std::copy( v.st, v.st + 2, g.st );
		std::copy( v.lm, v.lm + 2, g.lm );
		m_engine.shiftLightingBytes( v.color, g.color ); // R_ColorShiftLightingBytes on vertex colours
		g.color[3] = v.color[3];
	}

	// draw order: opaque, then translucent; within that grouped by shader and lightmap
	std::vector<int> order( m_bsp.surfaces.size() );
	for ( std::size_t i = 0; i < order.size(); ++i ) order[i] = int( i );
	const auto key = [this]( int i ){
		const Surface& s = m_bsp.surfaces[i];
		const Material& m = m_materials[s.shader].material;
		return std::make_tuple( m.translucent ? 1 : 0, s.shader, s.lightmap );
	};
	std::stable_sort( order.begin(), order.end(), [&]( int a, int b ){ return key( a ) < key( b ); } );

	std::vector<uint32_t> indexes;
	indexes.reserve( m_bsp.indexes.size() );
	for ( int i : order )
	{
		const Surface& s = m_bsp.surfaces[i];
		if ( m_materials[s.shader].material.invisible )
			continue;
		const int first = int( indexes.size() );
		indexes.insert( indexes.end(), m_bsp.indexes.begin() + s.firstIndex, m_bsp.indexes.begin() + s.firstIndex + s.numIndexes );
		if ( !m_batches.empty() && m_batches.back().shader == s.shader && m_batches.back().lightmap == s.lightmap )
			m_batches.back().numIndexes += s.numIndexes;
		else
			m_batches.push_back( { s.shader, s.lightmap, first, s.numIndexes } );
	}

	m_vbo.create();
	m_vbo.bind();
	m_vbo.allocate( verts.data(), int( verts.size() * sizeof( GpuVertex ) ) );
	m_ibo.create();
	m_ibo.bind();
	m_ibo.allocate( indexes.data(), int( indexes.size() * sizeof( uint32_t ) ) );
	m_indexCount = int( indexes.size() );
}

void View::uploadLightmaps(){
	if ( !m_lightmapTextures.empty() )
		glDeleteTextures( GLsizei( m_lightmapTextures.size() ), m_lightmapTextures.data() );
	m_lightmapTextures.clear();
	std::vector<uint8_t> rgb;
	for ( const LightmapPage& page : m_bsp.lightmaps )
	{
		m_engine.prepareLightmap( page.rgb.data(), std::size_t( page.width ) * page.height, rgb );
		QImage image( page.width, page.height, QImage::Format_RGBA8888 );
		for ( int y = 0; y < page.height; ++y )
		{
			uchar* line = image.scanLine( y );
			for ( int x = 0; x < page.width; ++x )
			{
				const uint8_t* p = &rgb[( std::size_t( y ) * page.width + x ) * 3];
				line[x * 4 + 0] = p[0]; line[x * 4 + 1] = p[1]; line[x * 4 + 2] = p[2]; line[x * 4 + 3] = 255;
			}
		}
		m_lightmapTextures.push_back( uploadImage( image, false, false ) );
	}
}

void View::uploadTextures(){
	for ( GpuMaterial& m : m_materials )
	{
		releaseMaterial( m );
		m.texture = m.material.image.isNull() ? 0 : uploadImage( m.material.image, true, true );
		for ( int i = 0; i < 6; ++i ) // sky boxes: clamped, no mipmaps
			m.sky[i] = m.material.skyBox[i].isNull() ? 0 : uploadImage( m.material.skyBox[i], false, true );
	}
}

void View::paintGL(){
	QPainter painter( this );
	painter.beginNativePainting(); // Qt restores its own GL state afterwards (needed for the text overlay)
	if ( m_mapDirty ) {
		uploadMap();
		m_mapDirty = false;
	}
	if ( m_lightmapsDirty ) {
		uploadLightmaps();
		m_lightmapsDirty = false;
	}
	deleteOrphans();
	if ( m_texturesDirty ) {
		uploadTextures();
		m_texturesDirty = false;
	}

	glViewport( 0, 0, int( width() * devicePixelRatioF() ), int( height() * devicePixelRatioF() ) );
	glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if ( m_indexCount != 0 && m_program->isLinked() ) {
		const float aspect = float( std::max( 1, width() ) ) / float( std::max( 1, height() ) );
		const float vfov = 2.0f * std::atan( std::tan( m_camera.fov * c_pi / 360.0f ) / aspect ) * 180.0f / c_pi;
		QMatrix4x4 proj;
		proj.perspective( vfov, aspect, 4.0f, 65536.0f );
		const float yaw = m_camera.yaw * c_pi / 180.0f, pitch = m_camera.pitch * c_pi / 180.0f;
		const QVector3D eye( m_camera.origin[0], m_camera.origin[1], m_camera.origin[2] );
		const QVector3D forward( std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ) );
		QMatrix4x4 view;
		view.lookAt( eye, eye + forward, QVector3D( 0, 0, 1 ) );

		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_LEQUAL );
		glEnable( GL_CULL_FACE );
		glCullFace( GL_FRONT ); // Q3 surfaces are wound clockwise
		glFrontFace( GL_CCW );

		m_program->bind();
		m_program->setUniformValue( "u_mvp", proj * view );
		m_program->setUniformValue( "u_tex", 0 );
		m_program->setUniformValue( "u_lightmap", 1 );
		const int obb = m_engine.effectiveOverbrightBits();
		m_program->setUniformValue( "u_identity", 1.0f / float( 1 << obb ) );
		m_program->setUniformValue( "u_gamma", m_engine.p.hardwareGamma ? m_engine.clampedGamma() : 1.0f );
		m_program->setUniformValue( "u_overbright", float( 1 << obb ) );
		m_program->setUniformValue( "u_whiteTexture", m_mode == ViewMode::LightingOnly );
		m_program->setUniformValue( "u_eye", eye );
		for ( int i = 0; i < 6; ++i )
			m_program->setUniformValue( QString( "u_sky%1" ).arg( i ).toLatin1().constData(), 2 + i );

		m_vbo.bind();
		m_ibo.bind();
		for ( int a = 0; a < 4; ++a ) m_program->enableAttributeArray( a );
		m_program->setAttributeBuffer( 0, GL_FLOAT, offsetof( GpuVertex, xyz ), 3, sizeof( GpuVertex ) );
		m_program->setAttributeBuffer( 1, GL_FLOAT, offsetof( GpuVertex, st ), 2, sizeof( GpuVertex ) );
		m_program->setAttributeBuffer( 2, GL_FLOAT, offsetof( GpuVertex, lm ), 2, sizeof( GpuVertex ) );
		glVertexAttribPointer( 3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof( GpuVertex ), reinterpret_cast<const void*>( offsetof( GpuVertex, color ) ) );

		bool blending = false;
		for ( const Batch& b : m_batches )
		{
			const GpuMaterial& gm = m_materials[b.shader];
			const Material& m = gm.material;
			if ( m.translucent && !blending ) {
				glEnable( GL_BLEND );
				glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
				glDepthMask( GL_FALSE );
				blending = true;
			}
			const bool skyBox = m.sky && gm.sky[0] != 0 && gm.sky[1] != 0 && gm.sky[2] != 0 && gm.sky[3] != 0 && gm.sky[4] != 0 && gm.sky[5] != 0;
			int source = 0;
			if ( skyBox )
				source = 3;
			else if ( m_mode == ViewMode::Fullbright || m.sky )
				source = 2;
			else if ( b.lightmap < 0 || b.lightmap >= int( m_lightmapTextures.size() ) )
				source = 1;
			m_program->setUniformValue( "u_source", source );
			m_program->setUniformValue( "u_alphaTest", m.alphaTest );
			m_program->setUniformValue( "u_alpha", m.translucent ? m.alpha : 1.0f );
			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, gm.texture != 0 ? gm.texture : m_checker );
			glActiveTexture( GL_TEXTURE1 );
			glBindTexture( GL_TEXTURE_2D, source == 0 ? m_lightmapTextures[b.lightmap] : m_white );
			if ( skyBox )
				for ( int i = 0; i < 6; ++i ) {
					glActiveTexture( GL_TEXTURE2 + i );
					glBindTexture( GL_TEXTURE_2D, gm.sky[i] );
				}
			glDrawElements( GL_TRIANGLES, b.numIndexes, GL_UNSIGNED_INT, reinterpret_cast<const void*>( std::size_t( b.firstIndex ) * sizeof( uint32_t ) ) );
		}
		glActiveTexture( GL_TEXTURE0 );
		for ( int a = 0; a < 4; ++a ) m_program->disableAttributeArray( a );
		m_vbo.release();
		m_ibo.release();
		m_program->release();
		glDisable( GL_BLEND );
		glDepthMask( GL_TRUE );
		glDisable( GL_CULL_FACE );
		glDisable( GL_DEPTH_TEST );
	}
	painter.endNativePainting();

	if ( !m_overlay.isEmpty() || m_indexCount == 0 ) {
		QPainter& p = painter;
		p.setPen( QColor( 230, 230, 230 ) );
		QFont f = p.font();
		f.setPointSizeF( f.pointSizeF() * 1.1 );
		p.setFont( f );
		const QString text = m_indexCount == 0 && m_overlay.isEmpty() ? QString( "No preview yet: press Compile" ) : m_overlay;
		const QRect r = p.fontMetrics().boundingRect( QRect( 0, 0, width() - 20, height() ), Qt::TextWordWrap, text ).adjusted( -6, -4, 6, 4 );
		const QRect placed = r.translated( 10 - r.left(), 10 - r.top() );
		p.fillRect( placed, QColor( 0, 0, 0, 160 ) );
		p.drawText( placed.adjusted( 6, 4, -6, -4 ), Qt::TextWordWrap, text );
	}
}

// ---- free-fly camera: right-drag to look, WASD / arrows, Q/E or Space/C for down/up, Shift faster

void View::mousePressEvent( QMouseEvent* e ){
	setFocus();
	if ( e->button() == Qt::RightButton || e->button() == Qt::LeftButton ) {
		m_looking = true;
		m_lastMouse = e->pos();
		setCursor( Qt::BlankCursor );
	}
}

void View::mouseMoveEvent( QMouseEvent* e ){
	if ( !m_looking )
		return;
	const QPoint d = e->pos() - m_lastMouse;
	m_lastMouse = e->pos();
	m_camera.yaw -= d.x() * 0.25f;
	m_camera.pitch = std::clamp( m_camera.pitch - d.y() * 0.25f, -89.0f, 89.0f );
	update();
	if ( onUserCamera )
		onUserCamera();
}

void View::mouseReleaseEvent( QMouseEvent* ){
	m_looking = false;
	unsetCursor();
}

void View::wheelEvent( QWheelEvent* e ){
	m_speed = std::clamp( m_speed * ( e->angleDelta().y() > 0 ? 1.25f : 0.8f ), 50.0f, 5000.0f );
}

void View::keyPressEvent( QKeyEvent* e ){
	if ( e->isAutoRepeat() )
		return;
	m_keys.insert( e->key() );
	if ( !m_moveTimer.isActive() ) {
		m_moveClock.start();
		m_moveTimer.start();
	}
}

void View::keyReleaseEvent( QKeyEvent* e ){
	if ( !e->isAutoRepeat() )
		m_keys.remove( e->key() );
}

void View::focusOutEvent( QFocusEvent* ){
	m_keys.clear();
	m_looking = false;
	unsetCursor();
}

void View::tick(){
	if ( m_keys.isEmpty() ) {
		m_moveTimer.stop();
		return;
	}
	const float dt = std::min( 0.1f, m_moveClock.restart() / 1000.0f );
	const float speed = m_speed * ( ( QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier ) ? 3.0f : 1.0f ) * dt;
	const float yaw = m_camera.yaw * c_pi / 180.0f, pitch = m_camera.pitch * c_pi / 180.0f;
	const float fwd[3] = { std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ) };
	const float right[3] = { std::sin( yaw ), -std::cos( yaw ), 0 };
	float move[3] = { 0, 0, 0 };
	const auto add = [&]( const float* v, float s ){ for ( int i = 0; i < 3; ++i ) move[i] += v[i] * s; };
	if ( m_keys.contains( Qt::Key_W ) || m_keys.contains( Qt::Key_Up ) ) add( fwd, 1 );
	if ( m_keys.contains( Qt::Key_S ) || m_keys.contains( Qt::Key_Down ) ) add( fwd, -1 );
	if ( m_keys.contains( Qt::Key_D ) || m_keys.contains( Qt::Key_Right ) ) add( right, 1 );
	if ( m_keys.contains( Qt::Key_A ) || m_keys.contains( Qt::Key_Left ) ) add( right, -1 );
	if ( m_keys.contains( Qt::Key_E ) || m_keys.contains( Qt::Key_Space ) ) move[2] += 1;
	if ( m_keys.contains( Qt::Key_Q ) || m_keys.contains( Qt::Key_C ) ) move[2] -= 1;
	if ( move[0] == 0 && move[1] == 0 && move[2] == 0 )
		return;
	for ( int i = 0; i < 3; ++i ) m_camera.origin[i] += move[i] * speed;
	update();
	if ( onUserCamera )
		onUserCamera();
}

} // namespace lightpreview
