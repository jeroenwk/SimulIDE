/***************************************************************************
 *   Trigger editor (specs/triggers.html §4)                               *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QButtonGroup>

#include "triggereditor.h"
#include "circuit.h"
#include "component.h"
#include "node.h"
#include "pin.h"
#include "simulator.h"

static QString num( double v, const QString& unit )   // 0.35, "A" -> "350 mA"
{
    if( v == 0 ) return "0 "+unit;
    static const char* pre[] = { "p","n","µ","m","","k","M","G","T" };
    int e = (int)std::floor( std::log10( std::fabs( v ) )/3 );
    e = std::max( -4, std::min( 4, e ) );
    return QString::number( v/std::pow( 1000, e ), 'g', 6 )+" "+pre[e+4]+unit;
}

static bool parse( const QString& text, const QString& unit, double* v )
{
    bool ok = false;
    double d = Simulator::parseSi( text.trimmed().replace(' ', ""), unit, &ok );
    if( ok ) *v = d;
    return ok;
}

static QString timeText( uint64_t ps ) { return ps ? num( ps*1e-12, "s" ) : ""; }

static bool parseTime( const QString& text, uint64_t* ps )
{
    if( text.trimmed().isEmpty() ){ *ps = 0; return true; }
    double s = 0;
    if( !parse( text, "s", &s ) || s < 0 ) return false;
    *ps = (uint64_t)std::llround( s*1e12 );
    return true;
}

static QWidget* row( QWidget* parent, QList<QWidget*> ws, bool stretch=true )
{
    QWidget* w = new QWidget( parent );
    QHBoxLayout* l = new QHBoxLayout( w );
    l->setContentsMargins( 0, 0, 0, 0 );
    l->setSpacing( 6 );
    for( QWidget* x : ws ) l->addWidget( x );
    if( stretch ) l->addStretch( 1 );
    return w;
}

TriggerEditor::TriggerEditor( QWidget* parent, Component* part )
             : QWidget( parent )
{
    m_fixed = part;
    QFormLayout* f = new QFormLayout( this );
    f->setContentsMargins( 0, 0, 0, 0 );
    f->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );

    m_part = new QComboBox( this );
    m_part->setMinimumContentsLength( 18 );
    m_part->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    if( !m_fixed ) f->addRow( tr("Part"), m_part );
    else m_part->hide();

    m_kState = new QRadioButton( tr("State"), this );
    m_kVolt  = new QRadioButton( tr("Voltage"), this );
    m_kCurr  = new QRadioButton( tr("Current"), this );
    QButtonGroup* kg = new QButtonGroup( this );
    kg->addButton( m_kState ); kg->addButton( m_kVolt ); kg->addButton( m_kCurr );
    f->addRow( tr("Watch"), row( this, { m_kState, m_kVolt, m_kCurr } ) );

    m_block = new QComboBox( this );
    m_pin   = new QComboBox( this );
    m_toLabel = new QLabel( tr("to"), this );
    m_ref   = new QComboBox( this );
    f->addRow( "", row( this, { m_block, m_pin, m_toLabel, m_ref } ) );

    m_when = new QComboBox( this );
    m_cond = new QStackedWidget( this );
    m_edge = new QComboBox( this );
    m_edge->addItems( { tr("↑ rising"), tr("↓ falling"), tr("↕ either") } );
    m_level = new QLineEdit( this );
    m_level->setMaximumWidth( 110 );
    m_cond->addWidget( row( this, { m_edge, m_level } ) );                         // 0: edge
    m_low  = new QLineEdit( this );
    m_high = new QLineEdit( this );
    m_low->setMaximumWidth( 100 );
    m_high->setMaximumWidth( 100 );
    m_cond->addWidget( row( this, { m_low, new QLabel("…", this ), m_high } ) );  // 1: window
    m_from = new QComboBox( this );
    m_to   = new QComboBox( this );
    m_from->setEditable( true );
    m_to->setEditable( true );
    for( QComboBox* cb : { m_from, m_to } ){
        cb->setMinimumContentsLength( 11 );
        cb->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    }
    m_cond->addWidget( row( this, { new QLabel( tr("from"), this ), m_from, new QLabel( tr("to"), this ), m_to } ) );   // 2
    m_cond->addWidget( new QWidget( this ) );                                       // 3: any change
    m_sev = new QComboBox( this );
    m_sev->addItems( { "ON", "WAIT", "FAULT" } );
    m_cond->addWidget( row( this, { m_sev } ) );                                    // 4: reaches
    f->addRow( tr("When"), row( this, { m_when, m_cond } ) );

    m_hyst = new QLineEdit( this );
    m_hyst->setMaximumWidth( 110 );
    m_hyst->setPlaceholderText( tr("auto (1 %)") );
    m_hystRow = row( this, { new QLabel( tr("hysteresis"), this ), m_hyst } );
    f->addRow( "", m_hystRow );

    m_pause    = new QCheckBox( tr("Pause"), this );
    m_keyframe = new QCheckBox( tr("Key frame"), this );
    m_before   = new QLineEdit( this );
    m_before->setPlaceholderText("0");
    m_before->setMaximumWidth( 80 );
    m_before->setToolTip( tr("Save the key frame this long before the trigger (with the history)") );
    f->addRow( tr("Action"), row( this, { m_pause, m_keyframe, new QLabel( tr("before"), this ), m_before,
                                          new QLabel( tr("(neither: count only)"), this ) } ) );

    m_mode = new QComboBox( this );
    m_mode->addItems( { tr("single"), tr("normal") } );
    m_mode->setToolTip( tr("single: disarms itself after firing; normal: fires each time") );
    m_holdoff = new QLineEdit( this );
    m_holdoff->setPlaceholderText("–");
    m_holdoff->setMaximumWidth( 80 );
    m_max = new QSpinBox( this );
    m_max->setRange( 0, 1000000 );
    m_max->setSpecialValueText("–");
    m_normalRow = row( this, { new QLabel( tr("holdoff"), this ), m_holdoff, new QLabel( tr("max"), this ), m_max }, false );
    m_armed = new QCheckBox( tr("Armed"), this );
    f->addRow( tr("Mode"), row( this, { m_mode, m_normalRow, m_armed } ) );

    m_name = new QLineEdit( this );
    m_name->setPlaceholderText( tr("optional: names the key frames it saves") );
    f->addRow( tr("Name"), m_name );

    m_note = new QLabel( this );
    m_note->setWordWrap( true );
    m_note->setStyleSheet("QLabel { color: palette(mid); }");
    f->addRow( m_note );

    fillParts();

    connect( m_part, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this](){ if( !m_filling ){ partChanged(); changed(); } } );
    for( QRadioButton* r : { m_kState, m_kVolt, m_kCurr } )
        connect( r, &QRadioButton::toggled, this, [this]( bool on ){ if( on && !m_filling ){ kindChanged(); changed(); } } );
    for( QComboBox* c : { m_block, m_pin, m_ref, m_when, m_edge, m_sev, m_mode } )
        connect( c, QOverload<int>::of( &QComboBox::currentIndexChanged ), this, [this](){ if( !m_filling ){ updateVisible(); changed(); } } );
    for( QComboBox* c : { m_from, m_to } )
        connect( c, &QComboBox::currentTextChanged, this, [this](){ if( !m_filling ) changed(); } );
    for( QLineEdit* e : { m_level, m_low, m_high, m_hyst, m_before, m_holdoff, m_name } )
        connect( e, &QLineEdit::textEdited, this, [this](){ if( !m_filling ) changed(); } );
    for( QCheckBox* b : { m_pause, m_keyframe, m_armed } )
        connect( b, &QCheckBox::toggled, this, [this](){ if( !m_filling ){ updateVisible(); changed(); } } );
    connect( m_max, QOverload<int>::of( &QSpinBox::valueChanged ), this, [this](){ if( !m_filling ) changed(); } );
}

void TriggerEditor::changed()
{
    QString e = error();
    m_note->setText( e );
    m_note->setVisible( !e.isEmpty() );
    emit edited();
}

void TriggerEditor::fillParts()
{
    m_filling = true;
    m_part->clear();
    if( m_fixed ) m_part->addItem( Triggers::partName( m_fixed ), m_fixed->getUid() );
    else{
        m_part->addItem( tr("(pick a part)"), "" );
        QList<Component*> parts;
        for( Component* c : *Circuit::self()->compList() )
        {
            if( c->getPins().empty() && Triggers::stateBlocks( c ).isEmpty() ) continue;
            parts.append( c );
        }
        std::sort( parts.begin(), parts.end(), []( Component* a, Component* b ){
            return Triggers::partName( a ).compare( Triggers::partName( b ), Qt::CaseInsensitive ) < 0; } );
        for( Component* c : parts ) m_part->addItem( Triggers::partName( c )+"  ("+c->itemType()+")", c->getUid() );
    }
    m_filling = false;
}

Component* TriggerEditor::part()
{
    if( m_fixed ) return m_fixed;
    return Triggers::comp( m_part->currentData().toString() );
}

void TriggerEditor::partChanged()   // What the part offers: its states, pins, pin pairs
{
    m_filling = true;
    Component* c = part();
    m_block->clear(); m_pin->clear(); m_ref->clear();
    QStringList blocks = c ? Triggers::stateBlocks( c ) : QStringList();
    for( const QString& b : blocks ) m_block->addItem( b.isEmpty() ? tr("its state") : b, b );
    if( c ){
        std::vector<Pin*> pins = c->getPins();
        bool two = pins.size() == 2;
        m_ref->addItem( tr("ground"), "" );
        for( Pin* p : pins ){
            if( !p ) continue;
            m_pin->addItem( Triggers::pinText( c, p ), Triggers::pinName( p, c ) );
            m_ref->addItem( Triggers::pinText( c, p ), Triggers::pinName( p, c ) );
        }
        if( two && m_ref->count() > 2 ) m_ref->setCurrentIndex( 2 );   // Two-pin parts: across by default
    }
    m_kState->setEnabled( !blocks.isEmpty() );
    m_kVolt->setEnabled( c && !c->getPins().empty() );
    m_kCurr->setEnabled( c && !c->getPins().empty() );
    if( m_kState->isChecked() && blocks.isEmpty() ) m_kVolt->setChecked( true );
    if( !m_kState->isChecked() && !m_kVolt->isChecked() && !m_kCurr->isChecked() )
        ( blocks.isEmpty() ? m_kVolt : m_kState )->setChecked( true );
    m_filling = false;
    kindChanged();
}

void TriggerEditor::kindChanged()
{
    m_filling = true;
    bool st = m_kState->isChecked();
    int when = m_when->currentIndex();
    m_when->clear();
    if( st ) m_when->addItems( { tr("changes"), tr("any change"), tr("reaches") } );
    else     m_when->addItems( { tr("crosses"), tr("leaves"), tr("enters") } );
    m_when->setCurrentIndex( when >= 0 && when < 3 ? when : 0 );
    if( st ){                                            // The states it reported so far, and "any"
        Component* c = part();
        QStringList seen = c ? Triggers::statesSeen( c, m_block->currentData().toString() ) : QStringList();
        for( QComboBox* cb : { m_from, m_to } ){
            QString cur = cb->currentText();
            cb->clear();
            cb->addItem("any");
            cb->addItems( seen );
            cb->setCurrentText( cur.isEmpty() ? "any" : cur );
        }
    }
    m_filling = false;
    updateVisible();
}

void TriggerEditor::updateVisible()
{
    bool st = m_kState->isChecked();
    bool cu = m_kCurr->isChecked();
    m_block->setVisible( st );
    m_pin->setVisible( !st );
    m_toLabel->setVisible( !st && !cu );
    m_ref->setVisible( !st && !cu );
    int w = m_when->currentIndex();
    m_cond->setCurrentIndex( st ? 2+qMax( 0, w ) : ( w <= 0 ? 0 : 1 ) );
    m_hystRow->setVisible( !st );
    QString unit = cu ? "A" : "V";
    m_level->setPlaceholderText( unit );
    m_low->setPlaceholderText( tr("low ")+unit );
    m_high->setPlaceholderText( tr("high ")+unit );
    m_normalRow->setVisible( m_mode->currentIndex() == 1 );
    m_before->setEnabled( m_keyframe->isChecked() );
    Component* c = part();
    bool two = c && c->getPins().size() == 2;
    if( cu && two && m_pin->count() == 2 ){              // Through a two-pin part
        m_pin->setItemText( 0, tr("through (pin 1 → pin 2)") );
        m_pin->setItemText( 1, tr("through (pin 2 → pin 1)") );
    }else if( c ){
        std::vector<Pin*> pins = c->getPins();
        for( int i=0; i<m_pin->count() && i<(int)pins.size(); ++i ) if( pins[i] ) m_pin->setItemText( i, Triggers::pinText( c, pins[i] ) );
    }
}

void TriggerEditor::setTrigger( const Trigger& t )
{
    m_t = t;
    const TrigCond& c = t.cond;
    m_filling = true;
    QString uid = c.comp;
    QString pin = c.pin;
    if( !c.node.isEmpty() ){ uid = c.node.left( c.node.lastIndexOf(':') ); pin = c.node.mid( c.node.lastIndexOf(':')+1 ); }
    if( !m_fixed ){
        int i = m_part->findData( uid );
        m_part->setCurrentIndex( i < 0 ? 0 : i );
    }
    ( c.sig == "state" ? m_kState : c.sig == "curr" ? m_kCurr : m_kVolt )->setChecked( true );
    m_filling = false;
    partChanged();
    m_filling = true;
    if( c.sig == "state" ){
        int i = m_block->findData( c.block );
        if( i >= 0 ) m_block->setCurrentIndex( i );
        kindChanged();
        m_filling = true;
        m_when->setCurrentIndex( c.change ? 1 : c.sev >= 0 ? 2 : 0 );
        m_from->setCurrentText( c.from.isEmpty() ? "any" : c.from );
        m_to->setCurrentText( c.to.isEmpty() ? "any" : c.to );
        if( c.sev >= 0 ) m_sev->setCurrentText( Triggers::levelName( c.sev ) );
    }else{
        int i = m_pin->findData( pin );
        if( i >= 0 ) m_pin->setCurrentIndex( i );
        i = m_ref->findData( c.ref );
        if( !c.refComp.isEmpty() && c.refComp != c.comp ){       // A pin of another part (made by the CLI)
            Component* rc = Triggers::comp( c.refComp );
            Pin* rp = Triggers::findPin( c.refComp, c.ref );
            m_ref->addItem( rc && rp ? Triggers::partName( rc )+" "+Triggers::pinText( rc, rp ) : c.refComp+"."+c.ref, "@ext" );
            m_ref->setCurrentIndex( m_ref->count()-1 );
        }
        else if( !c.node.isEmpty() ){                            // A node: watched at this pin
            m_ref->addItem( tr("ground (node)"), "@node" );
            m_ref->setCurrentIndex( m_ref->count()-1 );
        }
        else if( i >= 0 && !c.ref.isEmpty() ) m_ref->setCurrentIndex( i );
        else m_ref->setCurrentIndex( 0 );
        m_when->setCurrentIndex( c.kind == "window" ? ( c.outside ? 1 : 2 ) : 0 );
        m_edge->setCurrentIndex( c.edge > 0 ? 0 : c.edge < 0 ? 1 : 2 );
        QString u = c.sig == "curr" ? "A" : "V";
        bool fresh = c.comp.isEmpty() && c.node.isEmpty();
        m_level->setText( fresh ? "" : num( c.level, u ) );
        m_low->setText( fresh || c.kind != "window" ? "" : num( c.low, u ) );
        m_high->setText( fresh || c.kind != "window" ? "" : num( c.high, u ) );
        m_hyst->setText( c.hyst >= 0 ? num( c.hyst, u ) : "" );
    }
    m_pause->setChecked( t.pause );
    m_keyframe->setChecked( t.keyframe );
    m_before->setText( timeText( t.before ) );
    m_mode->setCurrentIndex( t.single ? 0 : 1 );
    m_holdoff->setText( timeText( t.holdoff ) );
    m_max->setValue( t.max );
    m_armed->setChecked( t.armed );
    m_name->setText( t.name );
    m_filling = false;
    updateVisible();
    QString e = error();
    m_note->setText( e );
    m_note->setVisible( !e.isEmpty() && !( c.comp.isEmpty() && c.node.isEmpty() ) );
}

Trigger TriggerEditor::trigger()
{
    Trigger t = m_t;
    TrigCond& c = t.cond;
    Component* p = part();
    double hyst = c.hyst;
    c = TrigCond();
    c.hyst = -1;
    c.comp = p ? p->getUid() : "";
    if( m_kState->isChecked() ){
        c.sig = "state";
        c.kind = "state";
        c.block = m_block->currentData().toString();
        int w = m_when->currentIndex();
        if( w == 1 ) c.change = true;
        else if( w == 2 ) c.sev = Triggers::levelOf( m_sev->currentText() );
        else{
            c.from = m_from->currentText().trimmed();
            c.to   = m_to->currentText().trimmed();
            if( c.from.compare( "any", Qt::CaseInsensitive ) == 0 ) c.from.clear();
            if( c.to.compare( "any", Qt::CaseInsensitive ) == 0 ) c.to.clear();
        }
    }else{
        bool cu = m_kCurr->isChecked();
        c.sig = cu ? "curr" : "volt";
        c.pin = m_pin->currentData().toString();
        if( !cu ) c.ref = m_ref->currentData().toString();
        if( c.ref == "@ext" ){ c.refComp = m_t.cond.refComp; c.ref = m_t.cond.ref; }
        else if( c.ref == "@node" ){ c.ref.clear(); c.node = c.comp+":"+c.pin; c.comp.clear(); c.pin.clear(); }
        if( c.ref == c.pin && c.refComp.isEmpty() ) c.ref.clear();
        QString u = cu ? "A" : "V";
        int w = m_when->currentIndex();
        if( w <= 0 ){
            c.kind = "edge";
            c.edge = m_edge->currentIndex() == 0 ? 1 : m_edge->currentIndex() == 1 ? -1 : 0;
            parse( m_level->text(), u, &c.level );
        }else{
            c.kind = "window";
            c.outside = w == 1;
            parse( m_low->text(), u, &c.low );
            parse( m_high->text(), u, &c.high );
        }
        if( !m_hyst->text().trimmed().isEmpty() ){ double h = 0; if( parse( m_hyst->text(), u, &h ) && h >= 0 ) c.hyst = h; }
        Q_UNUSED( hyst );
    }
    t.pause = m_pause->isChecked();
    t.keyframe = m_keyframe->isChecked();
    parseTime( m_before->text(), &t.before );
    if( !t.keyframe ) t.before = 0;
    t.single = m_mode->currentIndex() == 0;
    parseTime( m_holdoff->text(), &t.holdoff );
    t.max = m_max->value();
    if( t.single ){ t.holdoff = 0; t.max = 0; }
    t.armed = m_armed->isChecked();
    t.name = m_name->text().trimmed();
    return t;
}

QString TriggerEditor::error()
{
    if( !part() ) return tr("Pick a part.");
    if( !m_kState->isChecked() )
    {
        QString u = m_kCurr->isChecked() ? "A" : "V";
        double v = 0;
        if( m_when->currentIndex() <= 0 ){
            if( !parse( m_level->text(), u, &v ) ) return tr("Type the level, e.g. 4.5 or 350m (%1).").arg( u );
        }else{
            double lo = 0, hi = 0;
            if( !parse( m_low->text(), u, &lo ) || !parse( m_high->text(), u, &hi ) ) return tr("Type the band's low and high (%1).").arg( u );
            if( !( lo < hi ) ) return tr("The low must be below the high.");
        }
        if( !m_hyst->text().trimmed().isEmpty() && ( !parse( m_hyst->text(), u, &v ) || v < 0 ) ) return tr("The hysteresis is a value ≥ 0, or empty (auto).");
    }
    else if( m_when->currentIndex() == 0 && m_from->currentText().trimmed().compare( "any", Qt::CaseInsensitive ) == 0
                                         && m_to->currentText().trimmed().compare( "any", Qt::CaseInsensitive ) == 0 )
        return tr("any → any: choose \"any change\".");
    uint64_t ps;
    if( !parseTime( m_before->text(), &ps ) ) return tr("\"before\" is a time, e.g. 1ms.");
    if( !parseTime( m_holdoff->text(), &ps ) ) return tr("The holdoff is a time, e.g. 1ms.");
    Trigger t = trigger();
    QString e = Triggers::self()->check( t );
    return e;
}

//------------------------------------------------------------------------------------------------

TriggerDialog::TriggerDialog( QWidget* parent, const Trigger& t, bool isNew )
             : QDialog( parent )
{
    setWindowTitle( isNew ? tr("New trigger") : tr("Trigger %1").arg( t.id ) );
    QVBoxLayout* l = new QVBoxLayout( this );
    m_ed = new TriggerEditor( this );
    l->addWidget( m_ed );
    QDialogButtonBox* bb = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    l->addWidget( bb );
    connect( bb, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( bb, &QDialogButtonBox::rejected, this, &QDialog::reject );
    auto check = [this, bb](){ bb->button( QDialogButtonBox::Ok )->setEnabled( m_ed->error().isEmpty() ); };
    connect( m_ed, &TriggerEditor::edited, this, check );
    m_ed->setTrigger( t );
    check();
}
