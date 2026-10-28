/*
   Hotspot rectangle editor: Qt widgets only (no editor dependencies), so it can be tested standalone.
   The editor glue (texture loading, saving, applying) lives in hotspot_editor.cpp.

   This file is part of NetRadiant-custom and is distributed under the GPL v2 or later.
 */

#pragma once

#include "hotspot_core.h"

#include <functional>

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QImage>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QListWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QSignalBlocker>
#include <QMessageBox>

namespace hotspot
{

inline QColor mode_colour( Mode mode ){
	switch ( mode )
	{
	case Mode::TileH: return QColor( 255, 150, 40 );
	case Mode::TileV: return QColor( 230, 80, 230 );
	default: return QColor( 60, 200, 255 );
	}
}

/// Image view with rectangles: draw, select, move, resize, zoom and pan.
class Canvas : public QWidget
{
	enum Edge { None = 0, Left = 1, Right = 2, Top = 4, Bottom = 8, Move = 16 };

	QImage m_image;
	Definition* m_def = nullptr;
	int m_selected = -1;
	double m_zoom = 1;
	QPointF m_offset;      // widget position of image pixel (0,0)
	int m_snap = 1;

	int m_dragEdges = None;
	bool m_creating = false;
	bool m_panning = false;
	QPointF m_pressImage;  // image coords at press
	QPoint m_pressWidget;
	Rect m_pressRect{};
	QPointF m_pressOffset;

public:
	std::function<void()> onChanged;   // rect geometry/count changed
	std::function<void()> onSelected;  // selection changed

	Canvas( QWidget* parent = nullptr ) : QWidget( parent ){
		setMouseTracking( true );
		setFocusPolicy( Qt::StrongFocus );
		setMinimumSize( 300, 300 );
	}

	void setImage( const QImage& image ){
		m_image = image;
		m_selected = -1;
		fitView();
	}
	const QImage& image() const {
		return m_image;
	}
	void setDefinition( Definition* def ){
		m_def = def;
		m_selected = -1;
		update();
	}
	void setSnap( int snap ){
		m_snap = snap < 1 ? 1 : snap;
	}
	int selected() const {
		return m_selected;
	}
	void select( int index ){
		m_selected = ( m_def != nullptr && index >= 0 && index < int( m_def->rects.size() ) ) ? index : -1;
		update();
		if ( onSelected )
			onSelected();
	}

	void fitView(){
		if ( m_image.isNull() ) {
			update();
			return;
		}
		const double margin = 16;
		m_zoom = std::fmin( ( width() - 2 * margin ) / m_image.width(), ( height() - 2 * margin ) / m_image.height() );
		if ( !( m_zoom > 0 ) )
			m_zoom = 1;
		m_offset = QPointF( ( width() - m_image.width() * m_zoom ) / 2, ( height() - m_image.height() * m_zoom ) / 2 );
		update();
	}

private:
	QPointF toImage( const QPointF& w ) const {
		return ( w - m_offset ) / m_zoom;
	}
	QRectF toWidget( const Rect& r ) const {
		return QRectF( m_offset.x() + r.x * m_zoom, m_offset.y() + r.y * m_zoom, r.w * m_zoom, r.h * m_zoom );
	}
	double snap( double v ) const {
		return std::round( v / m_snap ) * m_snap;
	}
	double clampX( double v ) const {
		return std::fmax( 0.0, std::fmin( v, double( m_image.width() ) ) );
	}
	double clampY( double v ) const {
		return std::fmax( 0.0, std::fmin( v, double( m_image.height() ) ) );
	}

