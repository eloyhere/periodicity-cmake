// Periodicity — Language.

#include <md/components/language.h>

#include <md/components/font.h>
#include <md/components/modal.h>
#include <md/core/i18n.h>
#include <md/store/store.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <functional>

namespace md {

namespace {

constexpr qreal kHeightDp = 40.0;
constexpr qreal kHorizontalPaddingDp = 12.0;
constexpr qreal kIconSizeDp = 18.0;
constexpr qreal kGapDp = 8.0;
constexpr qreal kArrowSizeDp = 18.0;

// LanguagePanel's compact two-column card. Ten locales fit in five 48 dp rows.
constexpr qreal kPanelWidthDp = 360.0;
constexpr qreal kPanelPaddingDp = 16.0;
constexpr qreal kPanelHeaderHeightDp = 48.0;
constexpr qreal kPanelRowHeightDp = 48.0;
constexpr qreal kPanelColumnGapDp = 8.0;
constexpr qreal kPanelRowGapDp = 4.0;
constexpr qreal kPanelRowRadiusDp = 12.0;

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

// Native names are not themselves translated: a language selector should be
// readable to someone who speaks that language. "中文" wins over the more
// precise "简体中文" because it is the label the design brief asks for.
QString nativeNameFor(const QString& code)
{
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    if (code == QLatin1String("zh_CN"))
        return QStringLiteral("中文");
    if (code == QLatin1String("zh_TW"))
        return QStringLiteral("繁體中文");
    if (code == QLatin1String("ja"))
        return QStringLiteral("日本語");
    if (code == QLatin1String("ko"))
        return QStringLiteral("한국어");
    if (code == QLatin1String("fr"))
        return QStringLiteral("Français");
    if (code == QLatin1String("de"))
        return QStringLiteral("Deutsch");
    if (code == QLatin1String("es"))
        return QStringLiteral("Español");
    if (code == QLatin1String("ru"))
        return QStringLiteral("Русский");
    if (code == QLatin1String("pt_BR"))
        return QStringLiteral("Português (Brasil)");

    const QLocale locale(code);
    const QString native = locale.nativeLanguageName();
    return native.isEmpty() ? code : native;
}

Language::LanguageEntry makeEntry(const QString& code)
{
    return Language::LanguageEntry{code, nativeNameFor(code), QLocale(code)};
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

// The modal's card. It is View-painted (no child widgets), so its rows are
// revealed by the paint clip and can never cover a sibling. The two-column
// layout keeps all ten locales within five 48 dp rows.
class LanguagePanel final : public View
{
public:
    explicit LanguagePanel(QWidget* parent)
        : View(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        syncAppearance();
        setAccessibleName(QCoreApplication::translate("Language", "Language"));
    }

    void setEntries(const QList<Language::LanguageEntry>& entries, const QString& current)
    {
        entries_ = entries;
        current_ = current;

        focused_ = 0;
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            if (entries_.at(index).code == current_) {
                focused_ = index;
                break;
            }
        }

        hover_ = -1;
        pressed_ = -1;
        syncAccessibleDescription();
        requestUpdate();
    }

    std::function<void(const QString&)> activated;
    std::function<void()> dismissed;

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const Density& density = Theme::forWidget(this).density();
        const int rows = rowCount();
        const qreal heightDp =
            kPanelPaddingDp * 2.0 + kPanelHeaderHeightDp
            + rows * kPanelRowHeightDp + std::max(0, rows - 1) * kPanelRowGapDp;
        setMeasuredSize(QSizeF(width.resolve(density.dp(kPanelWidthDp)),
                               height.resolve(density.dp(heightDp))));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (content.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(this);
        const ColorScheme& colours = theme.colours();
        const Density& density = theme.density();

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setClipPath(shape().path(content));

        painter.setFont(fontFor(theme.type().titleLarge, density, this));
        painter.setPen(colours.onSurface);
        painter.drawText(headerRect(), static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         QCoreApplication::translate("Language", "Language"));

        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            const QRectF row = rowRect(index);
            if (row.isEmpty())
                continue;

            const bool selected = entries_.at(index).code == current_;
            const qreal radius = density.dp(kPanelRowRadiusDp);

            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colours.secondaryContainer);
                painter.drawRoundedRect(row, radius, radius);
            } else if (index == hover_ || (hasFocus() && index == focused_)) {
                painter.setPen(Qt::NoPen);
                QColor stateLayer = colours.onSurface;
                stateLayer.setAlphaF(0.08f);
                painter.setBrush(stateLayer);
                painter.drawRoundedRect(row, radius, radius);
            }

