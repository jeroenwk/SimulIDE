/***************************************************************************
 *   SimulIDE command line control interface                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CLIREFS_H
#define CLIREFS_H

#include <QString>
#include <QJsonObject>
#include <QPointF>

class Component;
class Connector;
class Pin;

// Resolve user references to circuit objects and describe them as JSON
namespace CliRefs
{
    Component* comp( QString ref );              // uid ("Resistor-5") or label ("R1"); throws
    Pin*       pin( QString ref );               // "R1.lPin", "U1.PB5", "R1.0"; throws
    Connector* wire( QString ref );              // Connector uid; throws

    QJsonObject compJson( Component* comp, bool detail );
    QJsonObject pinJson( Pin* pin, int index );
    QJsonObject wireJson( Connector* con );

    QString pinRef( Pin* pin );                  // "R1.lPin" style reference for output
    QPointF parsePoint( QString s );             // "x,y"; throws
}
#endif