	int edgesAt( const QPointF& w, int index ) const {
		if ( m_def == nullptr || index < 0 )
			return None;
		const QRectF r = toWidget( m_def->rects[index] );
		const double g = 6;
		if ( !r.adjusted( -g, -g, g, g ).contains( w ) )
			return None;
		int e = None;
		if ( std::fabs( w.x() - r.left() ) <= g ) e |= Left;
		else if ( std::fabs( w.x() - r.right() ) <= g ) e |= Right;
		if ( std::fabs( w.y() - r.top() ) <= g ) e |= Top;
		else if ( std::fabs( w.y() - r.bottom() ) <= g ) e |= Bottom;
		if ( e == None && r.contains( w ) )
			e = Move;
		return e;
	}
	int rectAt( const QPointF& w ) const {
		if ( m_def == nullptr )
			return -1;
		// smallest rect under the cursor wins, so nested/overlapping rects stay reachable
		int best = -1;
		double bestArea = 1e30;
		for ( int i = 0; i < int( m_def->rects.size() ); ++i )
		{
			const QRectF r = toWidget( m_def->rects[i] );
			if ( r.contains( w ) && r.width() * r.height() < bestArea ) {
				best = i;
				bestArea = r.width() * r.height();
			}
		}
		return best;
	}
	static Qt::CursorShape cursorFor( int e ){
		if ( e == Move ) return Qt::SizeAllCursor;
		if ( e == ( Left | Top ) || e == ( Right | Bottom ) ) return Qt::SizeFDiagCursor;
		if ( e == ( Right | Top ) || e == ( Left | Bottom ) ) return Qt::SizeBDiagCursor;
		if ( e & ( Left | Right ) ) return Qt::SizeHorCursor;
		if ( e & ( Top | Bottom ) ) return Qt::SizeVerCursor;
		return Qt::CrossCursor;
	}
	void changed(){
		update();
		if ( onChanged )
			onChanged();
	}

protected:
	void resizeEvent( QResizeEvent* ) override {
		fitView();
	}

	void paintEvent( QPaintEvent* ) override {
		QPainter p( this );
		p.fillRect( rect(), QColor( 40, 40, 44 ) );
		if ( m_image.isNull() ) {
			p.setPen( QColor( 180, 180, 180 ) );
			p.drawText( rect(), Qt::AlignCenter, "No texture loaded.\nSelect one in the texture browser and press \"From browser\"." );
			return;
		}
		p.setRenderHint( QPainter::SmoothPixmapTransform, m_zoom < 1 );
		const QRectF imageRect( m_offset, QSizeF( m_image.width() * m_zoom, m_image.height() * m_zoom ) );
		p.drawImage( imageRect, m_image );
		p.setPen( QPen( QColor( 255, 255, 255, 60 ), 1 ) );
		p.drawRect( imageRect );
		if ( m_def == nullptr )
			return;

		QFont font = p.font();
		font.setBold( true );
		p.setFont( font );
		for ( int i = 0; i < int( m_def->rects.size() ); ++i )
		{
			const Rect& r = m_def->rects[i];
			const QColor c = mode_colour( r.mode );
			const QRectF w = toWidget( r );
			const bool sel = i == m_selected;
			QColor fill = c;
			fill.setAlpha( sel ? 70 : 35 );
			p.fillRect( w, fill );
			p.setPen( QPen( c, sel ? 3 : 1.5 ) );
			p.drawRect( w );
			if ( def_inset() > 0 ) {
				p.setPen( QPen( c, 1, Qt::DashLine ) );
				const double in = def_inset() * m_zoom;
				p.drawRect( w.adjusted( in, in, -in, -in ) );
			}
			if ( r.mode != Mode::Fit ) { // arrows showing the repeat direction
				p.setPen( QPen( c, 2 ) );
				const QPointF ctr = w.center();
				const double L = std::fmin( 30.0, ( r.mode == Mode::TileH ? w.width() : w.height() ) * 0.3 );
				const QPointF d = r.mode == Mode::TileH ? QPointF( L, 0 ) : QPointF( 0, L );
				const QPointF n = r.mode == Mode::TileH ? QPointF( 0, 6 ) : QPointF( 6, 0 );
				const QPointF a = ctr - d, b = ctr + d;
				const QPointF da = d / std::sqrt( d.x() * d.x() + d.y() * d.y() ) * 8;
				p.drawLine( a, b );
				p.drawLine( a, a + da + n ); p.drawLine( a, a + da - n );
				p.drawLine( b, b - da + n ); p.drawLine( b, b - da - n );
			}
			const QString label = QString( "%1 %2" ).arg( i + 1 ).arg( mode_name( r.mode ) );
			const QRectF textRect = p.fontMetrics().boundingRect( label ).adjusted( -3, -1, 3, 1 );
			const QPointF at = w.topLeft() + QPointF( 4, 4 );
			p.fillRect( textRect.translated( at - textRect.topLeft() ), QColor( 0, 0, 0, 170 ) );
			p.setPen( c );
			p.drawText( at + QPointF( 3, p.fontMetrics().ascent() + 1 ), label );
			if ( sel ) { // handles
				p.setPen( Qt::NoPen );
				p.setBrush( Qt::white );
				for ( const QPointF& h : { w.topLeft(), w.topRight(), w.bottomLeft(), w.bottomRight(),
				                           QPointF( w.center().x(), w.top() ), QPointF( w.center().x(), w.bottom() ),
				                           QPointF( w.left(), w.center().y() ), QPointF( w.right(), w.center().y() ) } )
					p.drawRect( QRectF( h - QPointF( 3, 3 ), QSizeF( 6, 6 ) ) );
				p.setBrush( Qt::NoBrush );
			}
		}
	}