            const QFont labelFont = fontFor(theme.type().bodyLarge, density, this);
            const QFontMetricsF metrics(labelFont);
            const qreal trailing = selected ? density.dp(32.0) : density.dp(12.0);
            const QRectF textRect = row.adjusted(density.dp(16.0), 0.0,
                                                 -trailing, 0.0);

            painter.setFont(labelFont);
            painter.setPen(selected ? colours.onSecondaryContainer : colours.onSurface);
            painter.drawText(textRect, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                             metrics.elidedText(entries_.at(index).nativeName,
                                                Qt::ElideRight, textRect.width()));

            if (selected) {
                const QPointF centre(row.right() - density.dp(18.0), row.center().y());
                QPainterPath check;
                check.moveTo(centre.x() - density.dp(5.0), centre.y());
                check.lineTo(centre.x() - density.dp(1.5), centre.y() + density.dp(3.5));
                check.lineTo(centre.x() + density.dp(5.0), centre.y() - density.dp(4.0));
                painter.setPen(QPen(colours.primary, density.dp(2.0), Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(check);
            }

            if (hasFocus() && index == focused_) {
                QColor ring = colours.primary;
                ring.setAlphaF(0.9f);
                painter.setPen(QPen(ring, density.dp(1.5)));
                painter.setBrush(Qt::NoBrush);
                painter.drawRoundedRect(row.adjusted(density.dp(0.75), density.dp(0.75),
                                                     -density.dp(0.75), -density.dp(0.75)),
                                        radius, radius);
            }
        }

        painter.restore();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int index = indexAt(event->position());
        if (index != hover_) {
            hover_ = index;
            requestUpdate();
        }
        View::mouseMoveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            pressed_ = indexAt(event->position());
            if (pressed_ >= 0) {
                focused_ = pressed_;
                requestUpdate();
            }
        }
        View::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const int pressed = pressed_;
        pressed_ = -1;

        if (event->button() == Qt::LeftButton && pressed >= 0
            && indexAt(event->position()) == pressed) {
            activateIndex(pressed);
            event->accept();
            return;
        }

        View::mouseReleaseEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (entries_.isEmpty()) {
            View::keyPressEvent(event);
            return;
        }

        const int columns = columnCount();
        const int last = static_cast<int>(entries_.size()) - 1;

        switch (event->key()) {
        case Qt::Key_Up:
            focused_ = std::max(0, focused_ - columns);
            break;
        case Qt::Key_Down:
            focused_ = std::min(last, focused_ + columns);
            break;
        case Qt::Key_Left:
            focused_ = std::max(0, focused_ - 1);
            break;
        case Qt::Key_Right:
            focused_ = std::min(last, focused_ + 1);
            break;
        case Qt::Key_Home:
            focused_ = 0;
            break;
        case Qt::Key_End:
            focused_ = last;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            activateIndex(focused_);
            event->accept();
            return;
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            // The panel is the only focusable surface inside the modal, so Tab
            // stays here until the modal closes and hands focus back to the chip.
            event->accept();
            return;
        case Qt::Key_Escape:
            if (dismissed)
                dismissed();
            event->accept();
            return;
        default:
            View::keyPressEvent(event);
            return;
        }

        requestUpdate();
        event->accept();
    }

    void focusInEvent(QFocusEvent* event) override
    {
        View::focusInEvent(event);
        requestUpdate();
    }

