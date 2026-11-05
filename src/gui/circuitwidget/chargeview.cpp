/***************************************************************************
 *   Capacitor charge and inductor current display: gauges, colours, label *
 *   See specs/capacitor-charge.md and specs/inductor-current.md           *
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
#include "inductor.h"
#include "transformer.h"
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
    m_iref      = 0;
    m_irefSource = refNone;
    m_ipeak     = 0;
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
    m_ipeak = 0;
}

void ChargeView::clear()
{
    m_peak = 0;
    m_ipeak = 0;
    repaintCaps();
}

void ChargeView::repaintCaps()
{
    for( Component* comp : *Circuit::self()->compList() )
    {
        if     ( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) ) cap->chargeDisplayChanged();
        else if( Inductor*      ind = dynamic_cast<Inductor*>( comp ) )      ind->chargeDisplayChanged();
        else if( Transformer*   tr  = dynamic_cast<Transformer*>( comp ) )   tr->chargeDisplayChanged();
    }
}

QString ChargeView::refSourceStr( int s )
{
    switch( s ){
        case refOverride: return "override";
        case refSources:  return "sources";
        case refPeak:     return "peak";
        case refCapacitor: return "capacitor";
        case refPart:     return "part";
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

    // No source: the highest capacitor node voltage since the start. The initial voltages
    // count too: the peak is only sampled once per frame, and a run can start between frames
    for( Component* comp : *Circuit::self()->compList() )
        if( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) )
        {
            m_peak = qMax( m_peak, qAbs( cap->initVolt() ) );
            for( Pin* pin : cap->getPins() )
                if( pin && pin->isConnected() ) m_peak = qMax( m_peak, qAbs( pin->getVoltage() ) );
        }

    m_refSource = ( m_peak > 0 ) ? refPeak : refNone;
    return m_vref = m_peak;
}

double ChargeView::iref()
{
    double scale = Circuit::self()->currentScale();
    if( scale > 0 ) { m_irefSource = refOverride; return m_iref = scale; }

    if( Simulator::self() && Simulator::self()->isRunning() )
    {
        for( Component* comp : *Circuit::self()->compList() )
        {
            if( Inductor* ind = dynamic_cast<Inductor*>( comp ) )
            {
                if( !ind->ownCurrentRef() && ind->currentShown( false ) )   // Initial current too
                    m_ipeak = qMax( m_ipeak, qMax( qAbs( cleanAmps( ind->indCurr() ) ), qAbs( ind->initCurr() ) ) );
            }
            else if( Transformer* tr = dynamic_cast<Transformer*>( comp ) )
                if( !tr->ownCurrentRef() ) m_ipeak = qMax( m_ipeak, tr->maxCoilCurrent() );
        }
    }
    m_irefSource = ( m_ipeak > 0 ) ? refPeak : refNone;
    return m_iref = m_ipeak;
}

QColor ChargeView::scaleColor( double value, double ref )
{
    QColor zero = Theme::line();
    if( ref <= 0 ) return zero;
    double t = qBound(-1.0, value/ref, 1.0 );
    QColor end = Theme::animWire( t >= 0 );
    t = qAbs( t );
    return QColor( zero.red()  +( end.red()  -zero.red()   )*t,
                   zero.green()+( end.green()-zero.green() )*t,
                   zero.blue() +( end.blue() -zero.blue()  )*t );
}

QColor ChargeView::plateColor( double volt, double ref )
{
    if( ref <= 0 ) ref = m_vref;
    return scaleColor( volt, ref );
}

void ChargeView::updateStep()
{
    if( !m_enabled ) return;
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ) return;

    vref();
    iref();
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

QString ChargeView::ampStr( double a )
{
    double m = qAbs( a );
    if( m < 1e-9 ) return "0 A";             // Below what the display can mean
    QString unit = "A";
    double  val  = a;
    if( m < 1e-6 )      { val = a*1e9; unit = "nA"; }
    else if( m < 1e-3 ) { val = a*1e6; unit = "µA"; }
    else if( m < 1 )    { val = a*1e3; unit = "mA"; }
    else if( m >= 1e3 ) { val = a*1e-3; unit = "kA"; }
    return QString::number( val, 'g', 3 )+" "+unit;
}

QString ChargeView::pctStr( double level, bool zero )
{
    int pct = qRound( level*100 );
    if( zero ) return "0 %";
    if( pct < 1 ) return "<1 %";
    return QString::number( pct )+" %";
}
