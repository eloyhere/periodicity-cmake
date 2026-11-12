// Periodicity — Language.

#include <md/components/language.h>

#include <md/components/menu.h>
#include <md/store/store.h>
#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QVariant>
#include <QWidget>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

#include <algorithm>

namespace md {

namespace {

constexpr qreal kHeightDp = 40.0;
constexpr qreal kHorizontalPaddingDp = 12.0;
constexpr qreal kIconSizeDp = 18.0;
constexpr qreal kGapDp = 8.0;
constexpr qreal kArrowSizeDp = 18.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

Language::LanguageEntry makeEntry(const QString& code, const QString& nativeName)
{
    return Language::LanguageEntry{code, nativeName, QLocale(code)};
}

void drawGlobe(QPainter& painter, const QRectF& box, const QColor& colour,
               const Density& density)
{
    const qreal stroke = density.dp(1.5);
    QPen pen(colour, stroke);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    painter.drawEllipse(box);

    const qreal inset = box.width() * 0.28;
    painter.drawEllipse(box.adjusted(inset, 0.0, -inset, 0.0));
    painter.drawLine(QPointF(box.left(), box.center().y()),
                     QPointF(box.right(), box.center().y()));
}

} // namespace

Language::Language(QWidget* parent)
    : View(parent)
{
    languages_ = defaultLanguages();
    current_ = languages_.isEmpty() ? QString() : languages_.first().code;

    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    syncAppearance();
}

Language::~Language()
{
    if (scopeHost_ != nullptr
        && scopeHost_->property("md.languageScope").value<QObject*>() == this) {
        scopeHost_->setProperty("md.languageScope", QVariant());
    }
    if (menu_ != nullptr)
        delete menu_.data();
}

QList<Language::LanguageEntry> Language::defaultLanguages()
{
    return {
        makeEntry(QStringLiteral("en"), QStringLiteral("English")),
        makeEntry(QStringLiteral("zh_CN"), QStringLiteral("简体中文")),
        makeEntry(QStringLiteral("zh_TW"), QStringLiteral("繁體中文")),
        makeEntry(QStringLiteral("ja"), QStringLiteral("日本語")),
        makeEntry(QStringLiteral("ko"), QStringLiteral("한국어")),
        makeEntry(QStringLiteral("fr"), QStringLiteral("Français")),
        makeEntry(QStringLiteral("de"), QStringLiteral("Deutsch")),
        makeEntry(QStringLiteral("es"), QStringLiteral("Español")),
        makeEntry(QStringLiteral("ru"), QStringLiteral("Русский")),
        makeEntry(QStringLiteral("pt_BR"), QStringLiteral("Português (Brasil)")),
    };
}

void Language::syncAppearance()
{
    const Theme& theme = Theme::forWidget(this);
    setShape(theme.shapes().full);
    setElevation(Elevation::Level0);

    if (menuOpen_) {
        setBackgroundColour(theme.colours().secondaryContainer);
        setContentColour(theme.colours().onSecondaryContainer);
    } else {
        setBackgroundColour(theme.colours().surfaceContainerHigh);
        setContentColour(theme.colours().onSurface);
    }

    requestUpdate();
}

void Language::setLanguages(const QList<LanguageEntry>& languages)
{
    languages_ = languages;

    bool found = false;
    for (const LanguageEntry& entry : languages_) {
        if (entry.code == current_) {
            found = true;
            break;
        }
    }

    if (!found)
        current_ = languages_.isEmpty() ? QString() : languages_.first().code;

    if (menuOpen_)
        closeMenu();
    else if (menu_ != nullptr)
        menu_->close();

    syncAppearance();
}

QList<Language::LanguageEntry> Language::languages() const
{
    return languages_;
}

bool Language::setCurrentLanguage(const QString& code)
{
    int index = -1;
    for (int i = 0; i < static_cast<int>(languages_.size()); ++i) {
        if (languages_.at(i).code == code) {
            index = i;
            break;
        }
    }

    if (index < 0)
        return false;

    if (current_ == code)
        return true;

    current_ = code;
    const LanguageEntry entry = languages_.at(index);

    if (menuOpen_) {
        rebuildMenu();
        if (menu_ != nullptr)
            menu_->close();
    }

    syncAppearance();
    emit languageChanged(entry.code, entry.locale);

    if (installer_)
        installer_(entry.code);

    notifyScope();
    return true;
}

QString Language::currentLanguage() const
{
    return current_;
}

Language::LanguageEntry Language::currentEntry() const
{
    for (const LanguageEntry& entry : languages_) {
        if (entry.code == current_)
            return entry;
    }

    return LanguageEntry{current_, current_, QLocale(current_)};
}

void Language::setTranslatorInstaller(std::function<void(const QString& code)> installer)
{
    installer_ = std::move(installer);
}

void Language::setIcon(const QIcon& icon)
{
    icon_ = icon;
    requestUpdate();
}

QIcon Language::icon() const
{
    return icon_;
}

void Language::rebuildMenu()
{
    if (menu_ == nullptr)
        return;

    menu_->clear();

    for (const LanguageEntry& entry : languages_) {
        Menu::MenuItem item;
        item.text = entry.nativeName;
        item.checkable = true;
        item.checked = entry.code == current_;
        menu_->addItem(item);
    }
}

void Language::openMenu()
{
    if (menuOpen_) {
        closeMenu();
        return;
    }

    QWidget* host = window() != nullptr ? window() : parentWidget();
    if (host == nullptr)
        return;

    if (menu_ == nullptr) {
        menu_ = new Menu(host);
        QObject::connect(menu_, &Menu::itemTriggered, this,
                         [this](int index, const QString& text) {
                             Q_UNUSED(text);
                             handleMenuTrigger(index);
                         });
        QObject::connect(menu_, &Menu::closed, this, [this] {
            menuOpen_ = false;
            syncAppearance();
            requestUpdate();
        });
    } else if (menu_->parentWidget() != host) {
        menu_->setParent(host);
    }

    rebuildMenu();
    menu_->setAnchor(this);
    menu_->open();

    if (menu_->isOpen()) {
        menuOpen_ = true;
        syncAppearance();
        requestUpdate();
    }
}

void Language::closeMenu()
{
    if (menu_ != nullptr && menu_->isOpen())
        menu_->close();

    if (!menuOpen_)
        return;

    menuOpen_ = false;
    syncAppearance();
    requestUpdate();
}

bool Language::isMenuOpen() const
{
    return menuOpen_ && menu_ != nullptr && menu_->isOpen();
}

Menu* Language::menu() const
{
    return menu_.data();
}

void Language::handleMenuTrigger(int index)
{
    if (index < 0 || index >= static_cast<int>(languages_.size()))
        return;

    const QString code = languages_.at(index).code;

    if (menu_ != nullptr)
        menu_->close();

    setCurrentLanguage(code);
}

void Language::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    const QFont labelFont = fontFor(theme.type().labelLarge, density, this);
    const QFontMetricsF metrics(labelFont);

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal gap = density.dp(kGapDp);
    const qreal arrow = density.dp(kArrowSizeDp);
    const qreal textWidth = metrics.horizontalAdvance(currentEntry().nativeName);

