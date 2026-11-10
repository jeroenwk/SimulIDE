/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include <QDebug>
#include "transformer.h"
#include "itemlibrary.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "circuit.h"
#include "chargeview.h"
#include "pin.h"

#include "doubleprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Transformer",str)

Component* Transformer::construct( QString type, QString id )
{ return new Transformer( type, id ); }

LibraryItem* Transformer::libraryItem()
{
    return new LibraryItem(
        tr("Transformer"),
        "Reactive",
        "transformer.png",
        "Transformer",
        Transformer::construct );
}

Transformer::Transformer( QString type, QString id )
           : Component( type, id )
           , eElement( id )
{
    m_coupCoeff = 0.99;
    m_baseInd = 1;
    m_lenght = 0;
    m_currentScale = 0;

    m_primary.prefix = m_id+"-P";
    m_primary.angle = 180;
    m_primary.x     =-16;

    m_secondary.prefix = m_id+"-S";
    m_secondary.angle = 0;
    m_secondary.x     = 16;

    addPropGroup( { tr("Main"), {
        new DoubProp<Transformer>("CoupCoeff", tr("Coupling Coefficient"), ""
                                 , this, &Transformer::coupCoeff, &Transformer::setCoupCoeff ),

        new DoubProp<Transformer>("BaseInd"  , tr("Base Inductance"), "H"
                                 , this, &Transformer::baseInd, &Transformer::setBaseInd ),

        new StrProp <Transformer>("Primary"  , tr("Primary"), ""
                                 , this, &Transformer::primary, &Transformer::setPrimary, propNoCopy ),

        new StrProp <Transformer>("Secondary", tr("Secondary"), ""
                                 , this, &Transformer::secondary, &Transformer::setSecondary, propNoCopy ),

        new DoubProp<Transformer>("CurrentScale", tr("Current gauge 100 % at (0 = circuit)"), "A"
                                 , this, &Transformer::currentScale, &Transformer::setCurrentScale ),
    },0 } );

    setPrimary("1");
    setSecondary("1");

    Simulator::self()->addToUpdateList( this );
}
Transformer::~Transformer()
{
    for( CoilGauge* cg : m_coilGauges ) delete cg;   // The gauge items go with the part
}

void Transformer::stamp()
{
    m_coils.clear();

    setupInducts( &m_primary );
    setupInducts( &m_secondary );

    m_reacStep = Simulator::self()->reactStep();
    double tStep = (double)m_reacStep/1e12;

    int i, j, k;
    int n = m_coils.size();

    // Create admitance matrix
    double admit[n][n];
    for( int i=0; i<n; i++ ) admit[i][i] = m_coils.at(i)->inductance;
    for( int i=0; i<n; i++ )
    {
        eCoil* coili = m_coils.at(i);
        for( int j=0; j<i; ++j )
        {
            eCoil* coilj = m_coils.at(j);
            admit[i][j] = admit[j][i] = m_coupCoeff*qSqrt( coili->inductance*coilj->inductance )*coili->sign*coilj->sign;
        }
    }

    // Invert matrix
    for( j=0; j<n; ++j )
    {
        for( i=0; i<j; ++i )
        {
            double q = admit[i][j];
            for( k=0; k<i; ++k ) q -= admit[i][k]*admit[k][j];
            admit[i][j] = q;
        }
        for( i=j; i<n; ++i )
        {
            double q = admit[i][j];
            for( k=0; k<j; ++k ) q -= admit[i][k]*admit[k][j];
            admit[i][j] = q;
        }
        if( j == n-1 ) continue;
        for( i=j+1; i<n; ++i ) admit[i][j] /= admit[j][j];
    }
    double b[n];
    m_inva.resize( n, std::vector<double>( n, 0 ) );
    m_prevVolt.assign( n, 0 );            // BH-055: filled at the first update
    m_firstEvent = true;

    for( k=0; k<n; k++)
    {
        for( j=0; j<n; j++) b[j] = 0;
        b[k] = 1;

        double tot;
        for( i=k+1; i<n; ++i )
        {
            tot = b[i];
            for( int j=k; j<i; ++j ) tot -= admit[i][j]*b[j];
            b[i] = tot;
        }
        for( i=n-1; i>=0; --i )
        {
            tot = b[i];
            for( int j=i+1; j<n; ++j ) tot -= admit[i][j]*b[j];
            b[i] = tot/admit[i][i];
        }
        for( int i=0; i<n; i++ ) m_inva[i][k] = b[i]*tStep;
    }

    for( int i=0; i<n; i++ ) // Stamp coils before creating induced currents
    {
        m_coils.at(i)->setAdmit( m_inva[i][i] );
        m_coils.at(i)->stampCoil();
    }
    // Create induced current Sources
    for( int i=0; i<n; i++ )
    {
        eCoil* coili = m_coils.at(i);
        for( int j=0; j<n; j++ )
            if( i != j ) coili->addIductor( m_coils.at(j), m_inva[i][j] );
    }
    Simulator::self()->addEvent( m_reacStep, this );
}

