/***************************************************************************
 *   Capacitor charge display: gauge, plate colours, live label           *
 *   See specs/capacitor-charge.md                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <QSettings>
#include <QDateTime>

#include "chargeview.h"
#include "mainwindow.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "capacitorbase.h"
#include "subcircuit.h"
#include "theme.h"
#include "pin.h"

ChargeView* ChargeView::self()
{
    static ChargeView* view = new ChargeView();
    return view;
}

ChargeView::ChargeView()
          : Updatable()
{
    QSettings* s = MainWindow::self()->settings();
    m_enabled   = s->value("Circuit/capCharge", false ).toBool();
    m_labelMode = qBound( 0, s->value("Circuit/capLabel", 0 ).toInt(), 3 );
    m_blinkOn   = true;
    m_vref      = 0;
    m_refSource = refNone;
    m_peak      = 0;
}

void ChargeView::setEnabled( bool en )
{
    if( en == m_enabled ) return;
    m_enabled = en;
    CircuitWidget::self()->syncCapCharge( en );       // Toolbar button
    CircuitWidget::self()->syncEventLog();             // Messages checkbox
    MainWindow::self()->settings()->setValue("Circuit/capCharge", en );
    if( en ) attach();
    repaintCaps();
}

void ChargeView::setLabelMode( int mode )
{
    m_labelMode = qBound( 0, mode, 3 );
    MainWindow::self()->settings()->setValue("Circuit/capLabel", m_labelMode );
    repaintCaps();
}

bool ChargeView::active()
{
    if( !m_enabled ) return false;
    Simulator* sim = Simulator::self();
    return sim && sim->isRunning() && sim->circTime() > 1;
}

void ChargeView::attach()
{
    if( Simulator::self() ) Simulator::self()->addToUpdateList( this ); // No-op if listed
    m_peak = 0;
}

void ChargeView::clear()
{
    m_peak = 0;
    repaintCaps();
}

void ChargeView::repaintCaps()
{
    for( Component* comp : *Circuit::self()->compList() )
        if( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) ) cap->chargeDisplayChanged();
}

QString ChargeView::refSourceStr( int s )
{
    switch( s ){
        case refOverride: return "override";
        case refSources:  return "sources";
        case refPeak:     return "peak";
    }
    return "none";
}

void ChargeView::collectSources( QList<Component*>* list, double& max, int depth )
{
    for( Component* comp : *list )
    {
        double v = qAbs( comp->nominalVolt() );
        if( v > max ) max = v;
        if( depth < 8 )
            if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
                collectSources( sub->compList(), max, depth+1 );
    }
}

double ChargeView::sourcesMax()
{
    double max = 0;
    collectSources( Circuit::self()->compList(), max, 0 );
    return max;
}

double ChargeView::vref()
{
    double scale = Circuit::self()->chargeScale();
    if( scale > 0 ) { m_refSource = refOverride; return m_vref = scale; }

    double src = sourcesMax();
    if( src > 0 ) { m_refSource = refSources; return m_vref = src; }

    // No source: the highest capacitor node voltage since the start
    for( Component* comp : *Circuit::self()->compList() )
        if( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) )
            for( Pin* pin : cap->getPins() )
                if( pin && pin->isConnected() ) m_peak = qMax( m_peak, qAbs( pin->getVoltage() ) );

    m_refSource = ( m_peak > 0 ) ? refPeak : refNone;
    return m_vref = m_peak;
}

QColor ChargeView::plateColor( double volt )
{
    double ref = m_vref;
    QColor zero = Theme::line();
    if( ref <= 0 ) return zero;
    double t = qBound(-1.0, volt/ref, 1.0 );
    QColor end = Theme::animWire( t >= 0 );
    t = qAbs( t );
    return QColor( zero.red()  +( end.red()  -zero.red()   )*t,
                   zero.green()+( end.green()-zero.green() )*t,
                   zero.blue() +( end.blue() -zero.blue()  )*t );
}

void ChargeView::updateStep()
{
    if( !m_enabled ) return;
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ) return;

    vref();
    m_blinkOn = ( QDateTime::currentMSecsSinceEpoch()/250 ) % 2 == 0;
}

QString ChargeView::voltStr( double v )
{
    double a = qAbs( v );
    if( a < 1e-6 ) return "0 V";             // Below what the display can mean
    QString unit = "V";
    double  val  = v;
    if( a > 0 && a < 1e-3 ) { val = v*1e6; unit = "µV"; }
    else if( a > 0 && a < 1 ) { val = v*1e3; unit = "mV"; }
    else if( a >= 1e3 )     { val = v*1e-3; unit = "kV"; }
    return QString::number( val, 'g', 3 )+" "+unit;
}