	double def_inset() const {
		return m_def != nullptr ? m_def->inset : 0;
	}

	void mousePressEvent( QMouseEvent* e ) override {
		setFocus();
		m_pressWidget = e->pos();
		m_pressImage = toImage( e->pos() );
		if ( e->button() == Qt::MiddleButton || e->button() == Qt::RightButton
		  || ( e->button() == Qt::LeftButton && ( e->modifiers() & Qt::AltModifier ) ) ) {
			m_panning = true;
			m_pressOffset = m_offset;
			setCursor( Qt::ClosedHandCursor );
			return;
		}
		if ( e->button() != Qt::LeftButton || m_def == nullptr || m_image.isNull() )
			return;

		int edges = edgesAt( e->pos(), m_selected );
		if ( edges == None ) {
			const int hit = rectAt( e->pos() );
			if ( hit != m_selected )
				select( hit );
			edges = hit >= 0 ? Move : None;
		}
		if ( edges != None && !( e->modifiers() & Qt::ShiftModifier ) ) {
			m_dragEdges = edges;
			m_pressRect = m_def->rects[m_selected];
			return;
		}
		// start a new rect (Shift forces a new rect even on top of an existing one)
		Rect r{ clampX( snap( m_pressImage.x() ) ), clampY( snap( m_pressImage.y() ) ), 0, 0, Mode::Fit };
		m_def->rects.push_back( r );
		m_creating = true;
		select( int( m_def->rects.size() ) - 1 );
		m_pressRect = r;
	}

	void mouseMoveEvent( QMouseEvent* e ) override {
		if ( m_panning ) {
			m_offset = m_pressOffset + ( e->pos() - m_pressWidget );
			update();
			return;
		}
		const QPointF im = toImage( e->pos() );
		if ( m_creating && m_selected >= 0 ) {
			const double x0 = m_pressRect.x, y0 = m_pressRect.y;
			const double x1 = clampX( snap( im.x() ) ), y1 = clampY( snap( im.y() ) );
			Rect& r = m_def->rects[m_selected];
			r.x = std::fmin( x0, x1 ); r.w = std::fabs( x1 - x0 );
			r.y = std::fmin( y0, y1 ); r.h = std::fabs( y1 - y0 );
			changed();
			return;
		}
		if ( m_dragEdges != None && m_selected >= 0 ) {
			Rect& r = m_def->rects[m_selected];
			const Rect& o = m_pressRect;
			const double dx = im.x() - m_pressImage.x(), dy = im.y() - m_pressImage.y();
			if ( m_dragEdges == Move ) {
				r.x = std::fmax( 0.0, std::fmin( snap( o.x + dx ), m_image.width() - o.w ) );
				r.y = std::fmax( 0.0, std::fmin( snap( o.y + dy ), m_image.height() - o.h ) );
			}
			else{
				double l = o.x, t = o.y, rr = o.x + o.w, b = o.y + o.h;
				if ( m_dragEdges & Left ) l = clampX( snap( o.x + dx ) );
				if ( m_dragEdges & Right ) rr = clampX( snap( o.x + o.w + dx ) );
				if ( m_dragEdges & Top ) t = clampY( snap( o.y + dy ) );
				if ( m_dragEdges & Bottom ) b = clampY( snap( o.y + o.h + dy ) );
				r.x = std::fmin( l, rr ); r.w = std::fmax( 1.0, std::fabs( rr - l ) );
				r.y = std::fmin( t, b ); r.h = std::fmax( 1.0, std::fabs( b - t ) );
			}
			changed();
			return;
		}
		setCursor( cursorFor( edgesAt( e->pos(), m_selected ) != None ? edgesAt( e->pos(), m_selected ) : ( rectAt( e->pos() ) >= 0 ? int( Move ) : int( None ) ) ) );
	}

