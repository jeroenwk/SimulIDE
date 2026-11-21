/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SWITCH_BASE_H
#define SWITCH_BASE_H

#include "mech_contact.h"

class CustomButton;
class QGraphicsProxyWidget;

class SwitchBase : public MechContact
{
    public:
        SwitchBase( QString type, QString id );
        ~SwitchBase();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( SwitchBase )

        virtual void initialize() override;
        virtual void updateStep() override;
        virtual void runEvent() override;

        // Contact bounce: each change is a burst of 2*Bounces+1 changes within Bounce_Time
        // (seconds; 0 = a clean change), ending in the new state
        double bounceTime() { return m_bounceTime; }
        void setBounceTime( double t ) { m_bounceTime = ( t > 0 ) ? t : 0; }
        int  bounces() { return m_bounces; }
        void setBounces( int n ) { m_bounces = ( n < 1 ) ? 1 : ( n > 100 ) ? 100 : n; }

        virtual void setAngle( double angle ) override;
        virtual void rotateAngle( double a ) override;
        void rotateText( double angle );

        virtual void setHidden( bool hid, bool hidArea=false, bool hidLabel=false ) override;

        QString key() { return m_key; }
        void setKey( QString key );

        // Keyboard input (K5): a switch key is one capital A-Z, operated with Shift+letter.
        // Other values (older circuits) are kept but do nothing; opening the properties clears them
 static bool validKey( QString key ) { return key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z'; }
        virtual QString propHelp( QString prop ) override;
        virtual void slotProperties() override;
        
        CustomButton* button() { return m_button; }

        virtual void setNClose( bool nc ) override;
        virtual void onbuttonclicked();
        virtual void keyEvent( QString key, bool pressed ){;}

    protected:
        //virtual void setflip() override;
        virtual void setSwitch( bool closed ) override;

        QString m_key;

        double m_bounceTime;
        int    m_bounces;
        bool   m_target;     // State the burst ends in
        int    m_bounceLeft; // Changes still to come in the burst (0 = none running)
        int    m_bounceN;    // Changes made so far in the burst
        bool   m_contact;    // State the contacts are in (m_closed: the one asked for)
        bool   m_started;    // First frame of the run applies the contact cleanly
        void   nextBounce();
        
        CustomButton* m_button;
        QGraphicsProxyWidget* m_proxy;

        QMetaObject::Connection m_keyEventConn;
};

#endif
