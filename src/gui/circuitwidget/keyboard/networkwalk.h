/***************************************************************************
 *   Keyboard input: walking the wire network of a pin (K9,                *
 *   specs/keyboard-input.md)                                              *
 *                                                                         *
 *   The steps are every wire, corner and junction dot reachable from the  *
 *   pin along wires and through dots, never through a part. Each has a    *
 *   path length from the pin along the wires (wire: where it is entered;  *
 *   corner, dot: their position); sorted by it, ties dot < wire < corner  *
 *   (a dot before the wires leaving it), then scene position. Steps are   *
 *   kept by uid (+ corner index), so they are rebuilt, not trusted, after *
 *   an edit.                                                              *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef NETWORKWALK_H
#define NETWORKWALK_H

#include <QList>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QJsonObject>

class Pin;
class Connector;
class Node;

class NetworkWalk
{
    public:
        enum kind_t { stepWire=0, stepCorner, stepDot };
        struct Step {
            kind_t  kind;
            QString uid;      // Connector (wire, corner) or Node (dot)
            int     index;    // Corner: its point index in the wire's point list; else -1
            QPoint  pos;      // Wire: where it is entered; corner, dot: their position
            double  len;      // Path length from the pin
        };

        NetworkWalk();

 static QList<Step> build( Pin* pin );         // The ordered steps (empty: no wire)

        bool start( Pin* pin );                // false: the pin has no wire
        void stop();
        bool active() { return m_active; }

        QString pinId()   { return m_pinId; }  // The pin the walk started from
        QString partUid() { return m_partUid; }

        int  size()  { return m_steps.size(); }
        int  index() { return m_index; }
        Step current();                        // Only while active and not empty
        void stepBy( int dir );                // Next / previous, wrapping

        // After an edit: rebuild from the pin, the selection on the step of that kind
        // nearest to pos (any kind if none). false: no step left (or the pin is gone)
        bool rebuild( QPoint pos, kind_t kind );

        Connector* wire( const Step& s );      // nullptr: gone
        Node*      dot( const Step& s );
        QStringList cornerMoved( const Step& s, QPoint delta );   // The wire's new point list

        QJsonObject state();                   // CLI kbd state

 static QString kindName( kind_t k );

    private:
        bool         m_active;
        QString      m_pinId;
        QString      m_partUid;
        QList<Step>  m_steps;
        int          m_index;
};

#endif