	void mouseReleaseEvent( QMouseEvent* ) override {
		if ( m_panning ) {
			m_panning = false;
			setCursor( Qt::CrossCursor );
			return;
		}
		if ( m_creating && m_selected >= 0 ) {
			const Rect& r = m_def->rects[m_selected];
			if ( r.w < 2 || r.h < 2 ) { // a click, not a drag: drop it
				m_def->rects.erase( m_def->rects.begin() + m_selected );
				m_creating = false;
				select( rectAt( QPointF( m_pressWidget ) ) );
				changed();
				return;
			}
		}
		m_creating = false;
		m_dragEdges = None;
	}

	void wheelEvent( QWheelEvent* e ) override {
		if ( m_image.isNull() )
			return;
		const QPointF pos = e->position();
		const QPointF before = toImage( pos );
		const double factor = e->angleDelta().y() > 0 ? 1.25 : 0.8;
		m_zoom = std::fmax( 0.05, std::fmin( 32.0, m_zoom * factor ) );
		m_offset = pos - before * m_zoom;
		update();
	}

	void keyPressEvent( QKeyEvent* e ) override {
		if ( m_def == nullptr || m_selected < 0 ) {
			if ( e->key() == Qt::Key_F )
				fitView();
			return;
		}
		Rect& r = m_def->rects[m_selected];
		const double step = ( e->modifiers() & Qt::ShiftModifier ) ? 8 * m_snap : m_snap;
		switch ( e->key() )
		{
		case Qt::Key_Delete:
		case Qt::Key_Backspace:
			m_def->rects.erase( m_def->rects.begin() + m_selected );
			select( -1 );
			changed();
			return;
		case Qt::Key_Left:  r.x = std::fmax( 0.0, r.x - step ); break;
		case Qt::Key_Right: r.x = std::fmin( m_image.width() - r.w, r.x + step ); break;
		case Qt::Key_Up:    r.y = std::fmax( 0.0, r.y - step ); break;
		case Qt::Key_Down:  r.y = std::fmin( m_image.height() - r.h, r.y + step ); break;
		case Qt::Key_1: r.mode = Mode::Fit; break;
		case Qt::Key_2: r.mode = Mode::TileH; break;
		case Qt::Key_3: r.mode = Mode::TileV; break;
		case Qt::Key_F: fitView(); return;
		case Qt::Key_Escape: select( -1 ); return;
		default: QWidget::keyPressEvent( e ); return;
		}
		changed();
	}
};

/// The whole editor: texture bar, canvas and side panel.
class EditorWidget : public QWidget
{
	Definition m_def;
	bool m_dirty = false;
	QString m_texture;

	Canvas* m_canvas;
	QLineEdit* m_name;
	QListWidget* m_list;
	QComboBox* m_mode;
	QSpinBox* m_x, *m_y, *m_w, *m_h;
	QSpinBox* m_inset;
	QSpinBox* m_snap;
	QLabel* m_status;
	QPushButton* m_save;

public:
	// hooks provided by the editor glue
	std::function<QString()> browserTexture;                          // texture selected in the texture browser
	std::function<bool( const QString&, QImage&, std::string& )> load; // image + .hotspot text (empty if none)
	std::function<bool( const QString&, const std::string&, QString& )> save; // writes text; returns path or error
	std::function<void()> apply;                                      // hotspot the current selection
	std::function<void()> applyGrouped;
	std::function<QString( const QString& )> savePath;                // where Save will write, for display

