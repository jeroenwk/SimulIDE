/***************************************************************************
 *   Keyboard input: typing the selected part's value (K7)                 *
 ***( see copyright.txt file at root folder )*******************************/

#include <QKeyEvent>
#include <QDebug>

#include "valueeditor.h"
#include "circuit.h"
#include "component.h"
#include "comproperty.h"
#include "label.h"
#include "theme.h"
#include "utils.h"

ValueEditor::ValueEditor()
{
    m_active = false;
}

QChar ValueEditor::entryChar( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    if( !( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) ) return '\0';
    QString t = event->text();
    if( t.size() == 1 )
    {
        if( t[0].isDigit() ) return t[0];
        if( t[0] == '-' ) return '-';
        if( t[0] == '.' || t[0] == ',' ) return '.';
        if( t[0] == '_' ) return '\0';       // The AZERTY 8 key unshifted: never a digit
    }
    int key = event->key();
    if( key >= Qt::Key_0 && key <= Qt::Key_9 ) return QChar( '0'+key-Qt::Key_0 );  // é = Key_2
#ifdef Q_OS_MACOS
    quint32 vk = event->nativeVirtualKey();   // kVK_ANSI_0…9: the number row on every layout
    if     ( vk == 0x12 ) return '1';         // ("-", the AZERTY 6 key, is a minus: by text)
    else if( vk == 0x13 ) return '2';
    else if( vk == 0x14 ) return '3';
    else if( vk == 0x15 ) return '4';
    else if( vk == 0x16 ) return '6';
    else if( vk == 0x17 ) return '5';
    else if( vk == 0x19 ) return '9';
    else if( vk == 0x1A ) return '7';
    else if( vk == 0x1C ) return '8';
    else if( vk == 0x1D ) return '0';
#endif
    if( key == Qt::Key_Minus ) return '-';
    if( key == Qt::Key_Period || key == Qt::Key_Comma ) return '.';
    return '\0';
}

Component* ValueEditor::part()
{
    if( m_uid.isEmpty() ) return nullptr;
    for( Component* c : *Circuit::self()->compList() ) if( c->getUid() == m_uid ) return c;
    return nullptr;
}

bool ValueEditor::start( Component* comp, QChar first )
{
    if( !comp ) return false;
    QString prop = comp->valueProperty();
    if( prop.isEmpty() ) return false;                   // This part takes no typed value

    m_active  = true;
    m_uid     = comp->getUid();
    m_prop    = prop;
    m_oldText = comp->valLabel()->toPlainText();         // Restored on revert; the label shows
    comp->valLabel()->setVisible( true );                // while typing, even when hidden
    m_buf     = ( first == '.' ) ? "0." : QString( first );
    qDebug() << "[kbd] value edit" << prop << "on" << m_uid;
    updateLabel();
    return true;
}

