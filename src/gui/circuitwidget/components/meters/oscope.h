/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef OSCOPE_H
#define OSCOPE_H

#include "plotbase.h"

class LibraryItem;
class OscopeChannel;
class OscWidget;
class DataWidget;

class Oscope : public PlotBase
{
    public:
        Oscope( QString type, QString id );
        ~Oscope();

        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        double filter() { return m_filter; }
        void setFilter( double filter );

        int autoSC() { return m_auto; }
        void setAutoSC( int ch );

        QString hideCh();
        void setHideCh( QString hc );

        int tracks();
        void setTracks( int tracks );

        virtual QString timPos() override;
        virtual void setTimPos( QString tp ) override;

        virtual QString volDiv() override;
        virtual void setVolDiv( QString vd ) override;

        QString volPos();
        void setVolPos( QString vp );

        virtual void setTrigger( int ch ) override;

        // Trigger level: Auto = middle of the last cycle (the old behaviour), else a fixed level
        bool trigAuto() { return m_trigAuto; }
        void setTrigAuto( bool a );
        double trigLevel() { return m_trigLevel; }
        void setTrigLevel( double l );
        virtual bool trigLevelOn( double* level ) override;

        virtual void updateStep() override;

        virtual void setTimeDiv( uint64_t td ) override;

        int64_t timePos( int ch ){ return m_timePos[ch]; }
        void setTimePos( int ch, int64_t tp );
        virtual void moveTimePos( int64_t delta ) override;

        double voltDiv( int ch ){ return m_voltDiv[ch]; }
        void setVoltDiv( int ch, double vd );

        double voltPos( int ch ){ return m_voltPos[ch]; }
        void setVoltPos( int ch, double vp );

        void hideChannel( int ch, bool hide );

        virtual void setTunnels( QString tunnels ) override;

        virtual void expand( bool e ) override;

        DataWidget* dataW() { return m_dataWidget; }

    private:
        bool levelEnd( uint64_t now, uint64_t timeFrame, uint64_t* end );

        double m_filter;

        bool   m_trigAuto;
        double m_trigLevel;
        int    m_trigState;   // Display state from the level edges (fixed or auto), for the debug log: -1 unset, 0 free running, 1 triggered

        int m_auto;

        int64_t m_timePos[4];
        double  m_voltDiv[4];
        double  m_voltPos[4];
        bool    m_hideCh[4];

        OscWidget*  m_oscWidget;
        DataWidget* m_dataWidget;
};

#endif
