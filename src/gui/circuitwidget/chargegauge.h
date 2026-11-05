/***************************************************************************
 *   Gauge bar and live label beside a reactive part: capacitor charge,    *
 *   inductor and transformer-coil current.                                *
 *   See specs/capacitor-charge.md and specs/inductor-current.md           *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CHARGEGAUGE_H
#define CHARGEGAUGE_H

#include <QGraphicsItem>
#include <QString>

class Component;

// What a gauge shows. Implemented by the part (or, for transformer coils, by one
// object per coil). Called from the GUI thread while the simulation worker is idle.
class GaugeOwner
{
    public:
        virtual ~GaugeOwner(){}

        virtual double  gaugeLevel()=0;              // |value| / reference, may exceed 1
        virtual bool    gaugeWarn() { return false; } // Red and blinking (over the rating, reversed)
        virtual double  gaugeTick() { return -1; }   // Rating mark as a level (0..1), < 0: none
        virtual QString gaugeText( int labelMode )=0; // Live label for ChargeView::labelMode(), "" = none
};

// A child item of the part, kept upright on screen: its transform undoes the parent's
// rotation and flip, so its coordinates are scene-aligned with the origin at the part
// centre. Two placements:
// - beside a two-pin part, on the side opposite its value label (default);
// - anchored: at a point of the part's outline, pointing outwards (transformer coils).
class ChargeGauge : public QGraphicsItem
{
    public:
        ChargeGauge( Component* part, GaugeOwner* owner );

        QRectF boundingRect() const override { return m_bound; }
        void paint( QPainter* p, const QStyleOptionGraphicsItem*, QWidget* ) override;

        // Anchor in part coordinates, outward direction in part coordinates, bar height
        void setAnchor( QPointF at, QPointF out, double height );

        void refresh();     // Layout and repaint (once per frame while shown)

    private:
        Component*  m_part;
        GaugeOwner* m_owner;

        bool    m_anchored;
        QPointF m_anchor;
        QPointF m_out;
        double  m_height;

        QRectF  m_bar;      // Gauge bar (upright coordinates)
        QPointF m_textPos;  // Label baseline start
        QRectF  m_bound;
        QString m_text;
};

#endif