    void focusOutEvent(QFocusEvent* event) override
    {
        View::focusOutEvent(event);
        requestUpdate();
    }

    void changeEvent(QEvent* event) override
    {
        View::changeEvent(event);
        if (event != nullptr && event->type() == QEvent::LanguageChange) {
            syncAccessibleDescription();
            requestUpdate();
        }
    }

    void onThemeChanged() override
    {
        syncAppearance();
        requestUpdate();
    }

private:
    void syncAppearance()
    {
        const Theme& theme = Theme::forWidget(this);
        setShape(theme.shapes().extraLarge);
        setElevation(Elevation::Level2);
        setBackgroundColour(theme.colours().surfaceContainerHigh);
        requestUpdate();
    }

    void syncAccessibleDescription()
    {
        QString name;
        for (const Language::LanguageEntry& entry : entries_) {
            if (entry.code == current_) {
                name = entry.nativeName;
                break;
            }
        }
        if (name.isEmpty())
            name = current_;

        setAccessibleName(QCoreApplication::translate("Language", "Language"));
        setAccessibleDescription(
            QCoreApplication::translate("Language", "Language: %1").arg(name));
    }

    [[nodiscard]] int columnCount() const
    {
        return entries_.size() > 1 ? 2 : 1;
    }

    [[nodiscard]] int rowCount() const
    {
        const int columns = columnCount();
        return (static_cast<int>(entries_.size()) + columns - 1) / columns;
    }

    [[nodiscard]] QRectF headerRect() const
    {
        const QRectF content = contentRect();
        const Density& density = Theme::forWidget(this).density();
        const qreal padding = density.dp(kPanelPaddingDp);
        return QRectF(content.left() + padding, content.top() + padding,
                      std::max<qreal>(0.0, content.width() - padding * 2.0),
                      density.dp(kPanelHeaderHeightDp));
    }

    [[nodiscard]] QRectF rowRect(int index) const
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()))
            return QRectF();

        const QRectF content = contentRect();
        const Density& density = Theme::forWidget(this).density();
        const qreal padding = density.dp(kPanelPaddingDp);
        const qreal gap = density.dp(kPanelColumnGapDp);
        const qreal rowHeight = density.dp(kPanelRowHeightDp);
        const qreal rowGap = density.dp(kPanelRowGapDp);
        const int columns = columnCount();
        const int row = index / columns;
        const int column = index % columns;
        const qreal cellWidth = std::max<qreal>(
            0.0, (content.width() - padding * 2.0 - gap * (columns - 1)) / columns);

        return QRectF(content.left() + padding + column * (cellWidth + gap),
                      content.top() + padding + density.dp(kPanelHeaderHeightDp)
                          + row * (rowHeight + rowGap),
                      cellWidth, rowHeight);
    }

    [[nodiscard]] int indexAt(const QPointF& point) const
    {
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            if (rowRect(index).contains(point))
                return index;
        }
        return -1;
    }

    void activateIndex(int index)
    {
        if (index < 0 || index >= static_cast<int>(entries_.size()))
            return;

        const QString code = entries_.at(index).code;
        if (activated)
            activated(code);
    }

    QList<Language::LanguageEntry> entries_;
    QString current_;
    int focused_ = 0;
    int hover_ = -1;
    int pressed_ = -1;
};

} // namespace

Language::Language(QWidget* parent)
    : View(parent)
{
    languages_ = defaultLanguages();
    current_ = i18n::locale();

    bool found = false;
    for (const LanguageEntry& entry : languages_) {
        if (entry.code == current_) {
            found = true;
            break;
        }
    }
    if (!found)
        current_ = languages_.isEmpty() ? QString() : languages_.first().code;

    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setAccessibleName(tr("Language"));
    syncAccessibleDescription();
    syncAppearance();
}

Language::~Language()
{
    if (scopeHost_ != nullptr
        && scopeHost_->property("md.languageScope").value<QObject*>() == this) {
        scopeHost_->setProperty("md.languageScope", QVariant());
    }
    if (modal_ != nullptr)
        delete modal_.data();
}

