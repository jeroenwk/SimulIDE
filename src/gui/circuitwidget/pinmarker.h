/***************************************************************************
 *   Pin markers: a ring and a tag ("1", "+", "3 VDD") on pins of the      *
 *   canvas, so the pin a trigger watches can be seen (specs/triggers.html)*
 ***( see copyright.txt file at root folder )*******************************/

// A marker is drawn by a top-level item (above wires and parts), kept on its pin by an anchor:
// an empty child item of the pin, told when the pin moves in the scene (the part moved, rotated)
// and deleted with it. The two delete each other. The marker keeps its size at any zoom and
// takes no mouse clicks; its tag goes beside the pin, off the wire's path.
// One owner at a time (the trigger editor, the Triggers pane): show() replaces the markers of
// the previous owner, clear( owner ) removes them only if they are still that owner's.

#ifndef PINMARKER_H
#define PINMARKER_H

#include <QGraphicsItem>
#include <QList>
#include <QPair>
#include <QString>

class Pin;
class PinMarkerAnchor;

class PinMarker : public QGraphicsItem
{
    friend class PinMarkerAnchor;
    public:
 static void show( const void* owner, const QList<QPair<Pin*, QString>>& pins );
 static void clear( const void* owner );
 static int  count();                         // Markers shown (tests)

        ~PinMarker();

        QRectF boundingRect() const override;
        void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        PinMarker( Pin* pin, const QString& tag );
        void follow();                        // Onto the pin, the tag on its side

        Pin* m_pin;
        PinMarkerAnchor* m_anchor;
        QString m_tag;
        QPointF m_side;                       // The tag's direction from the ring (screen)
        QRectF  m_tagRect;

 static QList<PinMarker*> m_markers;
 static const void* m_owner;
};

class PinMarkerAnchor : public QGraphicsItem
{
    friend class PinMarker;
    public:
        PinMarkerAnchor( Pin* pin, PinMarker* marker );
        ~PinMarkerAnchor();
        QRectF boundingRect() const override { return QRectF(); }
        void paint( QPainter*, const QStyleOptionGraphicsItem*, QWidget* ) override {}
    protected:
        QVariant itemChange( GraphicsItemChange change, const QVariant& value ) override;
    private:
        PinMarker* m_marker;
};

#endif
