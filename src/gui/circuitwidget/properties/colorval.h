/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef COLORVAL_H
#define COLORVAL_H

#include "ui_colorval.h"
#include "propval.h"

class Component;
class PropDialog;

class ColorVal : public PropVal, private Ui::ColorVal
{
    Q_OBJECT
    
    public:
        ColorVal( PropDialog* parent, CompBase* comp, ComProperty* prop );
        ~ColorVal();

        virtual void setup( bool ) override;
        virtual void updtValues() override;

        bool eventFilter( QObject* object, QEvent* event) override;

    //public slots:


    private:
        void changeColor();
        void showValue( QString val );

        QColor  m_color;
        QString m_keyword;   // Type "color_<keyword>": a value meaning no own colour (theme, none)

        bool m_blocked;
};

#endif