void Transformer::runEvent()
{
    int n = m_coils.size();

    // BH-055: the accumulated current source of each coil must be built from the coil
    // voltages read at the *previous* update: the shunt admit inva[i][i] (= dt/L, as
    // the plain inductor's) already carries the current voltage during the interval, so
    // accumulating the freshly read voltages as well counts them twice whenever other
    // elements solved in between (a source, a switching diode...). That double count
    // showed up as a phantom series resistance of about w^2*L*reactStep (a 25 mH
    // winding: 95 ohm at 10 kHz with the default 1 us step) and damped/detuned
    // transformer circuits - the same bug the plain inductor had (BH-053). In a quiet
    // circuit (only transformer events) the read at each event is the voltage solved
    // after the previous one, so the old code was exact backward Euler there.
    if( m_firstEvent )   // Cache starts at the first post-stamp boundary: exact in a
    {                    // quiet circuit; in a busy one the first term errs by the
        m_firstEvent = false;   // drift over one reactive step (one-off, bounded).
        for( int i=0; i<n; i++ ) m_prevVolt.at( i ) = m_coils.at(i)->getVolt();
    }

    for( int i=0; i<n; i++ )
    {
        double current = 0;
        for( int j=0; j<n; ++j ) current += m_prevVolt.at( j )*m_inva[i][j];
        m_coils.at(i)->stampCurrent( current );
    }
    for( int i=0; i<n; i++ ) m_prevVolt.at( i ) = m_coils.at(i)->getVolt();
    Simulator::self()->addEvent( m_reacStep, this );
}

void Transformer::setupInducts( winding_t* w )  // Setup Inductances
{
    int pinIndex = -1;

    for( eCoil* coil : w->coils ) // connect coils to Pins
    {
        if( coil->index == 0 ) pinIndex++;

        coil->setEnode( 0, w->pins.at( pinIndex )->getEnode() );
        pinIndex++;
        coil->setEnode( 1, w->pins.at( pinIndex )->getEnode() );
        coil->setBaseInd( m_baseInd );

        m_coils.append( coil );
    }
}

