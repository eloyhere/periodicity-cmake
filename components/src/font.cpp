#include <md/components/font.h>

#include <md/components/modal.h>
#include <md/store/store.h>
#include <md/theme/theme.h>
#include <md/view/view.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QSet>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

namespace md {
namespace {

QFont tokenFont(const TextStyle& style, const Density& density)
{
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

} // namespace

namespace {

constexpr qreal kSelectorWidthDp = 380.0;
constexpr qreal kSelectorHeaderDp = 64.0;
constexpr qreal kSelectorRowDp = 48.0;
constexpr int kSelectorVisibleRows = 8;
constexpr qreal kSelectorPaddingDp = 8.0;

// The modal content of the font selector. It is a View rather than a stock
// list so it paints from the theme and follows the density; each row previews
// its family in that family's own face.
//
// No Q_OBJECT: the selection is a std::function callback, so the widget needs
// no signal of its own and the header stays free of a private meta-object.
class FontSelector final : public View
{
private:
    struct FontRow
    {
        QString label;
        QString value;
        bool placeholder = false;
    };

public:
    FontSelector(QString current, QStringList families, QWidget* parent = nullptr)
        : View(parent)
        , current_(std::move(current))
        , families_(std::move(families))
    {
        setFocusPolicy(Qt::StrongFocus);
        const Theme& theme = Theme::forWidget(this);
        setShape(theme.shapes().extraLarge);
        setElevation(Elevation::Level2);
        setAccessibleName(QCoreApplication::translate("FontSelector", "Font family"));
        rebuild();
    }

    void setOnActivated(std::function<void(QString)> handler)
    {
        onActivated_ = std::move(handler);
    }

    [[nodiscard]] QString currentFamily() const
    {
        return current_;
    }

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override
    {
        const Density& density = Theme::forWidget(this).density();
        const int visible = std::clamp(static_cast<int>(rows_.size()), 1, kSelectorVisibleRows);
        const qreal naturalWidth = density.dp(kSelectorWidthDp);
        const qreal naturalHeight = density.dp(kSelectorHeaderDp)
                                    + visible * density.dp(kSelectorRowDp)
                                    + density.dp(kSelectorPaddingDp);
        setMeasuredSize(QSizeF(width.resolve(naturalWidth), height.resolve(naturalHeight)));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        const Theme& theme = Theme::forWidget(this);
        const Density& density = theme.density();
        const ColorScheme& colours = theme.colours();

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const qreal padding = density.dp(16.0);
        const QRectF titleRect(content.left() + padding, content.top() + density.dp(8.0),
                               content.width() - 2.0 * padding, density.dp(26.0));
        painter.setFont(uiFont(theme, theme.type().titleMedium));
        painter.setPen(colours.onSurface);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QCoreApplication::translate("FontSelector", "Font family"));

        const QRectF subtitleRect(titleRect.left(), titleRect.bottom(), titleRect.width(),
                                  density.dp(20.0));
        painter.setFont(uiFont(theme, theme.type().bodySmall));
        painter.setPen(colours.onSurfaceVariant);
        painter.drawText(subtitleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         current_.isEmpty()
                             ? QCoreApplication::translate("FontSelector", "System default")
                             : current_);

        const qreal listTop = content.top() + density.dp(kSelectorHeaderDp);
        const QRectF listRect(content.left(), listTop, content.width(),
                              content.bottom() - listTop);
        painter.setClipRect(listRect);

