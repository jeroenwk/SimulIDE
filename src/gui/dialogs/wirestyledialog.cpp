/***************************************************************************
 *   Wire colours dialog (specs/wire-colors.md)                            *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QPainter>
#include <QPixmap>
#include <QDebug>

#include "wirestyledialog.h"
#include "connector.h"
#include "circuit.h"
#include "theme.h"
#include "utils.h"

#define tr(str) simulideTr("WireStyleDialog",str)

static const QStringList styleProps = { "color", "bandColor", "bandWidth" };
static const int defaultOpacity = 50;            // % for a new band

bool WireStyleDialog::edit( QList<Connector*> wires, QWidget* parent )
{
    if( wires.isEmpty() ) return false;
    WireStyleDialog d( wires, parent );
    if( d.exec() == QDialog::Accepted )
    {
        QMap<QString,QString> props;             // The touched fields, as previewed
        for( QString p : d.m_touched ) props[p] = wires.first()->getPropStr( p );
        d.restore();                             // Back to the originals, so apply()
        apply( wires, props );                   // records them as the undo values
        return !props.isEmpty();
    }
    d.restore();
    return false;
}

void WireStyleDialog::apply( QList<Connector*> wires, QMap<QString,QString> props )
{
    Circuit* circ = Circuit::self();
    bool any = false;
    for( Connector* con : wires )
        for( auto it = props.begin(); it != props.end(); ++it )
        {
            QString old = con->getPropStr( it.key() );
            if( old == it.value() ) continue;
            if( !any ){ circ->beginCircuitBatch(); any = true; }
            circ->addCompChange( con->getUid(), it.key(), old );
            con->setPropStr( it.key(), it.value() );
        }
    if( any ) circ->endCircuitBatch();           // One undo step
    qDebug() << "[wires] style" << props << "on" << wires.size() << "wires" << ( any ? "" : "(unchanged)" );
}

WireStyleDialog::WireStyleDialog( QList<Connector*> wires, QWidget* parent )
               : QDialog( parent )
{
    m_wires = wires;
    for( QString p : styleProps )
        for( Connector* con : wires ) m_orig[p].append( con->getPropStr( p ) );

    Connector* first = wires.first();          // Several wires: the first one's values
    m_line  = first->lineColor();
    m_band  = first->bandColor();
    m_width = qRound( first->bandWidth() );
    m_opacity = m_band.isValid() ? qRound( m_band.alphaF()*100 ) : defaultOpacity;

    setWindowTitle( wires.size() > 1 ? tr("Wire colours (%1 wires)").arg( wires.size() )
                                     : tr("Wire colours") );

    QGridLayout* grid = new QGridLayout;
    int row = 0;

    // Line
    grid->addWidget( new QLabel( tr("Line colour") ), row, 0 );
    m_lineButton = new QPushButton;
    m_lineButton->setToolTip( tr("Choose the wire's colour") );
    connect( m_lineButton, &QPushButton::clicked, this, &WireStyleDialog::pickLine );
    grid->addWidget( m_lineButton, row, 1 );
    QPushButton* lineNone = new QPushButton( tr("None") );
    lineNone->setToolTip( tr("Back to the theme's wire colour") );
    connect( lineNone, &QPushButton::clicked, this, [this](){ setLine( QColor() ); } );
    grid->addWidget( lineNone, row++, 2 );

    // Band
    grid->addWidget( new QLabel( tr("Band colour") ), row, 0 );
    m_bandButton = new QPushButton;
    m_bandButton->setToolTip( tr("Choose the colour of the band behind the wire") );
    connect( m_bandButton, &QPushButton::clicked, this, &WireStyleDialog::pickBand );
    grid->addWidget( m_bandButton, row, 1 );
    QPushButton* bandNone = new QPushButton( tr("None") );
    bandNone->setToolTip( tr("No band") );
    connect( bandNone, &QPushButton::clicked, this, [this](){ setBand( QColor() ); } );
    grid->addWidget( bandNone, row++, 2 );

    grid->addWidget( new QLabel( tr("Band opacity") ), row, 0 );
    m_opacitySlider = new QSlider( Qt::Horizontal );
    m_opacitySlider->setRange( 0, 100 );
    m_opacitySlider->setValue( m_opacity );
    connect( m_opacitySlider, &QSlider::valueChanged, this, &WireStyleDialog::opacityMoved );
    grid->addWidget( m_opacitySlider, row, 1 );
    m_opacityLabel = new QLabel;
    m_opacityLabel->setMinimumWidth( 40 );
    grid->addWidget( m_opacityLabel, row++, 2 );

    grid->addWidget( new QLabel( tr("Band width") ), row, 0 );
    m_widthSlider = new QSlider( Qt::Horizontal );
    m_widthSlider->setRange( 2, 40 );
    m_widthSlider->setValue( m_width );
    connect( m_widthSlider, &QSlider::valueChanged, this, &WireStyleDialog::widthMoved );
    grid->addWidget( m_widthSlider, row, 1 );
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange( 2, 40 );
    m_widthSpin->setSuffix(" px");
    m_widthSpin->setValue( m_width );
    connect( m_widthSpin, QOverload<int>::of( &QSpinBox::valueChanged ), this, &WireStyleDialog::widthMoved );
    grid->addWidget( m_widthSpin, row++, 2 );
    grid->setColumnStretch( 1, 1 );

    QDialogButtonBox* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
    connect( buttons, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

    QVBoxLayout* layout = new QVBoxLayout( this );
    layout->addLayout( grid );
    if( wires.size() > 1 )
    {
        QLabel* note = new QLabel( tr("Shows the first wire's values; only what you change is applied to all.") );
        note->setWordWrap( true );
        layout->addWidget( note );
    }
    layout->addWidget( buttons );
    setMinimumWidth( 380 );
    refreshSwatches();
}

static QIcon swatch( QColor c, bool alpha )
{
    QPixmap pix( 36, 16 );
    pix.fill( Qt::transparent );
    QPainter p( &pix );
    if( alpha )                                  // A checkerboard shows the opacity
        for( int x=0; x<36; x+=4 ) for( int y=0; y<16; y+=4 )
            p.fillRect( x, y, 4, 4, ( (x+y)/4 )%2 ? QColor( 200, 200, 200 ) : Qt::white );
    p.fillRect( pix.rect(), c );
    p.setPen( QColor( 128, 128, 128 ) );
    p.drawRect( pix.rect().adjusted( 0, 0, -1, -1 ) );
    return QIcon( pix );
}

void WireStyleDialog::refreshSwatches()
{
    m_updating = true;
    if( m_line.isValid() ){
        m_lineButton->setIcon( swatch( m_line, false ) );
        m_lineButton->setText( m_line.name() );
    }else{
        m_lineButton->setIcon( swatch( Theme::wire(), false ) );
        m_lineButton->setText( tr("theme") );
    }
    if( m_band.isValid() ){
        m_bandButton->setIcon( swatch( m_band, true ) );
        m_bandButton->setText( QColor( m_band.rgb() ).name() );
    }else{
        m_bandButton->setIcon( QIcon() );
        m_bandButton->setText( tr("none") );
    }
    m_opacitySlider->setValue( m_opacity );
    m_opacityLabel->setText( QString("%1 %").arg( m_opacity ) );
    m_widthSlider->setValue( m_width );
    m_widthSpin->setValue( m_width );
    m_updating = false;
}

// The colour picker previews while it is open; Cancel there goes back
void WireStyleDialog::pickLine()
{
    QColor before = m_line;
    QColorDialog d( m_line.isValid() ? m_line : Theme::wire(), this );
    d.setWindowTitle( tr("Line colour") );
    connect( &d, &QColorDialog::currentColorChanged, this, [this]( QColor c ){ setLine( c ); } );
    if( d.exec() == QDialog::Accepted ) setLine( d.selectedColor() );
    else                                setLine( before );
}

void WireStyleDialog::pickBand()
{
    QColor before = m_band;
    QColor start = m_band;
    if( !start.isValid() ){ start = QColor( 120, 100, 160 ); start.setAlpha( qRound( m_opacity*255/100.0 ) ); }
    QColorDialog d( start, this );
    d.setOption( QColorDialog::ShowAlphaChannel );
    d.setWindowTitle( tr("Band colour") );
    connect( &d, &QColorDialog::currentColorChanged, this, [this]( QColor c ){ setBand( c ); } );
    if( d.exec() == QDialog::Accepted ) setBand( d.selectedColor() );
    else                                setBand( before );
}

void WireStyleDialog::setLine( QColor c )
{
    m_line = c.isValid() ? QColor( c.rgb() ) : QColor();
    if( !m_touched.contains("color") ) m_touched.append("color");
    refreshSwatches();
    preview();
}

void WireStyleDialog::setBand( QColor c )
{
    m_band = c;
    if( m_band.isValid() ) m_opacity = qRound( m_band.alphaF()*100 );
    if( !m_touched.contains("bandColor") ) m_touched.append("bandColor");
    refreshSwatches();
    preview();
}

void WireStyleDialog::opacityMoved( int pct )
{
    if( m_updating ) return;
    m_opacity = pct;
    if( m_band.isValid() ) m_band.setAlpha( qRound( pct*255/100.0 ) );
    else{                                        // Opacity without a band: a band appears
        m_band = QColor( 120, 100, 160 );
        m_band.setAlpha( qRound( pct*255/100.0 ) );
    }
    if( !m_touched.contains("bandColor") ) m_touched.append("bandColor");
    refreshSwatches();
    preview();
}

void WireStyleDialog::widthMoved( int w )
{
    if( m_updating ) return;
    m_width = w;
    if( !m_touched.contains("bandWidth") ) m_touched.append("bandWidth");
    refreshSwatches();
    preview();
}

void WireStyleDialog::preview()
{
    for( Connector* con : m_wires )
    {
        if( m_touched.contains("color") )
            con->setPropStr( "color", m_line.isValid() ? m_line.name() : "" );
        if( m_touched.contains("bandWidth") )
            con->setPropStr( "bandWidth", m_width == qRound( Connector::defaultBandWidth ) ? "" : QString::number( m_width ) );
        if( m_touched.contains("bandColor") )
            con->setPropStr( "bandColor", m_band.isValid() ? m_band.name( QColor::HexArgb ) : "" );
    }
}

void WireStyleDialog::restore()
{
    for( QString p : styleProps )
        for( int i=0; i<m_wires.size(); ++i ) m_wires[i]->setPropStr( p, m_orig[p].at( i ) );
}