    const qreal naturalWidth =
        padding * 2.0 + iconSize + gap + textWidth + gap + arrow;

    setMeasuredSize(QSizeF(width.resolve(naturalWidth),
                           height.resolve(density.dp(kHeightDp))));
}

void Language::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();

    const qreal padding = density.dp(kHorizontalPaddingDp);
    const qreal iconSize = density.dp(kIconSizeDp);
    const qreal gap = density.dp(kGapDp);
    const qreal arrow = density.dp(kArrowSizeDp);

    const QColor foreground =
        menuOpen_ ? colours.onSecondaryContainer : colours.onSurface;
    const QColor secondary =
        menuOpen_ ? colours.onSecondaryContainer : colours.onSurfaceVariant;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setClipPath(shape().path(content));

    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape().path(content));

    const QRectF iconRect(content.left() + padding,
                          content.center().y() - iconSize / 2.0, iconSize, iconSize);

    if (icon_.isNull()) {
        drawGlobe(painter, iconRect, foreground, density);
    } else {
        icon_.paint(&painter, iconRect.toRect(), Qt::AlignCenter, QIcon::Normal,
                    QIcon::Off);
    }

    const QRectF arrowRect(content.right() - padding - arrow,
                           content.center().y() - arrow / 2.0, arrow, arrow);

    QPen arrowPen(secondary, density.dp(1.8));
    arrowPen.setCapStyle(Qt::RoundCap);
    arrowPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(arrowPen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath chevron;
    chevron.moveTo(arrowRect.left() + arrowRect.width() * 0.3,
                   arrowRect.top() + arrowRect.height() * 0.38);
    chevron.lineTo(arrowRect.left() + arrowRect.width() * 0.5,
                   arrowRect.top() + arrowRect.height() * 0.6);
    chevron.lineTo(arrowRect.left() + arrowRect.width() * 0.7,
                   arrowRect.top() + arrowRect.height() * 0.38);
    painter.drawPath(chevron);

    const qreal textLeft = iconRect.right() + gap;
    const qreal textRight = arrowRect.left() - gap;
    const qreal textWidth = std::max(textRight - textLeft, qreal(0.0));

    if (textWidth > 0.0) {
        const QFont labelFont = fontFor(theme.type().labelLarge, density, this);
        const QFontMetricsF metrics(labelFont);
        const QString label =
            metrics.elidedText(currentEntry().nativeName, Qt::ElideRight, textWidth);

        painter.setFont(labelFont);
        painter.setPen(foreground);
        painter.drawText(QRectF(textLeft, content.top(), textWidth, content.height()),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), label);
    }

    painter.restore();
}

