/***************************************************************************
 *   Application look: light or dark widgets, following the OS by default *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QApplication>
#include <QWidget>
#include <QSettings>
#include <QStyleFactory>
#include <QProxyStyle>
#include <QStyleOption>
#include <QIconEngine>
#include <QPainter>
#include <QPixmapCache>
#include <QRegularExpression>
#include <QTimer>
#include <functional>
#include <QEvent>

#include "uitheme.h"

#ifdef Q_OS_MACOS
bool uiMacSystemDark();             // uitheme_mac.mm
void uiMacSetAppearance( int mode );
void uiMacObserve( std::function<void()> onChange );
#endif

static const QPalette::ColorGroup s_groups[] = { QPalette::Active, QPalette::Inactive, QPalette::Disabled };
static const QPalette::ColorRole  s_fgRoles[] = { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText };
static const QPalette::ColorRole  s_bgRoles[] = { QPalette::Window, QPalette::Base, QPalette::Button, QPalette::AlternateBase };

//---------------------------------------------------------------------------
// Icons: dark-mode copy of an icon, converted pixmap by pixmap as Qt asks for them

class DarkIconEngine : public QIconEngine
{
    public:
        DarkIconEngine( const QIcon &icon ) : m_icon( icon ) {}

        void paint( QPainter* p, const QRect &rect, QIcon::Mode mode, QIcon::State state ) override
        {
            QPixmap pm = pixmap( rect.size()*p->device()->devicePixelRatioF(), mode, state );
            p->drawPixmap( rect, pm );
        }
        QPixmap pixmap( const QSize &size, QIcon::Mode mode, QIcon::State state ) override
        {
            QPixmap pm = m_icon.pixmap( size, mode, state );
            if( pm.isNull() ) return pm;
            QString key = "uidark_"+QString::number( pm.cacheKey() );
            QPixmap out;
            if( !QPixmapCache::find( key, &out ) )
            {
                out = QPixmap::fromImage( UiTheme::darkImage( pm.toImage() ) );
                out.setDevicePixelRatio( pm.devicePixelRatio() );
                QPixmapCache::insert( key, out );
            }
            return out;
        }
        QSize actualSize( const QSize &size, QIcon::Mode mode, QIcon::State state ) override
        { return m_icon.actualSize( size, mode, state ); }

        QList<QSize> availableSizes( QIcon::Mode mode, QIcon::State state ) const override
        { return m_icon.availableSizes( mode, state ); }

        QIconEngine* clone() const override { return new DarkIconEngine( m_icon ); }

    private:
        QIcon m_icon;
};

//---------------------------------------------------------------------------
// Style: Fusion, with dark-mode icons and item colours

class UiStyle : public QProxyStyle
{
    public:
        UiStyle( QStyle* base ) : QProxyStyle( base ) {}

        void drawControl( ControlElement el, const QStyleOption* opt, QPainter* p, const QWidget* w ) const override
        {
            UiTheme* t = UiTheme::self();
            if( !t->isDark() ) { QProxyStyle::drawControl( el, opt, p, w ); return; }

            if( auto o = qstyleoption_cast<const QStyleOptionViewItem*>( opt ) )
            {
                QStyleOptionViewItem v( *o );
                v.icon = t->icon( v.icon );
                bool ownBg = v.backgroundBrush.style() != Qt::NoBrush;
                if( ownBg && UiTheme::isLightBg( v.backgroundBrush.color() ) )
                {
                    v.backgroundBrush.setColor( t->bg( v.backgroundBrush.color() ) );
                    ownBg = false;
                }
                if( !ownBg )                          // Item text colours set for a light background
                    for( QPalette::ColorGroup g : s_groups )
                        v.palette.setColor( g, QPalette::Text, t->fg( v.palette.color( g, QPalette::Text ) ) );
                QProxyStyle::drawControl( el, &v, p, w );
                return;
            }
            if( auto o = qstyleoption_cast<const QStyleOptionToolButton*>( opt ) )
            {
                QStyleOptionToolButton v( *o ); v.icon = t->icon( v.icon );
                QProxyStyle::drawControl( el, &v, p, w ); return;
            }
            if( auto o = qstyleoption_cast<const QStyleOptionButton*>( opt ) )
            {
                QStyleOptionButton v( *o ); v.icon = t->icon( v.icon );
                QProxyStyle::drawControl( el, &v, p, w ); return;
            }
            if( auto o = qstyleoption_cast<const QStyleOptionMenuItem*>( opt ) )
            {
                QStyleOptionMenuItem v( *o ); v.icon = t->icon( v.icon );
                QProxyStyle::drawControl( el, &v, p, w ); return;
            }
            if( auto o = qstyleoption_cast<const QStyleOptionTab*>( opt ) )
            {
                QStyleOptionTab v( *o ); v.icon = t->icon( v.icon );
                QProxyStyle::drawControl( el, &v, p, w ); return;
            }
            if( auto o = qstyleoption_cast<const QStyleOptionComboBox*>( opt ) )
            {
                QStyleOptionComboBox v( *o ); v.currentIcon = t->icon( v.currentIcon );
                QProxyStyle::drawControl( el, &v, p, w ); return;
            }
            QProxyStyle::drawControl( el, opt, p, w );
        }
};

//---------------------------------------------------------------------------

UiTheme* UiTheme::self()
{
    static UiTheme* theme = new UiTheme();
    return theme;
}

UiTheme::UiTheme()
       : QObject( qApp )
{
    m_mode      = System;
    m_dark      = false;
    m_busy      = false;
    m_initDone  = false;
    m_settings  = nullptr;
    m_startDark = false;
}

static QPalette fullPalette( const QPalette &src ) // Every role set: never re-resolved against the OS palette
{
    QPalette p;
    for( QPalette::ColorGroup g : s_groups )
        for( int r=0; r<QPalette::NColorRoles; ++r )
            p.setBrush( g, (QPalette::ColorRole)r, src.brush( g, (QPalette::ColorRole)r ) );
    return p;
}

void UiTheme::init( QSettings* settings )
{
    m_settings = settings;
    int m = settings ? settings->value("uiTheme", 0 ).toInt() : 0;
    m_mode = ( m >= System && m <= Dark ) ? (mode_t)m : System;

    m_startDark = qApp->palette().color( QPalette::Window ).lightness() < 128;

    QApplication::setStyle( new UiStyle( QStyleFactory::create("Fusion") ) );
    m_lightPalette = fullPalette( QApplication::style()->standardPalette() );

    qApp->installEventFilter( this );
#ifdef Q_OS_MACOS
    uiMacObserve( [this](){ systemChanged(); } );  // Qt 5 does not tell widgets about it
#endif
    m_initDone = true;
    apply();
}

QString UiTheme::modeName( mode_t m )
{
    if( m == Light ) return "light";
    if( m == Dark  ) return "dark";
    return "system";
}

bool UiTheme::parseMode( QString s, mode_t &m )
{
    s = s.trimmed().toLower();
    if     ( s == "system" || s == "0" ) m = System;
    else if( s == "light"  || s == "1" ) m = Light;
    else if( s == "dark"   || s == "2" ) m = Dark;
    else return false;
    return true;
}

void UiTheme::setMode( mode_t m )
{
    m_mode = m;
    if( m_settings ) m_settings->setValue("uiTheme", (int)m );
    if( m_initDone ) apply();
}

bool UiTheme::systemDark()
{
#ifdef Q_OS_MACOS
    return uiMacSystemDark();
#else
    return m_startDark;
#endif
}

void UiTheme::apply()
{
    m_busy = true;
#ifdef Q_OS_MACOS
    uiMacSetAppearance( m_mode );   // Title bars and native dialogs match
#endif
    m_dark = ( m_mode == System ) ? systemDark() : ( m_mode == Dark );
    m_iconCache.clear();

    QApplication::setPalette( m_dark ? darkPalette() : m_lightPalette );

    // Qt only tells top-level windows, and they stop passing the palette down at widgets with
    // a style sheet: let every widget resolve it again (at start, before exec(), nothing exists yet)
    QEvent e( QEvent::ApplicationPaletteChange );
    for( QWidget* w : QApplication::allWidgets() ) QCoreApplication::sendEvent( w, &e );
    for( QWidget* w : QApplication::allWidgets() ) { adaptWidget( w ); w->update(); }
    m_busy = false;
    emit changed( m_dark );
}

void UiTheme::systemChanged()
{
    // The app's effective appearance follows the system a moment after the notification
    QTimer::singleShot( 150, this, [this](){ if( m_mode == System && systemDark() != m_dark ) apply(); } );
}

bool UiTheme::eventFilter( QObject* obj, QEvent* e )
{
    switch( e->type() )
    {
        case QEvent::Polish:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            if( !m_busy && obj->isWidgetType() )
            {
                m_busy = true;
                adaptWidget( static_cast<QWidget*>( obj ) );
                m_busy = false;
            }
            break;
        case QEvent::ThemeChange:          // OS appearance changed (platforms that send it)
            if( m_mode == System && !m_busy ) systemChanged();
            break;
        default: break;
    }
    return false;
}

//---------------------------------------------------------------------------
// Colours

bool UiTheme::isLightBg( const QColor &c )
{
    return c.alpha() > 0 && c.lightness() >= 200 && c.hslSaturation() < 110;
}

QColor UiTheme::fg( QColor c )
{
    if( !m_dark || !c.isValid() ) return c;
    int l = c.lightness();
    if( l >= 150 ) return c;
    int h = c.hslHue(), s = c.hslSaturation();
    if( s < 40 ) l = 230 - l*2/5;           // Black and greys: light grey, dimmer stays dimmer
    else         l = qMax( 165, 255-l );    // Colours: same hue, light
    return QColor::fromHsl( h, s, l, c.alpha() );
}

QColor UiTheme::bg( QColor c )
{
    if( !m_dark || !isLightBg( c ) ) return c;
    return QColor::fromHsl( c.hslHue(), c.hslSaturation(), 45+(255-c.lightness()), c.alpha() );
}

QString UiTheme::chipSheet( QColor c )
{
    QColor back = c, text = Qt::black;
    if( m_dark )
    {
        back = QColor::fromHsl( c.hslHue(), qMin( c.hslSaturation(), 160 ), 50 );
        text = c.lightness() < 170 ? QColor::fromHsl( c.hslHue(), c.hslSaturation(), 170 ) : c;
    }
    return "background-color:"+back.name()+"; color:"+text.name()+";";
}

QIcon UiTheme::icon( const QIcon &i )
{
    if( !m_dark || i.isNull() ) return i;
    qint64 key = i.cacheKey();
    auto it = m_iconCache.find( key );
    if( it != m_iconCache.end() ) return it.value();
    QIcon d( new DarkIconEngine( i ) );
    m_iconCache.insert( key, d );
    return d;
}

QImage UiTheme::darkImage( const QImage &src ) // Grey and black pixels inverted, dark colours lightened
{
    QImage img = src.convertToFormat( QImage::Format_ARGB32 );
    for( int y=0; y<img.height(); ++y )
    {
        QRgb* line = reinterpret_cast<QRgb*>( img.scanLine( y ) );
        for( int x=0; x<img.width(); ++x )
        {
            QRgb px = line[x];
            int a = qAlpha( px );
            if( a == 0 ) continue;
            QColor c( px );
            int h, s, v;
            c.getHsv( &h, &s, &v );
            if     ( s < 60  ) c.setHsv( h, s, 255-v );
            else if( v < 110 ) c.setHsv( h, s, 255-v );
            else continue;
            c.setAlpha( a );
            line[x] = c.rgba();
        }
    }
    return img;
}

QPalette UiTheme::darkPalette()
{
    QPalette p = m_lightPalette;
    auto set = [&]( QPalette::ColorRole r, QColor c, QColor dis=QColor() )
    {
        for( QPalette::ColorGroup g : s_groups ) p.setColor( g, r, c );
        if( dis.isValid() ) p.setColor( QPalette::Disabled, r, dis );
    };
    QColor text( 225, 225, 225 ), off( 120, 120, 120 );
    set( QPalette::Window,          QColor( 50, 50, 52 ) );
    set( QPalette::WindowText,      text, off );
    set( QPalette::Base,            QColor( 32, 32, 34 ), QColor( 45, 45, 47 ) );
    set( QPalette::AlternateBase,   QColor( 44, 44, 46 ) );
    set( QPalette::ToolTipBase,     QColor( 60, 60, 64 ) );
    set( QPalette::ToolTipText,     text );
    set( QPalette::PlaceholderText, QColor( 140, 140, 140 ) );
    set( QPalette::Text,            text, off );
    set( QPalette::Button,          QColor( 62, 62, 65 ) );
    set( QPalette::ButtonText,      text, off );
    set( QPalette::BrightText,      QColor( 255, 90, 90 ) );
    set( QPalette::Light,           QColor( 88, 88, 92 ) );
    set( QPalette::Midlight,        QColor( 72, 72, 76 ) );
    set( QPalette::Mid,             QColor( 44, 44, 46 ) );
    set( QPalette::Dark,            QColor( 30, 30, 32 ) );
    set( QPalette::Shadow,          QColor( 12, 12, 12 ) );
    set( QPalette::Highlight,       QColor( 48, 120, 200 ), QColor( 70, 70, 74 ) );
    set( QPalette::HighlightedText, Qt::white, off );
    set( QPalette::Link,            QColor( 100, 160, 240 ) );
    set( QPalette::LinkVisited,     QColor( 170, 130, 230 ) );
    return p;
}

//---------------------------------------------------------------------------
// Widgets that set their own colours

void UiTheme::adaptWidget( QWidget* w )
{
    if( w->property("uiThemeKeep").toBool() ) return;
    adaptPalette( w );
    adaptStyleSheet( w );
}

static bool sameExplicit( const QPalette &a, const QPalette &b ) // Same roles set, same colours in them
{
    if( a.resolve() != b.resolve() ) return false;
    for( int r=0; r<QPalette::NColorRoles; ++r )
    {
        if( !( a.resolve() & (1u<<r) ) ) continue;
        for( QPalette::ColorGroup g : s_groups )
            if( a.color( g, (QPalette::ColorRole)r ) != b.color( g, (QPalette::ColorRole)r ) ) return false;
    }
    return true;
}

QPalette UiTheme::darkVersion( const QPalette &orig )
{
    uint mask = orig.resolve();
    if( !mask ) return orig;

    bool bgSet = false;
    for( QPalette::ColorRole r : s_bgRoles )
    {
        if( !( mask & (1u<<r) ) ) continue;
        bgSet = true;
        if( !isLightBg( orig.color( QPalette::Active, r ) ) ) return orig; // Draws its own dark or coloured background
    }
    QPalette p = orig;
    for( QPalette::ColorGroup g : s_groups )
    {
        if( bgSet )
            for( QPalette::ColorRole r : s_bgRoles )
                if( mask & (1u<<r) ) p.setColor( g, r, bg( orig.color( g, r ) ) );
        for( QPalette::ColorRole r : s_fgRoles )
            if( mask & (1u<<r) ) p.setColor( g, r, fg( orig.color( g, r ) ) );
    }
    return p;
}

void UiTheme::adaptPalette( QWidget* w )
{
    QVariant vo = w->property("_uiPalOrig");
    if( !vo.isValid() && ( !m_dark || !w->testAttribute( Qt::WA_SetPalette ) ) ) return;

    QPalette cur = w->palette();
    QVariant vs  = w->property("_uiPalSet");
    QPalette orig = ( vo.isValid() && vs.isValid() && sameExplicit( cur, vs.value<QPalette>() ) )
                  ? vo.value<QPalette>()
                  : cur;                                   // New colours from the widget's own code
    QPalette want = m_dark ? darkVersion( orig ) : orig;

    w->setProperty("_uiPalOrig", QVariant::fromValue( orig ) );
    if( !sameExplicit( cur, want ) ) w->setPalette( want );
    w->setProperty("_uiPalSet", QVariant::fromValue( w->palette() ) );
}

static bool parseCssColor( QString s, QColor &c )
{
    s = s.trimmed();
    static const QRegularExpression rgb("^rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*(?:,\\s*([\\d.]+)\\s*)?\\)$");
    QRegularExpressionMatch m = rgb.match( s );
    if( m.hasMatch() )
    {
        c = QColor( m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt() );
        if( !m.captured(4).isEmpty() )
        {
            double a = m.captured(4).toDouble();
            c.setAlpha( a <= 1 ? qRound( a*255 ) : qRound( a ) );
        }
        return true;
    }
    c = QColor( s );
    return c.isValid();
}

static QString cssColor( const QColor &c )
{
    if( c.alpha() == 255 ) return c.name();
    return QString("rgba(%1, %2, %3, %4)").arg( c.red() ).arg( c.green() ).arg( c.blue() ).arg( c.alpha() );
}

static QString replaceColors( QString ss, const QRegularExpression &re, std::function<QColor(QColor)> f )
{
    QList<QRegularExpressionMatch> matches;
    auto it = re.globalMatch( ss );
    while( it.hasNext() ) matches.append( it.next() );
    for( int i=matches.size()-1; i>=0; --i )                  // From the end: offsets stay valid
    {
        QColor c;
        if( !parseCssColor( matches[i].captured(2), c ) ) continue;
        ss.replace( matches[i].capturedStart(2), matches[i].capturedLength(2), cssColor( f( c ) ) );
    }
    return ss;
}

QString UiTheme::adaptSheet( QString ss )
{
    static const QRegularExpression bgRe("(background(?:-color)?\\s*:\\s*)([^;}]+)");
    static const QRegularExpression fgRe("((?:^|[;{\\s])color\\s*:\\s*)([^;}]+)");

    auto bm = bgRe.globalMatch( ss );
    while( bm.hasNext() )                      // Any background that is not light: the sheet keeps its colours
    {
        QColor c;
        if( !parseCssColor( bm.next().captured(2), c ) || !isLightBg( c ) ) return ss;
    }
    ss = replaceColors( ss, bgRe, [this]( QColor c ){ return bg( c ); } );
    ss = replaceColors( ss, fgRe, [this]( QColor c ){ return fg( c ); } );
    return ss;
}

void UiTheme::adaptStyleSheet( QWidget* w )
{
    QString  ss = w->styleSheet();
    QVariant vo = w->property("_uiSSOrig");
    if( !vo.isValid() && ( !m_dark || ss.isEmpty() ) ) return;

    QString orig = ( vo.isValid() && w->property("_uiSSSet").toString() == ss ) ? vo.toString() : ss;
    QString want = m_dark ? adaptSheet( orig ) : orig;

    w->setProperty("_uiSSOrig", orig );
    w->setProperty("_uiSSSet",  want );
    if( want != ss ) w->setStyleSheet( want );
}
