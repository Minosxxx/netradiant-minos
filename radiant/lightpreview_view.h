/*
   Light preview: OpenGL view of a compiled map, drawn the way the game draws it.

   Lightmapped surfaces: lightmap (shifted like the engine does at load) x texture, then the
   engine's display gamma/overbright ramp. Vertex-lit surfaces use the shifted vertex colours.
   Qt + OpenGL only (no editor dependencies), so it can be tested standalone.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include "lightpreview_bsp.h"
#include "lightpreview_profile.h"

#include <functional>
#include <map>

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>

namespace lightpreview
{

enum class ViewMode { Lit, LightingOnly, Fullbright };

/// What the view needs to know about a shader to draw it.
struct Material
{
	QImage image;          // diffuse image (null = checker placeholder)
	bool sky = false;      // drawn unlit
	bool translucent = false;
	float alpha = 1.0f;
	bool alphaTest = false;
	bool invisible = false; // not drawn (fog, hint ...)
	QImage skyBox[6];       // skyparms far box, in tr_sky.c axis order: _rt _lf _bk _ft _up _dn
	bool hasSkyBox() const {
		for ( const QImage& i : skyBox )
			if ( i.isNull() )
				return false;
		return true;
	}
};

struct CameraState
{
	float origin[3] = { 0, 0, 64 };
	float yaw = 0, pitch = 0;  // degrees, Quake convention (pitch up positive)
	float fov = 90;            // horizontal, degrees
};

class View : public QOpenGLWidget, protected QOpenGLFunctions
{
public:
	View( QWidget* parent = nullptr );
	~View() override;

	/// Supplies materials for shader names; called when a map is set.
	std::function<Material( const std::string& shaderName )> materialFor;
	/// Called when the user moves the camera in the view itself (free-fly).
	std::function<void()> onUserCamera;

	void setMap( const Bsp& bsp );
	void clearMap();
	bool hasMap() const {
		return m_indexCount != 0;
	}
	void setEngine( const EngineParams& params );
	void setMode( ViewMode mode );
	void setCamera( const CameraState& camera );
	const CameraState& camera() const {
		return m_camera;
	}
	void setOverlayText( const QString& text ); // e.g. "outdated"
	void reloadMaterials();                     // look the shaders up again (textures changed on disk)

protected:
	void initializeGL() override;
	void resizeGL( int w, int h ) override;
	void paintGL() override;
	void mousePressEvent( QMouseEvent* e ) override;
	void mouseMoveEvent( QMouseEvent* e ) override;
	void mouseReleaseEvent( QMouseEvent* e ) override;
	void wheelEvent( QWheelEvent* e ) override;
	void keyPressEvent( QKeyEvent* e ) override;
	void keyReleaseEvent( QKeyEvent* e ) override;
	void focusOutEvent( QFocusEvent* e ) override;

private:
	struct Batch
	{
		int shader;
		int lightmap;
		int firstIndex;
		int numIndexes;
	};
	struct GpuMaterial
	{
		Material material;
		GLuint texture = 0;
		GLuint sky[6] = { 0, 0, 0, 0, 0, 0 };
	};

	void uploadMap();
	void uploadLightmaps();
	void uploadTextures();
	void freeGpu();
	void releaseMaterial( GpuMaterial& m ); // queues its textures for deletion
	void deleteOrphans();                   // needs a current context
	GLuint uploadImage( const QImage& image, bool mipmaps, bool engineScale );
	void tick();

	Bsp m_bsp;
	bool m_mapDirty = false;
	bool m_texturesDirty = false;
	bool m_lightmapsDirty = false;
	Quake3EngineModel m_engine;
	ViewMode m_mode = ViewMode::Lit;
	CameraState m_camera;
	QString m_overlay;

	bool m_glReady = false;
	QOpenGLShaderProgram* m_program = nullptr;
	QOpenGLBuffer m_vbo{ QOpenGLBuffer::VertexBuffer };
	QOpenGLBuffer m_ibo{ QOpenGLBuffer::IndexBuffer };
	int m_indexCount = 0;
	std::vector<Batch> m_batches;
	std::vector<GLuint> m_lightmapTextures;
	std::vector<GpuMaterial> m_materials;
	std::vector<GLuint> m_orphanTextures;
	GLuint m_white = 0;
	GLuint m_checker = 0;

	// free-fly
	QPoint m_lastMouse;
	bool m_looking = false;
	QSet<int> m_keys;
	QTimer m_moveTimer;
	QElapsedTimer m_moveClock;
	float m_speed = 400;
};

} // namespace lightpreview
