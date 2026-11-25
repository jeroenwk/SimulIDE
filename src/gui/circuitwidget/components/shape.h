/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SHAPE_H
#define SHAPE_H

#include "borderstyle.h"
#include "component.h"

class Shape : public Component
{
    public:
        Shape( QString type, QString id );
        ~Shape();

        void updateStep() override;
        
        QRectF boundingRect() const override {
            return QRectF( m_area.x()-m_border/2-1, m_area.y()-m_border/2-1, 
                           m_area.width()+m_border+2, m_area.height()+m_border+2 ); 
        }
        int hSize() { return m_hSize; }
        void setHSize( int size );
        
        int vSize() { return m_vSize; }
        void setVSize( int size );
        
        int border() { return m_border; }
        void setBorder( int border );

        QColor borderColor() { return m_bs.color(); }
        void setBorderColor( QColor color );

        QString borderStyle() { return m_bs.style(); }
        void setBorderStyle( QString style );

        QColor color() { return m_color; }
        void setColor( QColor color );

        double zVal() { return zValue(); }
        void setZVal( double v) { setZValue( v ); }

        double opac() { return m_opac; }
        void setOpac( double op );

        QString colorStr() { return m_color.name(); }
        void setColorStr( QString n ) { setColor( QColor(n) ); }

        QString borderColorStr() { return m_bs.colorStr(); }
        void setBorderColorStr( QString n );

        // Corner handles (specs/gfx-arrange.md). Corners of the area: 0 top left, 1 top right,
        // 2 bottom right, 3 bottom left, in the part's own (unrotated, unflipped) frame
        virtual QList<int> handleCorners() { return { 0, 1, 2, 3 }; }   // Line: its two ends
        virtual bool cornerFlips() { return false; }  // Line: dragged past the other end, it flips
        QPointF cornerScene( int corner );
        // Moves a corner to a scene point, the opposite corner staying where it is (size and
        // position change; a Line flips instead of shrinking below 1 px). No undo
        void setCornerScene( int corner, QPointF scenePos );
        void saveCornerUndo();                         // One undo step: position, size, flips

        // Arrange (Bring to Front, Forward, Send Backward, to Back) before the usual entries
        void contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) override;
        
    protected:
        void changed();
        QPointF cornerLocal( int corner );

        int m_hSize;
        int m_vSize;
        int m_border;
        BorderStyle m_bs;   // Line type and colour of the border

        double m_opac;

        bool m_changed;
};

#endif
