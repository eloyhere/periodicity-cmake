#include <md/components/font.h>

#include <md/store/store.h>

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QWidget>

#include <algorithm>

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

} // namespace md
