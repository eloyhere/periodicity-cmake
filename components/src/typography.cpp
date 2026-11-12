// Periodicity — Typography, the full design baseline type-scale display component.
//
// The layout is deliberately the same algorithm as Text: one QTextLayout pass
// with the token line height, so measure and paint cannot disagree and
// heightForWidth() is authoritative.

#include <md/components/typography.h>

#include <md/components/font.h>
#include <md/store/store.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include <algorithm>

namespace md {

namespace {

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

qreal lineHeightFor(const TextStyle& style, const Density& density)
{
    return density.sp(style.lineHeightSp);
}

// QTextLayout is a single paragraph, so a newline has to become the Unicode
// line separator before the layout can honour it.
QString normalised(const QString& text)
{
    QString result = text;
    result.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
    return result;
}

// The width of the widest hard line, not the advance of the whole block: a
// multi-line string measured as one run reports the sum of its lines, which
// made sizeHint() and the wrap width too large.
qreal naturalWidth(const QFontMetricsF& metrics, const QString& normalisedText)
{
    const QChar separator(QChar::LineSeparator);
    qreal width = 0.0;
    int start = 0;
    for (;;) {
        const int end = normalisedText.indexOf(separator, start);
        const QString line = end < 0 ? normalisedText.mid(start)
                                     : normalisedText.mid(start, end - start);
        width = std::max(width, metrics.horizontalAdvance(line));
        if (end < 0)
            break;
        start = end + 1;
    }

    return width;
}

// AlignLeft is the leading edge and mirrors in a right-to-left widget. The
// physical flag is paired with AlignAbsolute so the layout engine cannot mirror
// it a second time.
Qt::Alignment physicalHorizontalAlignment(Qt::Alignment alignment,
                                          Qt::LayoutDirection direction)
{
    Qt::Alignment result = alignment & Qt::AlignHorizontal_Mask;

    if (direction == Qt::RightToLeft && !alignment.testFlag(Qt::AlignAbsolute)) {
        const bool left = result.testFlag(Qt::AlignLeft);
        const bool right = result.testFlag(Qt::AlignRight);
        if (left != right) {
            result &= ~(Qt::AlignLeft | Qt::AlignRight);
            result |= left ? Qt::AlignRight : Qt::AlignLeft;
        }
    }

    return result | Qt::AlignAbsolute;
}

// One layout pass. The block lives on the stack of the measure or the paint
// path; QTextLayout is not copyable and is never cached between them.
class TypeBlock
{
public:
    TypeBlock(const QString& text, const QFont& font)
        : layout_(normalised(text), font)
    {
    }

    void run(qreal width, qreal lineHeight, Qt::Alignment alignment, bool wrap)
    {
        QTextOption option;
        option.setAlignment((alignment & Qt::AlignHorizontal_Mask)
                            | (alignment & Qt::AlignAbsolute));
        option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere
                                : QTextOption::NoWrap);
        layout_.setTextOption(option);

        layout_.beginLayout();
        qreal y = 0.0;
        for (;;) {
            QTextLine line = layout_.createLine();
            if (!line.isValid())
                break;

            line.setLineWidth(width);
            line.setPosition(QPointF(0.0, y));
            y += lineHeight;
            ++lines_;

            // A pathological width (zero, or a one-pixel column with a very
            // long string) must never turn into an unbounded loop.
            if (lines_ >= kMaxLines)
                break;
        }
        layout_.endLayout();
        height_ = y;
    }

    [[nodiscard]] QTextLayout& layout() noexcept { return layout_; }
    [[nodiscard]] qreal height() const noexcept { return height_; }

private:
    static constexpr int kMaxLines = 4096;

    QTextLayout layout_;
    qreal height_ = 0.0;
    int lines_ = 0;
};

} // namespace

Typography::Typography(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
}

Typography::Typography(const QString& text, QWidget* parent)
    : Typography(parent)
{
    setText(text);
}

Typography::Typography(const QString& text, TypeRole role, QWidget* parent)
    : Typography(parent)
{
    setRole(role);
    setText(text);
}

void Typography::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    requestUpdate();
}

QString Typography::text() const
{
    return text_;
}

void Typography::setRole(TypeRole role)
{
    const bool wasCustom = customStyle_;
    customStyle_ = false;
    if (role_ == role && !wasCustom)
        return;

    role_ = role;
    requestUpdate();
}

Typography::TypeRole Typography::role() const noexcept
{
    return role_;
}

void Typography::setRole(Text::TextRole role)
{
    const TypeRole mapped = typeRoleFor(role);
    const bool wasCustom = customStyle_;
    customStyle_ = false;
    if (role_ == mapped && !wasCustom)
        return;

    role_ = mapped;
    requestUpdate();
}

Text::TextRole Typography::textRole() const
{
    switch (role_) {
    case TypeRole::BodySmall:
        return Text::TextRole::Caption;
    case TypeRole::LabelLarge:
    case TypeRole::LabelMedium:
    case TypeRole::LabelSmall:
        return Text::TextRole::Label;
    case TypeRole::TitleLarge:
    case TypeRole::TitleMedium:
    case TypeRole::TitleSmall:
        return Text::TextRole::Title;
    case TypeRole::BodyLarge:
    case TypeRole::BodyMedium:
        return Text::TextRole::Body;
    case TypeRole::DisplayLarge:
    case TypeRole::DisplayMedium:
    case TypeRole::DisplaySmall:
    case TypeRole::HeadlineLarge:
    case TypeRole::HeadlineMedium:
    case TypeRole::HeadlineSmall:
        break;
    }

    // Display and headline text is title-strength content.
    return Text::TextRole::Title;
}