	EditorWidget( QWidget* parent = nullptr ) : QWidget( parent ){
		auto* root = new QVBoxLayout( this );

		{ // texture bar
			auto* bar = new QHBoxLayout;
			root->addLayout( bar );
			bar->addWidget( new QLabel( "Texture:" ) );
			m_name = new QLineEdit;
			m_name->setPlaceholderText( "textures/folder/name" );
			bar->addWidget( m_name, 1 );
			auto* fromBrowser = new QPushButton( "From browser" );
			fromBrowser->setToolTip( "Load the texture currently selected in the texture browser" );
			bar->addWidget( fromBrowser );
			auto* loadBtn = new QPushButton( "Load" );
			bar->addWidget( loadBtn );
			QObject::connect( fromBrowser, &QPushButton::clicked, [this](){
				if ( browserTexture )
					openTexture( browserTexture() );
			} );
			QObject::connect( loadBtn, &QPushButton::clicked, [this](){ openTexture( m_name->text() ); } );
			QObject::connect( m_name, &QLineEdit::returnPressed, [this](){ openTexture( m_name->text() ); } );
		}

		auto* splitter = new QSplitter;
		root->addWidget( splitter, 1 );
		m_canvas = new Canvas;
		m_canvas->setDefinition( &m_def );
		splitter->addWidget( m_canvas );

		auto* panel = new QWidget;
		panel->setFixedWidth( 300 );
		splitter->addWidget( panel );
		splitter->setStretchFactor( 0, 1 );
		splitter->setStretchFactor( 1, 0 );
		splitter->setChildrenCollapsible( false );
		auto* side = new QVBoxLayout( panel );
		side->setContentsMargins( 4, 0, 0, 0 );

		m_list = new QListWidget;
		m_list->setMinimumHeight( 130 );
		side->addWidget( new QLabel( "Rectangles" ) );
		side->addWidget( m_list, 1 );

		{
			auto* box = new QGroupBox( "Selected" );
			side->addWidget( box );
			auto* form = new QFormLayout( box );
			m_mode = new QComboBox;
			m_mode->addItem( "fit — stretch to face", int( Mode::Fit ) );
			m_mode->addItem( "tile_h — repeat horizontally", int( Mode::TileH ) );
			m_mode->addItem( "tile_v — repeat vertically", int( Mode::TileV ) );
			form->addRow( "Mode", m_mode );
			auto* grid = new QGridLayout;
			const auto spin = [](){ auto* s = new QSpinBox; s->setRange( 0, 16384 ); return s; };
			m_x = spin(); m_y = spin(); m_w = spin(); m_h = spin();
			grid->addWidget( new QLabel( "X" ), 0, 0 ); grid->addWidget( m_x, 0, 1 );
			grid->addWidget( new QLabel( "Y" ), 0, 2 ); grid->addWidget( m_y, 0, 3 );
			grid->addWidget( new QLabel( "W" ), 1, 0 ); grid->addWidget( m_w, 1, 1 );
			grid->addWidget( new QLabel( "H" ), 1, 2 ); grid->addWidget( m_h, 1, 3 );
			form->addRow( grid );
			auto* del = new QPushButton( "Delete" );
			form->addRow( del );
			QObject::connect( del, &QPushButton::clicked, [this](){
				const int i = m_canvas->selected();
				if ( i < 0 ) return;
				m_def.rects.erase( m_def.rects.begin() + i );
				m_canvas->select( -1 );
				changed();
			} );
			QObject::connect( m_mode, QOverload<int>::of( &QComboBox::currentIndexChanged ), [this]( int ){
				const int i = m_canvas->selected();
				if ( i < 0 ) return;
				m_def.rects[i].mode = Mode( m_mode->currentData().toInt() );
				changed();
			} );
			for ( QSpinBox* s : { m_x, m_y, m_w, m_h } )
				QObject::connect( s, QOverload<int>::of( &QSpinBox::valueChanged ), [this]( int ){
					const int i = m_canvas->selected();
					if ( i < 0 ) return;
					Rect& r = m_def.rects[i];
					r.x = m_x->value(); r.y = m_y->value();
					r.w = std::max( 1, m_w->value() ); r.h = std::max( 1, m_h->value() );
					changed();
				} );
		}
		{
			auto* box = new QGroupBox( "Sheet" );
			side->addWidget( box );
			auto* form = new QFormLayout( box );
			m_inset = new QSpinBox;
			m_inset->setRange( 0, 64 );
			m_inset->setSuffix( " px" );
			m_inset->setToolTip( "Shrink every rect by this much when texturing, to avoid bleeding from neighbouring cells" );
			form->addRow( "Inset", m_inset );
			m_snap = new QSpinBox;
			m_snap->setRange( 1, 256 );
			m_snap->setValue( 1 );
			m_snap->setSuffix( " px" );
			m_snap->setToolTip( "Snap rect edges to this pixel grid while drawing" );
			form->addRow( "Snap", m_snap );
			auto* detect = new QPushButton( "Auto-detect from gutters" );
			detect->setToolTip( "Replace all rects with cells found between dark gutter lines" );
			form->addRow( detect );
			auto* clear = new QPushButton( "Clear all" );
			form->addRow( clear );
			QObject::connect( m_inset, QOverload<int>::of( &QSpinBox::valueChanged ), [this]( int v ){ m_def.inset = v; changed(); } );
			QObject::connect( m_snap, QOverload<int>::of( &QSpinBox::valueChanged ), [this]( int v ){ m_canvas->setSnap( v ); } );
			QObject::connect( detect, &QPushButton::clicked, [this](){ autodetect(); } );
			QObject::connect( clear, &QPushButton::clicked, [this](){ m_def.rects.clear(); m_canvas->select( -1 ); changed(); } );
		}
		{
			m_save = new QPushButton( "Save" );
			m_save->setShortcut( QKeySequence( "Ctrl+S" ) );
			auto* applyBtn = new QPushButton( "Save + Apply" );
			applyBtn->setToolTip( "Save, then hotspot the faces selected in the map" );
			auto* applyGroupedBtn = new QPushButton( "Save + Apply grouped" );
			applyGroupedBtn->setToolTip( "Save, then hotspot the selected faces, treating parallel faces as one" );
			auto* row = new QHBoxLayout;
			row->addWidget( m_save );
			row->addWidget( applyBtn );
			side->addLayout( row );
			side->addWidget( applyGroupedBtn );
			QObject::connect( m_save, &QPushButton::clicked, [this](){ doSave(); } );
			QObject::connect( applyBtn, &QPushButton::clicked, [this](){ if ( doSave() && apply ) apply(); } );
			QObject::connect( applyGroupedBtn, &QPushButton::clicked, [this](){ if ( doSave() && applyGrouped ) applyGrouped(); } );
		}
		m_status = new QLabel;
		m_status->setWordWrap( true );
		m_status->setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Preferred );
		m_status->setTextInteractionFlags( Qt::TextSelectableByMouse );
		side->addWidget( m_status );
		auto* help = new QLabel( "Hover the image for controls." );
		help->setStyleSheet( "color: gray;" );
		side->addWidget( help );
		m_canvas->setToolTip( "Drag on empty space: draw a rect\n"
		                      "Drag a rect / its edges: move / resize\n"
		                      "Shift+drag: draw over an existing rect\n"
		                      "Wheel: zoom    Right/middle drag: pan    F: fit view\n"
		                      "Del: delete    Arrows: nudge (Shift = 8x)\n"
		                      "1 / 2 / 3: fit / tile_h / tile_v" );

