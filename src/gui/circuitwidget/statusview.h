/***************************************************************************
 *   Part state display: the states parts report (setStatus), as badges   *
 *   above each part and its outline in the most severe state's colour     *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef STATUSVIEW_H
#define STATUSVIEW_H

#include <QString>
#include <QList>
#include <QColor>
#include <QGraphicsItem>

#include "updatable.h"

class Component;

class StatusView : public Updatable
{
    public:
        static StatusView* self();

        struct Block { QString name; QString text; int level; };   // One reporting core of a part

        bool enabled() { return m_enabled; }
        void setEnabled( bool en );

        // The states a top-level part reports: itself (a scripted core placed on its own)
        // or each scripted core inside it (a subcircuit made of datasheet blocks)
        static QList<Block> blocks( Component* comp );
        static QString blockName( Component* inner );   // "MT7833A_supply-1" -> "supply"

        static QColor levelColor( int level );          // For the current canvas colour

        void attach();      // Register with the current Simulator (on every simulation start)
        void clear();       // Simulation stopped: remove the badges

        virtual void updateStep() override;

    private:
        StatusView();

        bool m_enabled;
};

class StatusBadge : public QGraphicsItem       // Child of the part; draws in scene coordinates
{
    public:
        StatusBadge( Component* comp );

        enum { Type = UserType + 40 };
        int type() const override { return Type; }

        void setBlocks( const QList<StatusView::Block>& blocks );

        virtual QRectF boundingRect() const override { return m_bounds; }
        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        struct Pill { QRectF rect; QString text; int level; };

        Component*  m_comp;
        QRectF      m_body;       // Part body, scene coordinates
        QList<Pill> m_pills;
        int         m_worst;
        QRectF      m_bounds;
};

#endif