TextStyle Typography::styleFor(TypeRole role, const TypeScale& scale)
{
    switch (role) {
    case TypeRole::DisplayLarge:
        return scale.displayLarge;
    case TypeRole::DisplayMedium:
        return scale.displayMedium;
    case TypeRole::DisplaySmall:
        return scale.displaySmall;
    case TypeRole::HeadlineLarge:
        return scale.headlineLarge;
    case TypeRole::HeadlineMedium:
        return scale.headlineMedium;
    case TypeRole::HeadlineSmall:
        return scale.headlineSmall;
    case TypeRole::TitleLarge:
        return scale.titleLarge;
    case TypeRole::TitleMedium:
        return scale.titleMedium;
    case TypeRole::TitleSmall:
        return scale.titleSmall;
    case TypeRole::BodyLarge:
        return scale.bodyLarge;
    case TypeRole::BodyMedium:
        return scale.bodyMedium;
    case TypeRole::BodySmall:
        return scale.bodySmall;
    case TypeRole::LabelLarge:
        return scale.labelLarge;
    case TypeRole::LabelMedium:
        return scale.labelMedium;
    case TypeRole::LabelSmall:
        return scale.labelSmall;
    }

    return scale.bodyMedium;
}

Typography::TypeRole Typography::typeRoleFor(Text::TextRole role)
{
    switch (role) {
    case Text::TextRole::Caption:
        return TypeRole::BodySmall;
    case Text::TextRole::Label:
        return TypeRole::LabelLarge;
    case Text::TextRole::Title:
        return TypeRole::TitleMedium;
    case Text::TextRole::Body:
        break;
    }

    return TypeRole::BodyMedium;
}

void Typography::setTextStyle(const TextStyle& style)
{
    explicitStyle_ = style;
    customStyle_ = true;
    requestUpdate();
}

TextStyle Typography::textStyle() const
{
    return resolvedStyle();
}

void Typography::setAlignment(Qt::Alignment alignment)
{
    if (alignment_ == alignment)
        return;

    alignment_ = alignment;
    requestUpdate();
}

Qt::Alignment Typography::alignment() const noexcept
{
    return alignment_;
}

void Typography::setWordWrap(bool wrap)
{
    if (wordWrap_ == wrap)
        return;

    wordWrap_ = wrap;
    requestUpdate();
}

bool Typography::wordWrap() const noexcept
{
    return wordWrap_;
}

void Typography::setColour(const QColor& colour)
{
    if (colour_ == colour)
        return;

    colour_ = colour;
    requestUpdate();
}

QColor Typography::colour() const
{
    return colour_;
}

TextStyle Typography::resolvedStyle() const
{
    if (customStyle_)
        return explicitStyle_;

    return styleFor(role_, Theme::forWidget(this).type());
}

bool Typography::focusable() const
{
    return false;
}

QColor Typography::resolvedColour() const
{
    if (colour_.isValid())
        return colour_;

    const ColorScheme& colours = Theme::forWidget(this).colours();

    switch (role_) {
    case TypeRole::LabelLarge:
    case TypeRole::LabelMedium:
    case TypeRole::LabelSmall:
        return colours.onSurfaceVariant;
    default:
        break;
    }

    return colours.onSurface;
}

void Typography::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const TextStyle style = resolvedStyle();
    const QFont font = fontFor(style, theme.density(), this);
    const qreal lineHeight = lineHeightFor(style, theme.density());
    const QFontMetricsF metrics(font);

    const qreal natural = text_.isEmpty() ? 0.0 : naturalWidth(metrics, normalised(text_));
    const qreal available = width.resolve(natural);
    const Qt::Alignment horizontal =
        physicalHorizontalAlignment(alignment_, layoutDirection());

    // Hard line breaks split the paragraph even with word wrap off, so the
    // height always comes from the block layout and the block wraps at the
    // available width rather than at the sizeHint width.
    qreal measuredHeight = lineHeight;
    if (!text_.isEmpty() && (available > 0.0 || !wordWrap_)) {
        TypeBlock layout(text_, font);
        layout.run(available, lineHeight, horizontal, wordWrap_);
        measuredHeight = layout.height();
    }

    setMeasuredSize(QSizeF(available, height.resolve(measuredHeight)));
}

void Typography::paintContent(QPainter& painter, const QRectF& content)
{
    if (text_.isEmpty() || content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const TextStyle style = resolvedStyle();
    const QFont font = fontFor(style, theme.density(), this);
    const qreal lineHeight = lineHeightFor(style, theme.density());
    const qreal width = std::max(content.width(), qreal(1.0));
    const Qt::Alignment horizontal =
        physicalHorizontalAlignment(alignment_, layoutDirection());

    TypeBlock block(text_, font);
    block.run(width, lineHeight, horizontal, wordWrap_);

    qreal top = content.top();
    if (alignment_.testFlag(Qt::AlignVCenter))
        top = content.center().y() - block.height() / 2.0;
    else if (alignment_.testFlag(Qt::AlignBottom))
        top = content.bottom() - block.height();

    painter.save();
    painter.setPen(resolvedColour());
    block.layout().draw(&painter, QPointF(content.left(), top));
    painter.restore();
}

void Typography::onStoreChanged(Store* store, const QString& key, const QVariant& value)
{
    View::onStoreChanged(store, key, value);

    if (key != QStringLiteral("font.family") && key != QStringLiteral("font.scale"))
        return;

    const QString family = store->value(QStringLiteral("font.family")).toString();
    const qreal scale = store->value(QStringLiteral("font.scale"), 1.0).toDouble();
    if (Font* font = Font::find(this)) {
        font->setFamily(family);
        font->setScale(scale);
    } else {
        Font::install(this, family, scale);
    }
}

} // namespace md