// notMine: not a value key — the caller reverts and the key does its normal action
// (Tab: next part, r: place a part, ...). committed: the property is set and the edit
// over; the caller leaves the part as Enter does
ValueEditor::result_t ValueEditor::key( QKeyEvent* event, Qt::KeyboardModifiers mods )
{
    int key = event->key();
    if( key == Qt::Key_Backspace && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
    {
        if( !m_buf.isEmpty() ) m_buf.chop(1);
        if( m_buf.isEmpty() ) revert();                  // D8: no char left: the edit ends; the
        else                  updateLabel();             // next Backspace deletes the part
        return handled;
    }
    if( mods == Qt::NoModifier )
    {
        if( key == Qt::Key_Escape ) { revert(); return handled; }       // Stays selected
        if( key == Qt::Key_Return || key == Qt::Key_Enter ) { commit(); return committed; }
    }
    QChar c = entryChar( event, mods );
    if( c != '\0' ){ addChar( c ); return handled; }
    QString t = event->text();
    if( t.size() == 1 && ( mods == Qt::NoModifier || mods == Qt::ShiftModifier ) )
    {   // Multipliers p n u m k M G (u = µ, M mega, m milli); capitals type the small
        // ones too (K = k), so a shifted keyboard works the same
        if( QString("pnumkMG").contains( t[0] ) ) { addChar( t[0] );           return handled; }
        if( QString("PNUK").contains( t[0] ) )    { addChar( t[0].toLower() ); return handled; }
    }
    return notMine;
}

// The buffer's multiplier letter, '\0' when none is typed
static QChar bufferMult( const QString& buf )
{
    for( QChar c : buf )
        if( !c.isDigit() && c != '-' && c != '.' && c != ',' ) return c;
    return '\0';
}

// What does not fit the grammar (a second dot, a second multiplier, a sign mid-value)
// is ignored
void ValueEditor::addChar( QChar c )
{
    bool hasMult = bufferMult( m_buf ) != '\0';
    bool hasDot  = m_buf.contains( '.' );
    int  digits  = 0;
    for( QChar ch : m_buf ) if( ch.isDigit() ) digits++;
    if( c.isDigit() ) m_buf.append( c );                 // Digits always fit (4k7 too)
    else if( c == '.' ) { if( !hasDot && !hasMult && digits > 0 ) m_buf.append( c ); }
    else if( c != '-' ) { if( !hasMult && digits > 0 ) m_buf.append( c ); }
    // c == '-': the sign is only the entry key; mid-edit it is ignored
    updateLabel();
}

// While no multiplier is typed the part's current one is shown (200▏kΩ): it does not
// change unless the buffer types one
void ValueEditor::updateLabel()
{
    Component* comp = part();
    if( !comp ) return;
    QString unit;
    if( ComProperty* p = comp->property( m_prop ) )
    {
        unit = p->unit();
        if( bufferMult( m_buf ) != '\0'                  // A typed multiplier replaces it
         && unit.size() > 1 && QString("pnµmkMGT").contains( unit.at(0) ) ) unit = unit.mid( 1 );
        if( unit.startsWith("_") ) unit = unit.mid( 1 ); // "_%": shown without the _
    }
    comp->valLabel()->setDefaultTextColor( Theme::highlight() );
    comp->setValLabelText( m_buf+QChar( 0x258F )+unit );
}

void ValueEditor::revert()
{
    if( !m_active ) return;
    m_active = false;
    Component* comp = part();
    m_uid.clear();
    m_buf.clear();
    if( comp )
    {
        comp->valLabel()->setDefaultTextColor( Theme::valLabel() );
        comp->valLabel()->setVisible( comp->showVal() );
        ComProperty* p = comp->property( m_prop );
        if( p && comp->showProp() == m_prop ) comp->setValLabelText( p->getValStr() );  // As set now
        else                                  comp->setValLabelText( m_oldText );
    }
    m_prop.clear();
}

void ValueEditor::reset()
{
    m_active = false;
    m_uid.clear();
    m_prop.clear();
    m_buf.clear();
}

// "200 kΩ", "4.7 µF", "100 nF"; digits after the multiplier are the fraction (4k7 =
// 4.7 k). Without a multiplier the part's current unit is kept (4.7 kΩ + 100 = 100 kΩ)
QString ValueEditor::valueString( Component* comp )
{
    ComProperty* p = comp->property( m_prop );
    if( !p ) return "";
    QString num = m_buf;
    QChar mult = bufferMult( m_buf );
    if( mult != '\0' )
    {
        int i = m_buf.indexOf( mult );
        num  = m_buf.left( i );
        QString tail = m_buf.mid( i+1 );
        if( !tail.isEmpty() ) num = num.contains('.') ? num+tail : num+"."+tail;
    }
    num.replace( ',', '.' );

    QString unit = p->unit();
    if( mult == '\0' )                                   // Keep the current multiplier
        return num + ( unit.isEmpty() ? "" : " "+unit );
    QString base = unit;
    if( base.size() > 1 && QString("pnµmkMGT").contains( base.at(0) ) ) base = base.mid( 1 );
    if( base.startsWith("_") || base.isEmpty() )         // No SI prefixes: the value itself
        return QString::number( num.toDouble()*getMultiplier( QString( mult ) ), 'g', 15 )+" "+base;
    return num+" "+( mult == 'u' ? QString("µ") : QString( mult ) )+base;
}

// One undo step: the value, and ShowProp when the value was not shown (a typed value
// is shown from then on; saved, since ShowProp is not empty). What the property
// refuses it clamps; the label shows what was set
void ValueEditor::commit()
{
    Component* comp = part();
    bool digits = false;
    for( QChar ch : m_buf ) if( ch.isDigit() ) digits = true;
    if( comp && digits )
    {
        QString val     = valueString( comp );
        QString old     = comp->getPropStr( m_prop );
        QString oldShow = comp->getPropStr("ShowProp");
        Circuit* circ = Circuit::self();
        circ->beginUndoStep();
        comp->setPropStr( m_prop, val );
        circ->addCompChange( m_uid, m_prop, old );
        if( comp->showProp() != m_prop )
        {
            comp->setPropStr("ShowProp", m_prop );
            circ->addCompChange( m_uid, "ShowProp", oldShow );
        }
        circ->calcCircuitChanges();
        circ->endCircuitBatch();
        circ->setChanged();
        qDebug() << "[kbd] value" << val << "->" << m_prop << "on" << m_uid;
    }
    revert();
}

QJsonObject ValueEditor::state()
{
    return QJsonObject{ {"part", m_uid}, {"property", m_prop}, {"buffer", m_buf} };
}
