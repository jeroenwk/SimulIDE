/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef ROTARYSWITCH_H
#define ROTARYSWITCH_H

#include <QDial>

#include "component.h"
#include "e-element.h"

class Pin;
class QGraphicsSimpleTextItem;
class LibraryItem;
class eResistor;
class QGraphicsProxyWidget;

// A pin's name: the text item the pin made for it (so the CLI and the triggers know the pin
// by name), placed by the switch, and the centre it is laid at, in the part's own frame
struct NameLabel
{
    Pin*                    pin;
    QGraphicsSimpleTextItem* name;
    QPointF                 centre;
};

// The knob of a rotary switch: a dial with one value per position. Drag it round, click it
// (one detent clockwise, with Shift the other way) or turn the wheel; the CLI sets it with
// `ui dial`. The pointer stands at startAngle + 30° × value, clockwise from 12 o'clock
class RotaryKnob : public QDial
{
    Q_OBJECT

    public:
        RotaryKnob( QWidget* parent=NULL );

        void setStartAngle( double deg ) { m_start = deg; update(); }

    protected:
        void paintEvent( QPaintEvent* ) override;
        void mousePressEvent( QMouseEvent* e ) override;
        void mouseMoveEvent( QMouseEvent* e ) override;
        void mouseReleaseEvent( QMouseEvent* e ) override;
        void wheelEvent( QWheelEvent* e ) override;

    private:
        double pointerAngle() { return m_start + 30.0*value(); }
        double angleAt( QPointF pos );      // Of a point: degrees clockwise from 12 o'clock, -180..180

        double m_start;
        bool   m_dragging;
        double m_offset;                    // Pointer angle minus mouse angle where the drag began

        QPointF m_pressPos;
};

// Rotary switch (docs/ROTARY_SWITCH.md): 1 to 4 poles, 3 to 12 positions, 30° per detent, as
// the SR25 / SR26 deck (poles × positions at most 12: one contact per pole and position). A
// round body with the knob in the middle; the commons on the left, the throws clockwise as on
// the deck (the top from the left, down the right side, the bottom from the right), each with
// its name inside the rim, horizontal whatever side it is on
// and however the part is turned or flipped. Each contact is a resistor (Contact_Res) from its pole's common to
// its throw. While running, a step has the switch's timing: break before make after
// Transit_Time, or make before break with the other contact opening after Overlap_Time, and
// a bounce burst at the chattering contact when Bounce_Time is set
class RotarySwitch : public Component, public eElement
{
    public:
        RotarySwitch( QString type, QString id );
        ~RotarySwitch() override;

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        int  poles() { return m_poles; }
        void setPoles( int p );
        int  positions() { return m_positions; }
        void setPositions( int n );
        int  position() { return m_position + 1; }        // 1..positions, as saved and typed
        void setPosition( int pos );
        bool shorting() { return m_shorting; }
        void setShorting( bool s ) { m_shorting = s; }
        double transitTime() { return m_transit; }
        void setTransitTime( double t ) { m_transit = ( t > 0 ) ? t : 0; }
        double overlapTime() { return m_overlap; }
        void setOverlapTime( double t ) { m_overlap = ( t > 0 ) ? t : 0; }
        double bounceTime() { return m_bounceTime; }
        void setBounceTime( double t ) { m_bounceTime = ( t > 0 ) ? t : 0; }
        int  bounces() { return m_bounces; }
        void setBounces( int n ) { m_bounces = ( n < 1 ) ? 1 : ( n > 100 ) ? 100 : n; }
        double contactRes() { return m_contactRes; }
        void setContactRes( double r );
        QString key() { return m_key; }
        void setKey( QString key );

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void runEvent() override;
        virtual void remove() override;

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( RotarySwitch )
        virtual void stateRestored() override;   // The knob shows the restored position

        virtual void setAngle( double angle ) override;
        virtual void rotateAngle( double a ) override;
        virtual void setflip() override;
        virtual void moveSignal() override;

        virtual QString propHelp( QString prop ) override;

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

        void step( int dir );               // One detent (+1 clockwise): a click on the knob, a key
        void dialChanged( int v );

    protected:
        enum { modeNone = 0, modeWait = 1, modeBounce = 2 };

        Pin*  addPin( int angle, QPoint pos, QString name, int index );
        void  setupPins();                  // Pins, ePins and contacts for the poles and positions
        bool  isClosed( int pole, int pos );
        void  setClosed( int pole, int pos, bool closed );
        void  rest();                       // Not running: the contacts follow the knob at once
        void  moveTo( int pos );            // Running: the knob went to another position, with the timing
        void  arrive();                     // The wait is over: the contact closes, the bounce burst starts
        void  nextBounce();
        void  finishBounce();
        void  showPosition();               // The value label
        void  placeLabels();                // The pins' names: horizontal, unmirrored, at their places
        void  placeKnob();                 // The knob in the middle of the body, upright and unmirrored

        int     m_radius;                  // Of the body: 40, or 48 when a side carries 4 pins
        int     m_poles;
        int     m_positions;
        int     m_position;                 // 0..m_positions-1

        bool    m_shorting;
        double  m_transit;                  // s
        double  m_overlap;                  // s
        double  m_bounceTime;               // s
        int     m_bounces;
        double  m_contactRes;               // Ω
        double  m_onAdmit;                  // 1 / m_contactRes
        QString m_key;

        std::vector<eResistor*> m_contact;  // m_contact[pole*m_positions + pos]

        RotaryKnob* m_knob;
        QGraphicsProxyWidget* m_proxy;
        QMetaObject::Connection m_keyEventConn;

        std::vector<NameLabel> m_names;     // One per pin

        int  m_at;                          // The position the mechanism is at, or going to
        int  m_mode;
        bool m_started;                     // The first frame of a run sets the contacts cleanly
        bool m_endClosed;                   // The chattering contact ends closed (a make) or open (a break)
        bool m_isClosed;                    // Its state now
        int  m_chatter[4];                  // Per pole: the position of the contact that chatters, -1 none
        int  m_bounceLeft;                  // Changes still to come (0: none running)
        int  m_bounceN;                     // Changes made so far
};

#endif