void Transformer::setBaseInd( double i )
{
    if( i < 1e-9 ){ // Zero or negative inductance gave NaN / 1e192 V node voltages (BH-028)
        qDebug() << "[Transformer] setBaseInd: Base Inductance" << i << "is not positive, using 1e-9 H";
        i = 1e-9;
    }
    if( m_baseInd == i ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_baseInd = i;
}


void Transformer::setPrimary( QString p )
{
    if( m_primary.definition == p ) return;
    if( p.isEmpty() ) p = " ";
    m_primary.definition = p;
    createCoils();
}

void Transformer::setSecondary( QString s )
{
    if( m_secondary.definition == s ) return;
    if( s.isEmpty() ) s = " ";
    m_secondary.definition = s;
    createCoils();
}

void Transformer::setCoupCoeff( double c )
{
    if( m_coupCoeff == c ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    if( c > 0.9999 ) c = 0.9999;
    if( c < 0.0001 ) c = 0.0001;
    m_coupCoeff = c;
}

void Transformer::createCoils()
{
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();

    QString priTopo = m_primary.topology;
    QString secTopo = m_secondary.topology;

    int priLenght = getLeght( &m_primary );
    int secLenght = getLeght( &m_secondary );

    if( priLenght > secLenght ) m_lenght = priLenght;
    else                        m_lenght = secLenght;

    m_pin.clear();

    if( priTopo == m_primary.topology   ) modify( &m_primary );
    else                                  create( &m_primary );

    if( secTopo == m_secondary.topology ) modify( &m_secondary );
    else                                  create( &m_secondary );

    buildGauges();
}

//--- Current display (specs/inductor-current.md) ---------------------------

void Transformer::buildGauges()
{
    for( CoilGauge* cg : m_coilGauges ) { delete cg->gauge; delete cg; }
    m_coilGauges.clear();

    for( int i=0; i<2; ++i )             // Same walk as paint(): one gauge per coil
    {
        winding_t* w = i? &m_secondary : &m_primary;
        double x   = i? w->x+1 : w->x-1;  // Just outside the pin ends
        QPointF out( i? 1 : -1, 0 );
        int y = w->start-8;
        int sect = -1;
        for( eCoil* coil : w->coils )
        {
            if( coil->index == 0 ) { y += 8; sect++; }
            int h = coil->size*8;
            QString name = QString( i? "S":"P" )+QString::number( sect )+QString::number( coil->index+1 );
            CoilGauge* cg = new CoilGauge( this, coil, name );
            cg->gauge = new ChargeGauge( this, cg );
            cg->gauge->setAnchor( QPointF( x, y+h/2.0 ), out, qMin( 14, h-4 ) );
            cg->gauge->setVisible( false );
            m_coilGauges.append( cg );
            y += h;
        }
    }
}

void Transformer::setCurrentScale( double a )
{
    m_currentScale = ( a > 0 ) ? a : 0;
    if( currentShown() ) for( CoilGauge* cg : m_coilGauges ) cg->gauge->refresh();
    update();
}

double Transformer::currentRef()
{
    if( m_currentScale > 0 ) return m_currentScale;
    return ChargeView::self()->iref();
}

double Transformer::maxCoilCurrent()
{
    double max = 0;
    for( CoilGauge* cg : m_coilGauges ) max = qMax( max, qAbs( ChargeView::cleanAmps( cg->coil->current() ) ) );
    return max;
}

bool Transformer::currentShown() { return ChargeView::self()->active(); }

void Transformer::snapCurrents()
{
    for( CoilGauge* cg : m_coilGauges ) cg->shownCurr = ChargeView::cleanAmps( cg->coil->current() );
    m_shownRef = currentRef();
}

void Transformer::chargeDisplayChanged()
{
    bool shown = currentShown();
    if( shown ) snapCurrents();
    for( CoilGauge* cg : m_coilGauges )
    {
        if( shown ) cg->gauge->refresh();
        else        cg->gauge->setVisible( false );
    }
    update();
}

void Transformer::updateStep()
{
    if( currentShown() )
    {
        snapCurrents();                    // Colours of this frame
        for( CoilGauge* cg : m_coilGauges ) cg->gauge->refresh();
        update();                          // Coil colours
    }
    else for( CoilGauge* cg : m_coilGauges )
        if( cg->gauge->isVisible() ) { cg->gauge->setVisible( false ); update(); }
}

double CoilGauge::gaugeLevel()
{
    double ref = xfmr->currentRef();
    if( ref <= 0 ) return 0;
    return qAbs( ChargeView::cleanAmps( coil->current() ) )/ref;
}

QString CoilGauge::gaugeText( int mode )
{
    double i = coil->current();
    QString pct = ChargeView::pctStr( gaugeLevel(), ChargeView::cleanAmps( i ) == 0 );
    if     ( mode == ChargeView::labelVolt    ) return ChargeView::ampStr( i );
    else if( mode == ChargeView::labelPercent ) return pct;
    return ChargeView::ampStr( i )+" · "+pct;
}

int Transformer::getLeght( winding_t* w )
{
    QStringList sectsStr = w->definition.split(":");
    sectsStr.removeAll("");
    int nSections = sectsStr.size();

    w->topology.clear();
    int nCoils = 0;
    for( int s=0; s<nSections; ++s )
    {
        QString sectStr = sectsStr.at( s );
        if( s > 0 ) w->topology.append(":");
        QStringList coils = sectStr.remove(" ").split(",");
        coils.removeAll("");
        coils.removeAll("0");  // BH-021: create()/modify() skip "0" coils, so must the topology
        nCoils += coils.size();
        for( int c=0; c<coils.size(); ++c )
        {
            if( c > 0 ) w->topology.append(",");
            w->topology.append("c");
        }
    }
    w->nCoils = nCoils;
    qDebug() << "Transformer::getLeght: definition" << w->definition << "topology" << w->topology << "coils" << nCoils;
    if( !nSections ) return 0;
    return nCoils*3+nSections-1;
}

void Transformer::modify( winding_t* w )
{
    for( Pin* pin : w->pins ) m_pin.emplace_back( pin );

    QList<eCoil*>* coils = &w->coils;

    int nCoils = w->nCoils;

    QString defin = w->definition;
    QStringList sectsStr = defin.remove(" ").split(":");
    sectsStr.removeAll("");

    int nSections = sectsStr.size();
    if( nSections == 0 || nCoils == 0 ) return; // No coils

    int coilSize = (m_lenght-nSections+1)/nCoils;
    int y = ((m_lenght-(nCoils*coilSize+nSections-1)))*4; // Position of first Pin
    w->start = y;

    int c = 0;
    int p = 0;
    for( int sectN=0; sectN<nSections; ++sectN )
    {
        w->pins.at( p )->setY( y );
        p++;

        QString sectStr = sectsStr.at( sectN );
        QStringList coilsStr = sectStr.split(",");
        coilsStr.removeAll("");
        coilsStr.removeAll("0");

        for( int coilN=0; coilN<coilsStr.size(); ++coilN )
        {
            y += coilSize*8;
            w->pins.at( p )->setY( y );
            p++;

            double rel = coilsStr.at( coilN ).toDouble();
            int sign = rel>=0 ? 1:-1;

            eCoil* coil = coils->at( c );
            coil->sign = sign;
            coil->size = coilSize;
            coil->relation = rel;
            coil->setBaseInd( m_baseInd );
            c++;
        }
        y += 8;
    }
    int l = m_primary.nCoils   ? 12:4;
    int r = m_secondary.nCoils ? 12:4;
    m_area = QRect(-l, 0, l+r, 8*m_lenght );
    Circuit::self()->update();
}

void Transformer::create( winding_t* w  )
{
    QList<Pin*>*   pins  = &w->pins;
    QList<eCoil*>* coils = &w->coils;

    for( Pin* pin : *pins ) deletePin( pin );
    pins->clear();
    for( eCoil* coil : *coils ) delete coil;
    coils->clear();

    int nCoils = w->nCoils;

    QString defin = w->definition;
    QStringList sectsStr = defin.remove(" ").split(":");
    sectsStr.removeAll("");

    int nSections = sectsStr.size();
    if( nCoils == 0 || nSections == 0 ) return; // No coils

    int angle = w->angle;
    int x     = w->x;
    int coilSize = (m_lenght-nSections+1)/nCoils;
    int y = ((m_lenght-(nCoils*coilSize+nSections-1)))*4; // Position of first Pin
    w->start = y;

    for( int sectN=0; sectN<nSections; ++sectN )
    {
        QString baseId = w->prefix+QString::number (sectN );
        Pin* pin = new Pin( angle, QPoint( x, y ), baseId+"0Pin", 0, this );
        pin->setLength( 6 );
        pins->append( pin );
        m_pin.emplace_back( pin );

        QString sectStr = sectsStr.at( sectN );
        QStringList coilsStr = sectStr.split(",");
        coilsStr.removeAll("");
        coilsStr.removeAll("0");

        for( int coilN=0; coilN<coilsStr.size(); ++coilN )
        {
            y += coilSize*8;
            QString id = baseId+QString::number( coilN+1 );

            pin = new Pin( angle, QPoint( x, y ), id+"Pin", 0, this );
            pin->setLength( 6 );
            pins->append( pin );
            m_pin.emplace_back( pin );

            double rel = coilsStr.at( coilN ).toDouble();
            int sign = rel>=0 ? 1:-1;

            eCoil* coil = new eCoil( coilN, sign, coilSize, m_baseInd, rel, id+"Coil" );
            coils->push_back( coil );
        }
        y += 8;
    }
    int l = m_primary.nCoils   ? 12:4;
    int r = m_secondary.nCoils ? 12:4;
    m_area = QRect(-l, 0, l+r, 8*m_lenght );
    Circuit::self()->update();
}

void Transformer::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );

    QPen pen = p->pen();
    pen.setWidthF( 2 );
    p->setPen(pen);
    QColor base = pen.color();

    bool shown = currentShown();

    for( int i=0; i<2; ++i )
    {
        winding_t* w = i? &m_secondary : &m_primary;
        int offset = i? -4:4;
        int y = w->start-8;
        int x = w->x+offset;
        QList<eCoil*> coils = w->coils;

        for( int coilN=0; coilN<coils.size(); ++coilN )
        {
            eCoil* coil = coils.at( coilN );
            if( coil->index == 0 ) y += 8;

            if( shown )                    // Coloured by current: + from its first pin to its second
            {
                double cur = 0;            // Last frame
                for( CoilGauge* cg : m_coilGauges ) if( cg->coil == coil ) cur = cg->shownCurr;
                pen.setColor( ChargeView::scaleColor( cur, m_shownRef ) );
                p->setPen( pen );
            }

            if( coil->sign >= 0 )
                 p->drawLine( QPointF( x-0.01, y+4 ), QPointF( x+0.01, y+4 ) );
            else p->drawLine( QPointF( x-0.01, y+20), QPointF( x+0.01, y+20) );

            for( int n=0; n<coil->size; ++n ){
                if( i ) p->drawArc( QRectF(  6, y, 8, 8 ), 90*16,  180*16 );
                else    p->drawArc( QRectF(-14, y, 8, 8 ), 90*16, -180*16 );
                y += 8;
            }
        }
    }
    pen.setColor( base );                  // Core
    p->setPen( pen );
    p->drawLine(-2, 0,-2, 8*m_lenght );
    p->drawLine( 2, 0, 2, 8*m_lenght );

    pen.setBrush( Qt::red );
    pen.setWidthF( 2.8 );
    p->setPen( pen );
    p->drawLine( QPointF(-2, 0 ), QPointF(-2, 0.1) );

    Component::paintSelected( p );
}

// Coil structure, example section with 3 coils: (VVVV = resistor)
//
// ┏━VVVV━━⚫ Pin0 -- eNode
// ┃
// ┗━VVVV━━⚫ Pin1 -- eNode
//                      |
// ┏━VVVV--- ePin ------
// ┃
// ┗━VVVV━━⚫ Pin2 -- eNode
//                      |
// ┏━VVVV--- ePin ------
// ┃
// ┗━VVVV━━⚫ Pin3 -- eNode
