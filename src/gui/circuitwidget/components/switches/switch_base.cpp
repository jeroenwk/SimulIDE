/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QDebug>
#include <QGraphicsProxyWidget>

#include "switch_base.h"
#include "simulator.h"
#include "circuit.h"
#include "label.h"
#include "custombutton.h"
#include <QtMath>

SwitchBase::SwitchBase( QString type, QString id )
          : MechContact( type, id )
{
    m_graphical = true;
    m_changed = true;
    m_key = "";
    m_bounceTime = 0;
    m_bounces    = 5;
    m_target     = false;
    m_bounceLeft = 0;
    m_bounceN    = 0;
    m_started    = false;
    m_contact    = false;

    m_area =  QRectF( 0,0,0,0 );
    m_ePin.resize(2);
    m_idLabel->setPos(-12,-24);

    m_button = new CustomButton( );
    m_button->setMaximumSize( 16, 16 );
    m_button->setCheckable( true );

    m_proxy = Circuit::self()->addWidget( m_button );
    m_proxy->setParentItem( this );
    m_proxy->setPos(-20,-16 );
    m_proxy->setTransformOriginPoint( m_proxy->boundingRect().center() );
    m_keyEventConn = QObject::connect( Circuit::self(), &Circuit::keyEvent, [=](QString k, bool p){ keyEvent(k,p); } );
}
SwitchBase::~SwitchBase(){
    QObject::disconnect( m_keyEventConn );
}

void SwitchBase::setSwitch( bool closed )
{
    MechContact::setSwitch( closed );
    m_contact = closed;
}

void SwitchBase::initialize()
{
    m_bounceLeft = 0;
    m_started = false;
}

void SwitchBase::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    if( m_bounceLeft ) return;   // A burst is running: it ends in its own target state

    bool want = m_closed;
    bool bounce = m_started && m_bounceTime > 0 && want != m_contact
               && Simulator::self()->isRunning();
    m_started = true;

    setSwitch( want );           // First touch (or the clean change)
    m_changed = false;
    if( bounce ){
        m_target = want;
        m_bounceN = 0;
        m_bounceLeft = 2*m_bounces;
        nextBounce();
    }
    update();
}

// Gaps shrink by 0.7 each change (the contact's swing dies down) and add up to Bounce_Time
void SwitchBase::nextBounce()
{
    double r = 0.7, sum = ( 1-qPow( r, 2*m_bounces ) )/( 1-r );
    double gap = m_bounceTime*qPow( r, m_bounceN )/sum;      // Seconds
    uint64_t ps = (uint64_t)( gap*1e12 );
    if( ps < 1 ) ps = 1;
    Simulator::self()->addEvent( ps, this );
}

void SwitchBase::runEvent()
{
    if( !m_bounceLeft ) return;
    m_bounceLeft--;
    m_bounceN++;
    bool closed = m_bounceLeft ? ( (m_bounceN & 1) ? !m_target : m_target ) : m_target;
    setSwitch( closed );
    if( m_bounceLeft ) nextBounce();
}

void SwitchBase::setAngle( double angle )
{
    SwitchBase::rotateAngle( angle );
    Component::setAngle( angle );
}

void SwitchBase::rotateAngle( double a )
{
    Component::rotateAngle( a );
    rotateText(-a );
}

void SwitchBase::rotateText( double angle )
{
    double rot = m_proxy->rotation();
    m_proxy->setRotation( rot + angle );
}

/*void SwitchBase::setflip()
{
    Component::setflip();
    m_proxy->setPos( -8 + ( m_Hflip>0 ? 0 : 16 ), 4 + ( m_Vflip>0 ? 0 : 16 ) );
    m_proxy->setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );
}*/

void SwitchBase::setHidden( bool hide, bool hidArea, bool hidLabel )
{
    Component::setHidden( hide, hidArea, hidLabel );
    if  ( hidArea ) m_area = QRectF( 0, 0,-4,-4 );
    else if( hide ) m_area = QRectF(-6, 6, 12, 12 );
    else            m_area = QRectF(-13,-16*m_numPoles, 26, 16*m_numPoles );

    m_proxy->setFlag( QGraphicsItem::ItemStacksBehindParent, hide && !hidArea );
}

void SwitchBase::onbuttonclicked()
{
    m_closed = m_button->isChecked();

    if( m_nClose ) m_closed = !m_closed;
    m_changed = true;
    update();
}

void SwitchBase::setNClose( bool nc )
{
    m_nClose = nc;
    // Contact state follows the button (toggled/pressed) as well as the normal position
    bool closed = m_button->isChecked() != nc;
    qDebug() << "[SwitchBase::setNClose]" << m_id << "nc" << nc << "button" << m_button->isChecked() << "-> closed" << closed;
    setSwitch( closed );
    update();
}

void SwitchBase::setKey( QString key )
{
    if( key.size()>1 ) key = key.left( 1 );
    m_key = key;
    m_button->setText( key );
}
