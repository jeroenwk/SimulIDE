/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CONNECTOR_H
#define CONNECTOR_H

#include <QColor>

#include "compbase.h"

class ConnectorLine;
class Pin;

class Connector : public CompBase
{
    public:
        Connector( QString type, QString id, Pin* startpin, Pin* endpin = NULL );
        ~Connector();

        QString pListStr() { return m_pointList.join(","); }
        QStringList pointList() { refreshPointList(); return m_pointList; }
        void setPointListStr( QString pl );
        void setPointList( QStringList pl );

        void dummySetter( QString ) {;}

        // Wire colours (specs/wire-colors.md): the line colour (invalid: the theme's),
        // a translucent band behind the wire (invalid: none) and its width (0: default)
        static constexpr double defaultBandWidth = 6;
        QColor lineColor() { return m_lineColor; }
        QColor bandColor() { return m_bandColor; }
        double bandWidth() { return m_bandWidth > 0 ? m_bandWidth : defaultBandWidth; }
        bool   hasStyle()  { return m_lineColor.isValid() || m_bandColor.isValid(); }
        QString colorStr();
        void    setColorStr( QString c );
        QString bandColorStr();
        void    setBandColorStr( QString c );
        QString bandWidthStr();
        void    setBandWidthStr( QString w );
        void    copyStyle( Connector* from );   // Split and merged wires keep their colours

        QString startPinId();
        QString endPinId();
        // The instrument input this wire is a lead of: wired to it directly, or through a
        // tunnel group that one of its tunnels is wired to. nullptr: an ordinary wire
        Pin* leadPin();

        Pin* startPin() { return m_startPin;}
        void setStartPin( Pin* pin ) { m_startPin = pin; }
        Pin* endPin() { return m_endPin; }
        void setEndPin( Pin* pin) { m_endPin = pin; }

        double getVoltage();

        // Current flow (CurrentFlow): signed current from start to end pin (A),
        // dot phase along the wire (px), whether dots are drawn, piece flag
        // (0 ok, 1 approximate, 2 inconsistent)
        double flowCurrent = 0;
        double flowPhase   = 0;
        bool   flowShown   = false;
        int    flowFlag    = 0;

        QList<ConnectorLine*>* lineList() { return &m_conLineList; }

        void incActLine() { if( m_actLine < m_conLineList.size()-1 ) m_actLine += 1; }

        ConnectorLine* addConLine( int x1, int y1, int x2, int y2, int index );
        void addConLine( ConnectorLine* line, int index );
        void remNullLines();
        void refreshPointList();
        void updateConRoute( Pin* nod, QPointF this_point );
        void closeCon( Pin* endpin );
        void splitCon( int index, Pin* pin0, Pin* pin2 );

        void updateLines();

        void setVisib(  bool vis );
        void setSelected( bool selected );
        
        void setIsBus( bool bus );
        bool isBus() { return m_isBus; }
        
        void move( QPointF delta );

        void remove();
        void remLines();

        bool m_freeLine;

    private:
        void remConLine( ConnectorLine* line  );
        void updateCon();
        void connectLines( int index1, int index2 );
        void disconnectLines( int index1, int index2 );

        int m_actLine;
        int m_lastindex;
        
        bool m_isBus;

        Pin*    m_startPin;
        Pin*    m_endPin;

        QStringList  m_pointList;

        QColor m_lineColor;
        QColor m_bandColor;
        double m_bandWidth = 0;

        QList<ConnectorLine*> m_conLineList;
};

#endif