        for (int index = 0; index < rows_.size(); ++index) {
            const QRectF row = rowRect(index);
            if (!row.intersects(listRect))
                continue;

            const bool active = index == active_;
            const bool highlighted = index == highlighted_;

            if (active || highlighted) {
                QColor fill = active ? colours.secondaryContainer : colours.onSurface;
                if (!active)
                    fill.setAlphaF(0.08f);
                painter.setPen(Qt::NoPen);
                painter.setBrush(fill);
                painter.drawRoundedRect(row, density.dp(12.0), density.dp(12.0));
            }

            const FontRow& entry = rows_.at(index);
            QFont textFont = uiFont(theme, theme.type().bodyLarge);
            textFont.setPixelSize(std::max(9, static_cast<int>(qRound(density.sp(16.0)))));
            if (!entry.placeholder && !entry.value.isEmpty())
                textFont.setFamilies({entry.value, QStringLiteral("Segoe UI"),
                                      QStringLiteral("Microsoft YaHei UI")});
            if (entry.placeholder)
                textFont.setItalic(true);

            QColor textColour = colours.onSurface;
            if (entry.placeholder)
                textColour = colours.onSurfaceVariant;
            else if (active)
                textColour = colours.onSecondaryContainer;

            painter.setFont(textFont);
            painter.setPen(textColour);
            painter.drawText(row.adjusted(density.dp(16.0), 0.0, -density.dp(44.0), 0.0),
                             Qt::AlignLeft | Qt::AlignVCenter, entry.label);

            if (active) {
                const QPointF centre(row.right() - density.dp(22.0), row.center().y());
                const qreal size = density.dp(16.0);
                QPainterPath check;
                check.moveTo(centre.x() - size * 0.42, centre.y());
                check.lineTo(centre.x() - size * 0.10, centre.y() + size * 0.32);
                check.lineTo(centre.x() + size * 0.45, centre.y() - size * 0.34);
                painter.setPen(QPen(textColour, density.dp(2.0), Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPath(check);
            }
        }

        painter.restore();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        View::mousePressEvent(event);
        if (event->button() != Qt::LeftButton)
            return;

        const int index = rowAt(event->position());
        if (index < 0)
            return;

        highlighted_ = index;
        activate(index);
        event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        switch (event->key()) {
        case Qt::Key_Down:
            moveHighlight(1);
            event->accept();
            return;
        case Qt::Key_Up:
            moveHighlight(-1);
            event->accept();
            return;
        case Qt::Key_Home:
            highlighted_ = 0;
            ensureVisible(highlighted_);
            requestUpdate();
            event->accept();
            return;
        case Qt::Key_End:
            highlighted_ = std::max(0, static_cast<int>(rows_.size()) - 1);
            ensureVisible(highlighted_);
            requestUpdate();
            event->accept();
            return;
        case Qt::Key_PageDown:
            moveHighlight(visibleRowCount());
            event->accept();
            return;
        case Qt::Key_PageUp:
            moveHighlight(-visibleRowCount());
            event->accept();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            activate(highlighted_);
            event->accept();
            return;
        default:
            break;
        }

        View::keyPressEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override
    {
        const Density& density = Theme::forWidget(this).density();
        const qreal step = density.dp(kSelectorRowDp) * 2.0;
        const qreal ticks = event->angleDelta().y() / 120.0;
        scroll_ = std::clamp(scroll_ - ticks * step, 0.0, maxScroll());
        requestUpdate();
        event->accept();
    }

    [[nodiscard]] bool focusable() const override
    {
        return true;
    }

private:
    static QFont uiFont(const Theme& theme, const TextStyle& style)
    {
        QFont font(QStringLiteral("Roboto"));
        font.setFamilies({QStringLiteral("Roboto"), QStringLiteral("Segoe UI"),
                          QStringLiteral("Microsoft YaHei UI")});
        font.setPixelSize(std::max(9, static_cast<int>(qRound(theme.density().sp(style.sizeSp)))));
        font.setWeight(static_cast<QFont::Weight>(style.weight));
        font.setLetterSpacing(QFont::AbsoluteSpacing, theme.density().sp(style.trackingSp));
        return font;
    }

    void rebuild()
    {
        rows_.clear();

        QSet<QString> seen;
        for (const QString& family : families_) {
            if (family.isEmpty() || seen.contains(family))
                continue;
            seen.insert(family);
            rows_.append(FontRow{family, family, false});
        }

        if (rows_.isEmpty()) {
            // No font database (a stripped-offscreen Qt, for example): still
            // show the current family and an explicit placeholder rather than
            // an empty box.
            rows_.append(FontRow{
                current_.isEmpty()
                    ? QCoreApplication::translate("FontSelector", "System default")
                    : current_,
                current_, false});
            rows_.append(FontRow{
                QCoreApplication::translate("FontSelector", "No fonts available"), QString(),
                true});
            active_ = 0;
        } else {
            active_ = -1;
            for (int index = 0; index < rows_.size(); ++index) {
                if (!current_.isEmpty()
                    && rows_.at(index).value.compare(current_, Qt::CaseInsensitive) == 0) {
                    active_ = index;
                    break;
                }
            }
            if (active_ < 0 && !current_.isEmpty()) {
                rows_.prepend(FontRow{current_, current_, false});
                active_ = 0;
            }
        }

        highlighted_ = active_ >= 0 ? active_ : 0;
        scroll_ = 0.0;
        ensureVisible(highlighted_);
    }

    [[nodiscard]] QRectF rowRect(int index) const
    {
        const Density& density = Theme::forWidget(this).density();
        const QRectF content = contentRect();
        const qreal padding = density.dp(kSelectorPaddingDp);
        const qreal top = content.top() + density.dp(kSelectorHeaderDp) - scroll_;
        return QRectF(content.left() + padding, top + index * density.dp(kSelectorRowDp),
                      content.width() - 2.0 * padding,
                      density.dp(kSelectorRowDp) - density.dp(4.0));
    }

    [[nodiscard]] int rowAt(const QPointF& position) const
    {
        const Density& density = Theme::forWidget(this).density();
        const qreal listTop = contentRect().top() + density.dp(kSelectorHeaderDp);
        if (position.y() < listTop)
            return -1;

        const qreal rowHeight = density.dp(kSelectorRowDp);
        const int index = static_cast<int>((position.y() - listTop + scroll_) / rowHeight);
        if (index < 0 || index >= rows_.size())
            return -1;
        return rowRect(index).contains(position) ? index : -1;
    }

    [[nodiscard]] int visibleRowCount() const
    {
        return std::min<int>(rows_.size(), kSelectorVisibleRows);
    }

    [[nodiscard]] qreal maxScroll() const
    {
        const Density& density = Theme::forWidget(this).density();
        const int hidden = rows_.size() - visibleRowCount();
        return std::max(0.0, hidden * density.dp(kSelectorRowDp));
    }

    void ensureVisible(int index)
    {
        if (index < 0)
            return;

        const Density& density = Theme::forWidget(this).density();
        const qreal rowHeight = density.dp(kSelectorRowDp);
        const qreal viewHeight = visibleRowCount() * rowHeight;
        const qreal top = index * rowHeight;
        if (top < scroll_)
            scroll_ = top;
        else if (top + rowHeight > scroll_ + viewHeight)
            scroll_ = top + rowHeight - viewHeight;
        scroll_ = std::clamp(scroll_, 0.0, maxScroll());
    }

    void moveHighlight(int delta)
    {
        if (rows_.isEmpty())
            return;

        highlighted_ = std::clamp(highlighted_ + delta, 0, static_cast<int>(rows_.size()) - 1);
        ensureVisible(highlighted_);
        requestUpdate();
    }

    void activate(int index)
    {
        if (index < 0 || index >= rows_.size() || rows_.at(index).placeholder)
            return;

        active_ = index;
        highlighted_ = index;
        current_ = rows_.at(index).value;
        if (onActivated_)
            onActivated_(current_);
        requestUpdate();
    }

    QString current_;
    QStringList families_;
    QList<FontRow> rows_;
    int active_ = -1;
    int highlighted_ = 0;
    qreal scroll_ = 0.0;
    std::function<void(QString)> onActivated_;
};

} // namespace

Font::Adapter::~Adapter() = default;

QString Font::Adapter::family(const QString& baseFamily) const
{
    return baseFamily;
}

qreal Font::Adapter::scale() const
{
    return 1.0;
}

QFont Font::Adapter::font(const TextStyle& style, const Density& density) const
{
    QFont result = tokenFont(style, density);
    const QString resolvedFamily = family(result.family());
    if (!resolvedFamily.isEmpty())
        result.setFamily(resolvedFamily);

    const qreal factor = scale();
    if (!qFuzzyCompare(factor + 1.0, 2.0))
        result.setPixelSize(std::max(1, static_cast<int>(qRound(result.pixelSize() * factor))));
    return result;
}

Font::Font(QWidget* host, std::unique_ptr<Adapter> adapter)
    : QObject(host)
    , host_(host)
    , adapter_(std::move(adapter))
{
    setObjectName(QStringLiteral("md.font"));
}

Font* Font::install(QWidget* host, std::unique_ptr<Adapter> adapter)
{
    if (host == nullptr || adapter == nullptr)
        return nullptr;

    if (Font* existing = find(host))
        delete existing;

    auto* font = new Font(host, std::move(adapter));
    font->notifyChanged();
    return font;
}

Font* Font::install(QWidget* host, const QString& family, qreal scale)
{
    if (host == nullptr)
        return nullptr;

    if (Font* existing = find(host))
        delete existing;

    auto* font = new Font(host, nullptr);
    font->family_ = family;
    font->scale_ = std::max(qreal(0.1), scale);
    font->notifyChanged();
    return font;
}

Font* Font::find(const QWidget* host)
{
    if (host == nullptr)
        return nullptr;

    for (QObject* child : host->children()) {
        if (auto* font = qobject_cast<Font*>(child))
            return font;
    }
    return nullptr;
}

const Font* Font::forWidget(const QWidget* context)
{
    for (const QWidget* widget = context; widget != nullptr; widget = widget->parentWidget()) {
        if (const Font* font = find(widget))
            return font;
    }
    return nullptr;
}

QFont Font::resolve(const QWidget* context, const TextStyle& style, const Density& density)
{
    QFont result = tokenFont(style, density);
    const Font* font = forWidget(context);
    if (font == nullptr)
        return result;

    if (font->adapter_ != nullptr)
        return font->adapter_->font(style, density);

    if (!font->family_.isEmpty())
        result.setFamily(font->family_);
    if (!qFuzzyCompare(font->scale_ + 1.0, 2.0)) {
        result.setPixelSize(
            std::max(1, static_cast<int>(qRound(result.pixelSize() * font->scale_))));
    }
    return result;
}

void Font::setAdapter(std::unique_ptr<Adapter> adapter)
{
    adapter_ = std::move(adapter);
    notifyChanged();
}

QString Font::family() const
{
    return family_;
}

void Font::setFamily(const QString& family)
{
    if (family_ == family)
        return;
    family_ = family;
    notifyChanged();
}

qreal Font::scale() const
{
    return scale_;
}

void Font::setScale(qreal scale)
{
    const qreal value = std::max(qreal(0.1), scale);
    if (qFuzzyCompare(scale_ + 1.0, value + 1.0))
        return;
    scale_ = value;
    notifyChanged();
}

void Font::notifyChanged()
{
    emit changed();

    if (host_ == nullptr)
        return;

    QEvent event(QEvent::FontChange);
    QApplication::sendEvent(host_.data(), &event);
    for (QWidget* child : host_->findChildren<QWidget*>())
        QApplication::sendEvent(child, &event);
}

Store* Font::store()
{
    return Store::open(QStringLiteral("md.font"));
}

Font* Font::installFromStore(QWidget* host)
{
    if (host == nullptr)
        return nullptr;

    Store* values = store();
    Font* font = install(host, values->value(QStringLiteral("family")).toString(),
                         values->value(QStringLiteral("scale"), 1.0).toDouble());
    if (font == nullptr)
        return nullptr;

    QObject::connect(values, &Store::changed, font, [font, values] {
        font->setFamily(values->value(QStringLiteral("family")).toString());
        font->setScale(values->value(QStringLiteral("scale"), 1.0).toDouble());
    });
    return font;
}

void Font::publish()
{
    Store* values = store();
    values->setValue(QStringLiteral("family"), family_);
    values->setValue(QStringLiteral("scale"), scale_);
}

Font::~Font()
{
    if (selector_ != nullptr) {
        selector_->disconnect(this);
        selector_->close();
        selector_->deleteLater();
        selector_.clear();
    }
}

QWidget* Font::createSelector(QWidget* parent)
{
    QPointer<Font> self(this);
    auto* selector = new FontSelector(family_, QFontDatabase::families(), parent);
    selector->setOnActivated([self](const QString& family) {
        if (self != nullptr)
            self->setFamily(family);
    });
    return selector;
}

void Font::openSelector(QWidget* anchor)
{
    if (selector_ != nullptr) {
        selector_->close();
        return;
    }

    QWidget* host = host_ != nullptr ? host_->window() : nullptr;
    if (host == nullptr)
        return;

    auto* modal = new Modal(host);
    auto* selector = static_cast<FontSelector*>(createSelector(modal));

    QPointer<Modal> guard(modal);
    QPointer<Font> self(this);
    selector->setOnActivated([self, guard](const QString& family) {
        if (self != nullptr)
            self->setFamily(family);
        if (guard != nullptr)
            guard->close();
    });

    modal->setCard(selector);
    modal->setAnchor(anchor != nullptr ? anchor : host_.data());
    modal->setFocusReturn(anchor != nullptr ? anchor : host_.data());

    selector_ = modal;
    connect(modal, &Modal::closeFinished, modal, &QObject::deleteLater);
    connect(modal, &QObject::destroyed, this, [this, modal] {
        if (selector_ == modal)
            selector_.clear();
    });

    modal->open();
}

bool Font::isSelectorOpen() const noexcept
{
    return selector_ != nullptr && selector_->isOpen();
}

} // namespace md