		m_canvas->onChanged = [this](){ changed(); };
		m_canvas->onSelected = [this](){ syncSelection(); };
		QObject::connect( m_list, &QListWidget::currentRowChanged, [this]( int row ){
			if ( row != m_canvas->selected() )
				m_canvas->select( row );
		} );

		refreshList();
		syncSelection();
		updateStatus();
	}

	bool isDirty() const {
		return m_dirty;
	}
	Canvas* canvas(){
		return m_canvas;
	}
	Definition& definition(){
		return m_def;
	}

	void openTexture( const QString& name ){
		if ( name.trimmed().isEmpty() || !load )
			return;
		if ( m_dirty && !m_texture.isEmpty()
		  && QMessageBox::question( this, "Hotspot Editor", "Discard unsaved changes to " + m_texture + "?" ) != QMessageBox::Yes )
			return;
		QImage image;
		std::string text;
		if ( !load( name.trimmed(), image, text ) ) {
			m_status->setText( "Could not load texture \"" + name + "\"" );
			return;
		}
		m_texture = name.trimmed();
		m_name->setText( m_texture );
		m_def = Definition();
		if ( !text.empty() )
			parse( text.c_str(), m_def );
		m_def.width = image.width();
		m_def.height = image.height();
		m_canvas->setDefinition( &m_def );
		m_canvas->setImage( image );
		{
			const QSignalBlocker block( m_inset );
			m_inset->setValue( int( m_def.inset ) );
		}
		m_dirty = false;
		refreshList();
		syncSelection();
		updateStatus( text.empty() ? "No .hotspot file yet. Draw rects or use Auto-detect." : "Loaded existing rects." );
	}