void Language::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() == Qt::LeftButton)
        pressed_ = true;
}

void Language::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    const bool activate = pressed_ && event->button() == Qt::LeftButton
        && QRectF(rect()).contains(event->position());
    pressed_ = false;

    if (!activate)
        return;

    if (menuOpen_)
        closeMenu();
    else
        openMenu();
}

void Language::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
    case Qt::Key_Down:
    case Qt::Key_Up:
        openMenu();
        event->accept();
        return;

    case Qt::Key_Escape:
        if (menuOpen_)
            closeMenu();
        event->accept();
        return;

    default:
        break;
    }

    View::keyPressEvent(event);
}

void Language::onThemeChanged()
{
    syncAppearance();
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Configuration container
// ---------------------------------------------------------------------------

Language::Adapter::~Adapter() = default;

QString Language::Adapter::text(const QString& key) const
{
    Q_UNUSED(key);
    return QString();
}

QString Language::Adapter::locale() const
{
    return {};
}

Qt::LayoutDirection Language::Adapter::direction() const
{
    return QApplication::layoutDirection();
}

QVariantMap Language::Adapter::strings() const
{
    return {};
}

void Language::setAdapter(std::unique_ptr<Adapter> adapter)
{
    adapter_ = std::move(adapter);

    if (Store* values = store(); values != nullptr && adapter_ != nullptr)
        values->setValue(QStringLiteral("strings"), adapter_->strings());

    notifyScope();
}

void Language::installOn(QWidget* host)
{
    if (host == nullptr)
        return;

    if (scopeHost_ != nullptr
        && scopeHost_->property("md.languageScope").value<QObject*>() == this) {
        scopeHost_->setProperty("md.languageScope", QVariant());
    }

    scopeHost_ = host;
    host->setProperty("md.languageScope", QVariant::fromValue<QObject*>(this));
    notifyScope();
}

const Language* Language::forWidget(const QWidget* context)
{
    for (const QWidget* widget = context; widget != nullptr; widget = widget->parentWidget()) {
        const QVariant value = widget->property("md.languageScope");
        if (!value.isValid())
            continue;
        if (const auto* language = qobject_cast<Language*>(value.value<QObject*>()))
            return language;
    }
    return nullptr;
}

QString Language::translate(const QWidget* context, const QString& key, const QString& fallback)
{
    const Language* language = forWidget(context);
    if (language != nullptr && language->adapter_ != nullptr) {
        const QString translated = language->adapter_->text(key);
        if (!translated.isEmpty())
            return translated;
    }
    return fallback.isEmpty() ? key : fallback;
}

Qt::LayoutDirection Language::layoutDirectionFor(const QWidget* context)
{
    const Language* language = forWidget(context);
    if (language != nullptr && language->adapter_ != nullptr)
        return language->adapter_->direction();
    return QApplication::layoutDirection();
}

void Language::notifyScope()
{
    if (scopeHost_ == nullptr)
        return;

    if (adapter_ != nullptr)
        scopeHost_->setLayoutDirection(adapter_->direction());

    QEvent event(QEvent::LanguageChange);
    QApplication::sendEvent(scopeHost_.data(), &event);
    for (QWidget* child : scopeHost_->findChildren<QWidget*>())
        QApplication::sendEvent(child, &event);
}

Store* Language::store()
{
    return Store::open(QStringLiteral("md.language"));
}

Language* Language::installFromStore(QWidget* host)
{
    if (host == nullptr)
        return nullptr;

    auto* language = new Language(host);
    Store* values = store();

    language->installOn(host);

    const QString code = values->value(QStringLiteral("code")).toString();
    if (!code.isEmpty())
        language->setCurrentLanguage(code);

    QObject::connect(language, &Language::languageChanged, values,
                     [values](const QString& selected, const QLocale&) {
                         values->setValue(QStringLiteral("code"), selected);
                     });
    QObject::connect(values, &Store::valueChanged, language,
                     [language](const QString& key, const QVariant& value) {
                         if (key == QStringLiteral("code"))
                             language->setCurrentLanguage(value.toString());
                     });
    return language;
}

void Language::publish()
{
    store()->setValue(QStringLiteral("code"), current_);
}

} // namespace md
