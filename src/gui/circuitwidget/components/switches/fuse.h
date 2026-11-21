/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef FUSE_H
#define FUSE_H

#include "e-resistor.h"
#include "e-fuseheat.h"
#include "comp2pin.h"

class LibraryItem;

// One-shot fuse (specs/fuse.md): a resistor that opens when its element melts, with the
// two-stage heat model of FuseHeat. It has no events or steps of its own in normal use: it
// accumulates I²dt in voltChanged() and updates the heat in closed form (§ 6.2).
class Fuse : public Comp2Pin, public eResistor
{
    public:
        Fuse( QString type, QString id );
        ~Fuse();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Fuse )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void runEvent() override;
        virtual void updateStep() override;

        double ratedCurrent() { return m_in; }
        void setRatedCurrent( double i );

        QString speed() { return m_speedStr; }
        void setSpeed( QString s );

        double i2t() { return m_i2tSet; }                 // 0 = from the preset
        void setI2t( double v );
        double vdrop() { return m_vdropSet; }             // V, 0 = from the preset
        void setVdrop( double v );

        QString model() { return m_resMode ? "Resistor" : "Fuse"; }
        void setModel( QString m );

        double ratedVolt() { return m_ratedVolt; }
        void setRatedVolt( double v ) { if( v > 0 ) m_ratedVolt = v; }
        double breaking() { return m_breakSet; }          // 0 = from the preset
        void setBreaking( double b ) { m_breakSet = b > 0 ? b : 0; }

        bool blownProp() { return m_blown; }
        void setBlownProp( bool b );
        bool keepBlown() { return m_keepBlown; }
        void setKeepBlown( bool k ) { m_keepBlown = k; }

        double minFuse() { return m_cMinFuse; }           // Custom: I_mf / In
        void setMinFuse( double m );
        double share() { return m_cShare; }               // Custom: G2 / G1
        void setShare( double s );
        double tau2() { return m_cTau2; }                 // Custom: body time constant, s
        void setTau2( double t );

        // Values in use (presets resolved)
        double i2tUsed();                                  // Melting I²t, A²s
        double vdropUsed();                                // V at In
        double breakingUsed();                             // A
        double resistance() { return vdropUsed()/m_in; }

        // For the CLI (read): now, without changing the state
        bool   resistorMode() { return m_resMode; }
        bool   blown()   { return m_blown; }
        double blownAt() { return m_blownAt; }             // s, -1 if not blown
        double heatNow();                                  // θ (1 = melts)
        double i2tSum();                                   // ∫I²dt since the start, A²s
        QString marking();                                 // "T1A", "F500mA"

        void replace();                                    // A fresh, cold fuse

        virtual void contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) override;
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        void setupHeat();
        void integrate( uint64_t t );
        void flush( uint64_t t );
        void blow( uint64_t t, bool bySim );
        void schedule( uint64_t t, uint64_t at );
        void planNext( uint64_t t, bool melt );
        void updateState();
        void updateLabel();

        enum { spF = 0, spT, spCustom } m_speed;
        QString m_speedStr;

        double m_in;            // Rated current, A
        double m_i2tSet;
        double m_vdropSet;
        double m_ratedVolt;
        double m_breakSet;
        double m_cMinFuse, m_cShare, m_cTau2;
        bool   m_resMode;
        bool   m_keepBlown;

        FuseHeat m_heat;
        uint64_t m_h;           // ps: longest interval between exact updates (τ1/50)

        // Accumulation between exact updates (simulation thread)
        uint64_t m_tFlush;      // ps: time of the heat state
        uint64_t m_tPrev;       // ps: last change of current
        double   m_u;           // I² since m_tPrev
        double   m_S;           // ∫I²dt since m_tFlush
        double   m_sumI2t;      // ∫I²dt up to m_tFlush
        double   m_predU;       // u the pending event was planned for (0: none)

        bool     m_blown;
        bool     m_blownBySim;
        double   m_blownAt;
        double   m_blowCurrent;
        bool     m_broke;
        bool     m_overVolt;
        int      m_level;       // State shown: 0 OK, 1 HOT, 2 BLOWN
        bool     m_msgPending;  // Messages line to write (GUI thread)
        bool     m_voltMsgPending;
        uint64_t m_overVoltAt;  // ps: first time above the rated peak
        double   m_overVoltMax; // V: highest until the message is written
        bool     m_repaint;
};

#endif
