#include <md/components/theme_switcher.h>

#include <md/components/modal.h>

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>

#include <algorithm>

namespace md {
namespace {

constexpr qreal kPadding = 16.0;
constexpr qreal kHeaderHeight = 34.0;
constexpr qreal kRowHeight = 56.0;
constexpr qreal kSwatchSize = 22.0;
constexpr qreal kSwatchGap = 10.0;

QFont fontFor(const Theme& theme, const TextStyle& style)
{
    QFont font(QStringLiteral("Roboto"));
    font.setFamilies({QStringLiteral("Roboto"), QStringLiteral("Segoe UI"),
                      QStringLiteral("Microsoft YaHei UI")});
    font.setPixelSize(qMax(9, qRound(theme.density().sp(style.sizeSp))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
    return font;
}

} // namespace

ThemeSwitcher::ThemeSwitcher(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundColour(Qt::transparent);
}

ThemeSwitcher::~ThemeSwitcher()
{
    if (selector_ != nullptr) {
        selector_->disconnect(this);
        selector_->close();
        selector_->deleteLater();
        selector_.clear();
    }
}

void ThemeSwitcher::addPreset(QString name, QColor primary, QColor second, QColor accent)
{
    const bool dark = Theme::applicationTheme().colours().surface.lightness() < 128;
    addPreset(std::move(name), Theme::fromColours(primary, second, accent, dark));
}

void ThemeSwitcher::addPreset(QString name, Theme theme)
{
    presets_.append(Preset{std::move(name), std::move(theme)});
    if (current_ < 0)
        current_ = 0;
    updateGeometry();
    requestUpdate();
}

void ThemeSwitcher::setCurrentPreset(int index)
{
    if (presets_.isEmpty())
        return;

    const int clamped = std::clamp(index, 0, int(presets_.size()) - 1);
    if (current_ == clamped)
        return;

    current_ = clamped;
    requestUpdate();
}

void ThemeSwitcher::applyPreset(int index)
{
    if (index < 0 || index >= presets_.size())
        return;

    current_ = index;
    const Theme& theme = presets_.at(index).theme;
    Theme::setApplicationTheme(theme);
    if (auto* application = qobject_cast<QApplication*>(QCoreApplication::instance()))
        application->setPalette(theme.toPalette());

    requestUpdate();
    Q_EMIT presetActivated(index);
    Q_EMIT themeApplied(theme);
}

QRectF ThemeSwitcher::presetRect(int index) const
{
    if (index < 0 || index >= presets_.size())
        return {};

    const QRectF area = contentRect();
    return QRectF(area.left() + kPadding,
                  area.top() + kHeaderHeight + index * kRowHeight,
                  qMax(qreal(0.0), area.width() - 2.0 * kPadding),
                  kRowHeight - 6.0);
}

QRectF ThemeSwitcher::swatchRect(int index, int swatch) const
{
    const QRectF row = presetRect(index);
    if (!row.isValid() || swatch < 0 || swatch > 2)
        return {};

    const qreal x = row.right() - (2 - swatch) * (kSwatchSize + kSwatchGap) - kSwatchSize;
    return QRectF(x, row.center().y() - kSwatchSize / 2.0, kSwatchSize, kSwatchSize);
}

QString ThemeSwitcher::swatchLabel(int swatch)
{
    switch (swatch) {
    case 0:
        return QStringLiteral("colorPrimary");
    case 1:
        return QStringLiteral("colorSecondary");
    default:
        return QStringLiteral("colorAccent");
    }
}

void ThemeSwitcher::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const QFont title = fontFor(theme, theme.type().titleSmall);
    const QFont label = fontFor(theme, theme.type().bodyMedium);
    const QFontMetricsF titleMetrics(title);
    const QFontMetricsF labelMetrics(label);

    qreal naturalWidth = titleMetrics.horizontalAdvance(QStringLiteral("Theme colours"));
    for (const Preset& preset : presets_)
        naturalWidth = qMax(naturalWidth, labelMetrics.horizontalAdvance(preset.name) + 160.0);

    const qreal naturalHeight = kHeaderHeight + presets_.size() * kRowHeight + 8.0;
    setMeasuredSize(QSizeF(width.resolve(naturalWidth + 2.0 * kPadding + 24.0),
                           height.resolve(naturalHeight)));
}

void ThemeSwitcher::paintContent(QPainter& painter, const QRectF& content)
{
    const Theme& theme = Theme::forWidget(this);
    const ColorScheme& colours = theme.colours();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const QFont title = fontFor(theme, theme.type().titleSmall);
    painter.setFont(title);
    painter.setPen(colours.onSurfaceVariant);
    painter.drawText(QRectF(content.left() + kPadding, content.top(),
                            content.width() - 2.0 * kPadding, kHeaderHeight),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Theme colours"));

    const QFont label = fontFor(theme, theme.type().bodyMedium);
    const QFont caption = fontFor(theme, theme.type().labelSmall);

    for (int index = 0; index < presets_.size(); ++index) {
        const Preset& preset = presets_.at(index);
        const QRectF row = presetRect(index);

        if (index == current_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colours.secondaryContainer);
            painter.drawRoundedRect(row, theme.density().dp(12.0), theme.density().dp(12.0));
        }

        painter.setFont(label);
        painter.setPen(index == current_ ? colours.onSecondaryContainer : colours.onSurface);
        painter.drawText(row.adjusted(16, 0, -110, 0), Qt::AlignLeft | Qt::AlignVCenter,
                         preset.name);

        for (int swatch = 0; swatch < 3; ++swatch) {
            const QRectF chip = swatchRect(index, swatch);
            const QColor colour = preset.theme.colour(swatchLabel(swatch));
            painter.setPen(Qt::NoPen);
            painter.setBrush(colour);
            painter.drawEllipse(chip);
            painter.setPen(QPen(colours.outlineVariant, theme.density().dp(1.0)));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(chip);
        }

        painter.setFont(caption);
        painter.setPen(index == current_ ? colours.onSecondaryContainer : colours.onSurfaceVariant);
        painter.drawText(row.adjusted(16, row.height() / 2.0, -110, 0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1 · %2 · %3")
                             .arg(preset.theme.colourPrimary().name(QColor::HexRgb),
                                  preset.theme.colourSecondary().name(QColor::HexRgb),
                                  preset.theme.colourAccent().name(QColor::HexRgb)));
    }

    painter.restore();
}

void ThemeSwitcher::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (!isEnabled())
        return;

    const int row = rowAt(event->position());
    if (row >= 0 && row < presets_.size())
        applyPreset(row);
}

void ThemeSwitcher::keyPressEvent(QKeyEvent* event)
{
    if (isEnabled() && !presets_.isEmpty()) {
        switch (event->key()) {
        case Qt::Key_Down:
            setCurrentPreset(current_ + 1);
            event->accept();
            return;
        case Qt::Key_Up:
            setCurrentPreset(current_ - 1);
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            applyPreset(current_);
            event->accept();
            return;
        default:
            break;
        }
    }

    View::keyPressEvent(event);
}

void ThemeSwitcher::changeEvent(QEvent* event)
{
    View::changeEvent(event);

    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        requestUpdate();
}

bool ThemeSwitcher::focusable() const
{
    return isEnabled();
}

void ThemeSwitcher::openSelector(QWidget* anchor)
{
    if (selector_ != nullptr) {
        selector_->close();
        return;
    }

    QWidget* host = window();
    if (host == nullptr)
        return;

    auto* modal = new Modal(host);
    auto* list = new ThemeSwitcher(modal);
    list->presets_ = presets_;
    list->current_ = current_;
    list->setFixedSize(sizeHint());

    QPointer<ThemeSwitcher> self(this);
    QObject::connect(list, &ThemeSwitcher::presetActivated, this,
                     [self, modal](int index) {
                         if (self != nullptr)
                             self->applyPreset(index);
                         modal->close();
                     });

    modal->setCard(list);
    modal->setAnchor(anchor != nullptr ? anchor : this);
    modal->setFocusReturn(anchor != nullptr ? anchor : this);

    selector_ = modal;
    connect(modal, &Modal::closeFinished, modal, &QObject::deleteLater);
    connect(modal, &QObject::destroyed, this, [this, modal] {
        if (selector_ == modal)
            selector_.clear();
    });

    modal->open();
}

bool ThemeSwitcher::isSelectorOpen() const noexcept
{
    return selector_ != nullptr && selector_->isOpen();
}

int ThemeSwitcher::rowAt(const QPointF& position) const
{
    for (int index = 0; index < presets_.size(); ++index) {
        if (presetRect(index).contains(position))
            return index;
    }
    return -1;
}

} // namespace md
