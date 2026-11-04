/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QComboBox>

#include "propval.h"
#include "propdialog.h"
#include "comproperty.h"
#include "circuit.h"

PropVal::PropVal( PropDialog* parent, CompBase* comp, ComProperty* prop )
       : QWidget( parent )
{
    m_propDialog = parent;
    m_component = comp;
    m_property  = prop;
    m_propName  = prop->name();
    m_blocked = false;
}
PropVal::~PropVal() {}

void PropVal::addDividers( QComboBox* unitBox, QString unit )
{
    unitBox->addItem("p"+unit );
    unitBox->addItem("n"+unit );
    unitBox->addItem("µ"+unit );
    unitBox->addItem("m"+unit );
}

void PropVal::addMultipliers( QComboBox* unitBox, QString unit )
{
    unitBox->addItem(    unit );
    unitBox->addItem("k"+unit );
    unitBox->addItem("M"+unit );
    unitBox->addItem("G"+unit );
    unitBox->addItem("T"+unit );
}

void PropVal::prepareChange()
{
    m_oldValue = m_property->getValStr();
    m_undo = m_property->flags() & propNoCopy; // May create or remove items (pins...)
    if( m_undo ) Circuit::self()->beginUndoStep();
}

void PropVal::saveChanges()
{
    if( m_undo ){ // Don'use endUndoStep() because We need addCompChange() before endCircuitBatch()
        Circuit::self()->calcCircuitChanges();
        Circuit::self()->addCompChange( m_component->getUid(), m_propName, m_oldValue );
        Circuit::self()->endCircuitBatch();
    }
    else if( m_property->getValStr() != m_oldValue ) // A plain value: one undo step
        Circuit::self()->saveCompChange( m_component->getUid(), m_propName, m_oldValue );
    m_propDialog->updtValues();
}