	void autodetect(){
		const QImage& img = m_canvas->image();
		if ( img.isNull() )
			return;
		std::vector<unsigned char> luma( std::size_t( img.width() ) * img.height() );
		const QImage rgb = img.convertToFormat( QImage::Format_RGB32 );
		for ( int y = 0; y < rgb.height(); ++y )
		{
			const QRgb* line = reinterpret_cast<const QRgb*>( rgb.constScanLine( y ) );
			for ( int x = 0; x < rgb.width(); ++x )
				luma[std::size_t( y ) * rgb.width() + x] = (unsigned char)qGray( line[x] );
		}
		m_def.rects = hotspot::autodetect( luma.data(), img.width(), img.height() );
		m_canvas->select( -1 );
		changed();
		updateStatus( QString( "Auto-detect found %1 rect(s)." ).arg( m_def.rects.size() ) );
	}

private:
	bool doSave(){
		if ( m_texture.isEmpty() || !save )
			return false;
		QString result;
		if ( !save( m_texture, write( m_def ), result ) ) {
			updateStatus( "Save failed: " + result );
			return false;
		}
		m_dirty = false;
		updateStatus( "Saved " + result );
		return true;
	}

	void changed(){
		m_dirty = true;
		refreshList();
		syncSelection();
		updateStatus();
		m_canvas->update();
	}

	void refreshList(){
		const QSignalBlocker block( m_list );
		m_list->clear();
		for ( std::size_t i = 0; i < m_def.rects.size(); ++i )
		{
			const Rect& r = m_def.rects[i];
			auto* item = new QListWidgetItem( QString( "%1  %2   %3,%4  %5×%6" )
			                                  .arg( i + 1 ).arg( mode_name( r.mode ), -6 )
			                                  .arg( r.x ).arg( r.y ).arg( r.w ).arg( r.h ) );
			item->setForeground( mode_colour( r.mode ) );
			m_list->addItem( item );
		}
		m_list->setCurrentRow( m_canvas->selected() );
	}

	void syncSelection(){
		const int i = m_canvas->selected();
		{
			const QSignalBlocker block( m_list );
			m_list->setCurrentRow( i );
		}
		const bool has = i >= 0;
		for ( QWidget* w : std::initializer_list<QWidget*>{ m_mode, m_x, m_y, m_w, m_h } )
			w->setEnabled( has );
		if ( !has )
			return;
		const Rect& r = m_def.rects[i];
		const QSignalBlocker b0( m_mode ), b1( m_x ), b2( m_y ), b3( m_w ), b4( m_h );
		m_mode->setCurrentIndex( m_mode->findData( int( r.mode ) ) );
		m_x->setValue( int( r.x ) ); m_y->setValue( int( r.y ) );
		m_w->setValue( int( r.w ) ); m_h->setValue( int( r.h ) );
	}

	void updateStatus( const QString& message = QString() ){
		QString s;
		if ( !m_texture.isEmpty() ) {
			s = QString( "%1 × %2 px, %3 rect(s)%4" )
			    .arg( m_canvas->image().width() ).arg( m_canvas->image().height() )
			    .arg( m_def.rects.size() ).arg( m_dirty ? " — unsaved" : "" );
			if ( savePath )
				s += "\nFile: " + savePath( m_texture );
		}
		if ( !message.isEmpty() )
			s = message + ( s.isEmpty() ? "" : "\n" + s );
		m_status->setText( s );
		m_save->setEnabled( !m_texture.isEmpty() );
	}
};

} // namespace hotspot