QList<Language::LanguageEntry> Language::defaultLanguages()
{
    QList<LanguageEntry> entries;
    const QStringList codes = i18n::availableLocales();
    entries.reserve(codes.size());
    for (const QString& code : codes)
        entries.append(makeEntry(code));
    return entries;
}

void Language::syncAccessibleDescription()
{
    setAccessibleName(tr("Language"));
    const QString name = currentEntry().nativeName;
    setAccessibleDescription(tr("Language: %1").arg(name));
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

    if (panel_ != nullptr) {
        static_cast<LanguagePanel*>(panel_.data())->setEntries(languages_, current_);
    }

    syncAccessibleDescription();
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

    // A host-installed callback owns translation loading. Otherwise the
    // component uses md::i18n, whose install() is tolerant: a locale whose .qm
    // has not been embedded returns false and leaves the globally installed
    // language alone. The selector can still present the requested locale, so
    // a host can ship translations incrementally.
    if (installer_)
        installer_(entry.code);
    else
        i18n::install(entry.code);

    if (panel_ != nullptr) {
        static_cast<LanguagePanel*>(panel_.data())->setEntries(languages_, current_);
    }
    if (menuOpen_)
        closeMenu();

    syncAccessibleDescription();
    syncAppearance();
    emit localeChanged(entry.code);
    emit languageChanged(entry.code, entry.locale);
    notifyScope();
    return true;
}

QString Language::currentLanguage() const
{
    return current_;
}

QString Language::currentLocale() const
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

void Language::showSelector()
{
    QWidget* host = window() != nullptr ? window() : parentWidget();
    if (host == nullptr)
        return;

    auto* modal = static_cast<Modal*>(modal_.data());
    if (modal == nullptr) {
        modal = new Modal(host);
        modal->setLevel(Modal::Level::Dialog);
        // Modal remembers the chip as the focus return target and runs the
        // scrim/card entrance and exit animations through ContainerTransform.
        modal->setFocusReturn(this);
        modal_ = modal;

        auto* panel = new LanguagePanel(nullptr);
        panel->activated = [this](const QString& code) { handleSelection(code); };
        panel->dismissed = [this] { closeMenu(); };
        modal->setCard(panel);
        panel_ = panel;

        QObject::connect(modal, &Modal::closed, this, [this] {
            // Modal keeps the card alive through the exit animation and hides
            // it itself; do not hide it here or the transform disappears.
            menuOpen_ = false;
            syncAppearance();
            requestUpdate();
        });
    }

    if (modal->parentWidget() != host)
        modal->setParent(host);

    auto* panel = static_cast<LanguagePanel*>(panel_.data());
    panel->setEntries(languages_, current_);

    modal->open();

    menuOpen_ = true;
    syncAppearance();
    requestUpdate();
}

void Language::handleSelection(const QString& code)
{
    setCurrentLanguage(code);
    closeMenu();
}

void Language::openMenu()
{
    if (menuOpen_) {
        closeMenu();
        return;
    }

    showSelector();
}

void Language::closeMenu()
{
    auto* modal = static_cast<Modal*>(modal_.data());
    if (modal == nullptr)
        return;

    // close() emits closed() synchronously and then runs the card/scrim exit;
    // focus is restored by Modal::restoreFocus().
    if (modal->isOpen())
        modal->close();

    if (!menuOpen_)
        return;

    menuOpen_ = false;
    syncAppearance();
    requestUpdate();
}

bool Language::isMenuOpen() const
{
    const auto* modal = static_cast<const Modal*>(modal_.data());
    return menuOpen_ && modal != nullptr && modal->isOpen();
}

QWidget* Language::selectorPanel() const
{
    return panel_.data();
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

void Language::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event != nullptr && event->type() == QEvent::LanguageChange) {
        syncAccessibleDescription();
        requestUpdate();
    }
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
