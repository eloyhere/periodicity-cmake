// Periodicity — the Text component (framework.md §5.9, §7.5).
//
// The string is laid out with QTextLayout rather than with
// QPainter::drawText(), because wrapping and heightForWidth() have to agree
// exactly: both paths call TextBlock::run() with the same width and the same
// token line height. Line height comes from the type token, not from
// QFontMetrics, and letter spacing is applied through
// QFont::setLetterSpacing() in sp (framework.md §7.5).

#include <md/components/text.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QRectF>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>

#include <algorithm>

namespace md {

namespace {

QFont fontFor(const TextStyle& style, const Density& density)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
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

// One layout pass. A TextBlock lives on the stack of the measure or the paint
// path; QTextLayout itself is not copyable and is never cached between them.
class TextBlock
{
public:
    TextBlock(const QString& text, const QFont& font)
        : layout_(normalised(text), font)
    {
    }

    void run(qreal width, qreal lineHeight, Qt::Alignment alignment, bool wrap)
    {
        QTextOption option;
        option.setAlignment(alignment & Qt::AlignHorizontal_Mask);
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
    [[nodiscard]] int lines() const noexcept { return lines_; }

private:
    static constexpr int kMaxLines = 4096;

    QTextLayout layout_;
    qreal height_ = 0.0;
    int lines_ = 0;
};

} // namespace

Text::Text(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
}

Text::Text(const QString& text, QWidget* parent)
    : Text(parent)
{
    setText(text);
}

void Text::setText(const QString& text)
{
    if (text_ == text)
        return;

    text_ = text;
    requestUpdate();
}

QString Text::text() const
{
    return text_;
}

void Text::setRole(TextRole role)
{
    customStyle_ = false;
    if (role_ == role && explicitStyle_ == TextStyle{})
        return;

    role_ = role;
    requestUpdate();
}

Text::TextRole Text::role() const noexcept
{
    return role_;
}

void Text::setTextStyle(const TextStyle& style)
{
    explicitStyle_ = style;
    customStyle_ = true;
    requestUpdate();
}

TextStyle Text::textStyle() const
{
    return resolvedStyle();
}

void Text::setAlignment(Qt::Alignment alignment)
{
    if (alignment_ == alignment)
        return;

    alignment_ = alignment;
    requestUpdate();
}

Qt::Alignment Text::alignment() const noexcept
{
    return alignment_;
}

void Text::setWordWrap(bool wrap)
{
    if (wordWrap_ == wrap)
        return;

    wordWrap_ = wrap;
    requestUpdate();
}

bool Text::wordWrap() const noexcept
{
    return wordWrap_;
}

void Text::setColour(const QColor& colour)
{
    if (colour_ == colour)
        return;

    colour_ = colour;
    requestUpdate();
}

QColor Text::colour() const
{
    return colour_;
}

TextStyle Text::resolvedStyle() const
{
    if (customStyle_)
        return explicitStyle_;

    const TypeScale& type = Theme::forWidget(this).type();
    switch (role_) {
    case TextRole::Caption:
        return type.bodySmall;
    case TextRole::Label:
        return type.labelLarge;
    case TextRole::Body:
        return type.bodyMedium;
    case TextRole::Title:
        return type.titleMedium;
    }

    return type.bodyMedium;
}

QColor Text::resolvedColour(const TextStyle& style) const
{
    Q_UNUSED(style);

    if (colour_.isValid())
        return colour_;

    const ColorScheme& colours = Theme::forWidget(this).colours();
    switch (role_) {
    case TextRole::Caption:
    case TextRole::Label:
        return colours.onSurfaceVariant;
    case TextRole::Body:
    case TextRole::Title:
        break;
    }

    return colours.onSurface;
}

void Text::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);
    const TextStyle style = resolvedStyle();
    const QFont font = fontFor(style, theme.density());
    const qreal lineHeight = lineHeightFor(style, theme.density());
    const QFontMetricsF metrics(font);

    const QString block = normalised(text_);
    const qreal natural = block.isEmpty() ? 0.0 : metrics.horizontalAdvance(block);
    const qreal available = width.resolve(natural);

    qreal measuredHeight = lineHeight;
    if (wordWrap_ && available > 0.0 && !block.isEmpty()) {
        TextBlock layout(block, font);
        layout.run(available, lineHeight, alignment_, true);
        measuredHeight = layout.height();
    }

    setMeasuredSize(QSizeF(available, height.resolve(measuredHeight)));
}

void Text::paintContent(QPainter& painter, const QRectF& content)
{
    if (text_.isEmpty() || content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const TextStyle style = resolvedStyle();
    const QFont font = fontFor(style, theme.density());
    const qreal lineHeight = lineHeightFor(style, theme.density());
    const qreal width = std::max(content.width(), qreal(1.0));

    TextBlock block(text_, font);
    block.run(width, lineHeight, alignment_, wordWrap_);

    qreal top = content.top();
    if (alignment_.testFlag(Qt::AlignVCenter))
        top = content.center().y() - block.height() / 2.0;
    else if (alignment_.testFlag(Qt::AlignBottom))
        top = content.bottom() - block.height();

    painter.save();
    painter.setPen(resolvedColour(style));
    block.layout().draw(&painter, QPointF(content.left(), top));
    painter.restore();
}

bool Text::focusable() const
{
    return false;
}

} // namespace md
