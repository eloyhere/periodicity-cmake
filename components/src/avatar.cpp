// Periodicity — Avatar (media/content batch).
//
// The component resolves exactly one source — raster image, icon, base64 or
// url — and paints it with a cover crop clipped to the avatar shape. The url
// path is asynchronous: QNetworkAccessManager owns the reply and the finished
// handler decodes off the paint path, guarded by a generation counter so a
// reply from a previous url can never overwrite a newer source. A data: url is
// decoded in place, and an injected loader replaces the network entirely.
//
// The placeholder is not a grey rectangle: its fill is chosen by hashing the
// initials against the theme's container roles, which keeps one identity one
// colour across reloads while staying inside the M3 palette.

#include <md/components/avatar.h>

#include <md/theme/theme.h>

#include <QFont>
#include <QFontMetricsF>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSizeF>

#include <algorithm>
#include <utility>

namespace md {

namespace {

constexpr int kPlaceholderRoleCount = 6;

// A container colour and the on-colour that M3 pairs with it. The placeholder
// used to paint on-surface over every container, which is a contrast guess: in
// the light scheme primary-container wants on-primary-container, and the same
// holds for the secondary, tertiary and error containers.
struct PlaceholderTint
{
    QColor fill;
    QColor ink;
};

PlaceholderTint placeholderTint(const ColorScheme& colours, const QString& key,
                                const QColor& overrideColour)
{
    if (overrideColour.isValid())
        return {overrideColour, colours.onSurface};

    const QColor fills[kPlaceholderRoleCount] = {
        colours.primaryContainer,      colours.secondaryContainer,
        colours.tertiaryContainer,     colours.errorContainer,
        colours.surfaceContainerHigh,  colours.surfaceContainerHighest,
    };
    // The two neutral containers carry on-surface content, as in a list row.
    const QColor inks[kPlaceholderRoleCount] = {
        colours.onPrimaryContainer,     colours.onSecondaryContainer,
        colours.onTertiaryContainer,    colours.onErrorContainer,
        colours.onSurface,              colours.onSurface,
    };

    const size_t hash = key.isEmpty() ? size_t(0) : qHash(key);
    const int index = std::clamp(
        static_cast<int>(hash % static_cast<size_t>(kPlaceholderRoleCount)), 0,
        kPlaceholderRoleCount - 1);
    return {fills[index], inks[index]};
}

QFont initialsFont(const Theme& theme, qreal boxSide)
{
    const TextStyle& style = theme.type().titleMedium;
    QFont font(style.family);
    font.setPixelSize(std::max(1, static_cast<int>(qRound(boxSide * 0.36))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    return font;
}

// Strips an optional data: URL header and any whitespace, then decodes the
// remaining base64 payload. A null image means the string is not an image.
QImage imageFromBase64Text(QString data)
{
    data = data.trimmed();
    if (data.isEmpty())
        return QImage();

    if (data.startsWith(QLatin1String("data:"), Qt::CaseInsensitive)) {
        const qsizetype comma = data.indexOf(QLatin1Char(','));
        if (comma < 0)
            return QImage();

        const QString header = data.left(comma);
        if (!header.contains(QLatin1String(";base64"), Qt::CaseInsensitive))
            return QImage();

        data = data.mid(comma + 1);
    }

    QString compact;
    compact.reserve(data.size());
    for (const QChar character : data) {
        if (!character.isSpace())
            compact.append(character);
    }

    if (compact.isEmpty())
        return QImage();

    // Base64Encoding tolerates padding and ignores characters outside the
    // alphabet; the image decoder is the final arbiter, so a malformed payload
    // yields a null image rather than a crash.
    const QByteArray bytes =
        QByteArray::fromBase64(compact.toLatin1(), QByteArray::Base64Encoding);
    return QImage::fromData(bytes);
}

QImage imageFromDataUrl(const QUrl& url)
{
    return imageFromBase64Text(url.toString());
}

} // namespace

Avatar::Avatar(QWidget* parent)
    : View(parent)
{
    // The avatar paints its own shape; the background layer must not add a
    // rectangular surface behind it.
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
}

Avatar::Avatar(const QString& initials, QWidget* parent)
    : Avatar(parent)
{
    setInitials(initials);
}

Avatar::~Avatar()
{
    cancelTransfer();
}

void Avatar::setShapeMode(ShapeMode mode)
{
    if (mode_ == mode)
        return;

    mode_ = mode;
    requestUpdate();
}

Avatar::ShapeMode Avatar::shapeMode() const noexcept
{
    return mode_;
}

void Avatar::setDiameter(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(diameterDp_ + 1.0, clamped + 1.0))
        return;

    diameterDp_ = clamped;
    updateGeometry();
    requestUpdate();
}

qreal Avatar::diameter() const noexcept
{
    return diameterDp_;
}

void Avatar::setBorderWidth(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(borderWidthDp_ + 1.0, clamped + 1.0))
        return;

    borderWidthDp_ = clamped;
    requestUpdate();
}

qreal Avatar::borderWidth() const noexcept
{
    return borderWidthDp_;
}

void Avatar::setBorderColour(const QColor& colour)
{
    if (borderColour_ == colour)
        return;

    borderColour_ = colour;
    requestUpdate();
}

QColor Avatar::borderColour() const
{
    return borderColour_;
}

void Avatar::setIcon(const QIcon& icon)
{
    cancelTransfer();
    setLoading(false);

    if (icon_.cacheKey() == icon.cacheKey() && image_.isNull() && base64_.isEmpty()
        && url_.isEmpty()) {
        return;
    }

    icon_ = icon;
    image_ = QImage();
    base64_.clear();
    url_ = QUrl();

    emit imageChanged();
    requestUpdate();
}

QIcon Avatar::icon() const
{
    return icon_;
}

void Avatar::setImage(const QImage& image)
{
    cancelTransfer();
    setLoading(false);

    image_ = image;
    icon_ = QIcon();
    base64_.clear();
    url_ = QUrl();

    emit imageChanged();
    requestUpdate();
}

QImage Avatar::image() const
{
    return image_;
}

void Avatar::setPixmap(const QPixmap& pixmap)
{
    setImage(pixmap.toImage());
}

void Avatar::setBase64(const QString& encoded)
{
    if (base64_ == encoded && image_.isNull() && icon_.isNull() && url_.isEmpty())
        return;

    cancelTransfer();
    setLoading(false);

    base64_ = encoded;
    icon_ = QIcon();
    url_ = QUrl();
    // An invalid payload yields a null image: the placeholder is the fallback,
    // exactly as for a failed download, but nothing was requested so no
    // loadFailed() is emitted.
    image_ = imageFromBase64Text(encoded);

    emit imageChanged();
    requestUpdate();
}

QString Avatar::base64() const
{
    return base64_;
}

void Avatar::setUrl(const QUrl& url)
{
    if (url_ == url && (loading_ || !image_.isNull() || !icon_.isNull()))
        return;

    cancelTransfer();

    url_ = url;
    image_ = QImage();
    icon_ = QIcon();
    base64_.clear();

    if (url.isEmpty() || !url.isValid()) {
        setLoading(false);
        emit imageChanged();
        requestUpdate();
        return;
    }

    setLoading(true);
    beginLoad();
}

QUrl Avatar::url() const
{
    return url_;
}

void Avatar::setImageLoader(std::function<QImage(const QUrl&)> loader)
{
    imageLoader_ = std::move(loader);

    // A loader installed while a url is active reloads that url; otherwise the
    // next setUrl() picks the loader up.
    if (!url_.isEmpty() && url_.isValid()) {
        cancelTransfer();
        image_ = QImage();
        icon_ = QIcon();
        setLoading(true);
        beginLoad();
    }
}

void Avatar::setInitials(const QString& initials)
{
    if (initials_ == initials)
        return;

    initials_ = initials;
    requestUpdate();
}

QString Avatar::initials() const
{
    return initials_;
}

QString Avatar::effectiveInitials() const
{
    const QString trimmed = initials_.trimmed();
    if (trimmed.isEmpty())
        return {};

    QStringList words;
    QString current;
    for (const QChar character : trimmed) {
        if (character.isSpace()) {
            if (!current.isEmpty()) {
                words.append(current);
                current.clear();
            }
        } else {
            current.append(character);
        }
    }
    if (!current.isEmpty())
        words.append(current);

    if (words.isEmpty())
        return {};

    QString result = words.size() >= 2 ? words.at(0).left(1) + words.at(1).left(1)
                                       : words.at(0).left(2);
    return result.toUpper();
}

void Avatar::setPlaceholderColour(const QColor& colour)
{
    if (placeholderColour_ == colour)
        return;

    placeholderColour_ = colour;
    requestUpdate();
}

QColor Avatar::placeholderColour() const
{
    return resolvedPlaceholderColour();
}

bool Avatar::hasImage() const noexcept
{
    return !image_.isNull() || !icon_.isNull();
}

bool Avatar::isLoading() const noexcept
{
    return loading_;
}

void Avatar::clear()
{
    cancelTransfer();
    setLoading(false);

    image_ = QImage();
    icon_ = QIcon();
    base64_.clear();
    url_ = QUrl();

    emit imageChanged();
    requestUpdate();
}

void Avatar::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const qreal side = Theme::forWidget(this).density().dp(diameterDp_);
    setMeasuredSize(QSizeF(width.resolve(side), height.resolve(side)));
}

void Avatar::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();

    // Keep the mark square: a circle must never become an ellipse.
    const qreal side = std::min(content.width(), content.height());
    if (side <= 0.0)
        return;

    const QRectF box(content.center().x() - side / 2.0,
                     content.center().y() - side / 2.0, side, side);

    const Shape outline = mode_ == ShapeMode::Circle
        ? Shape::pill()
        : Shape::rounded(CornerRadii::uniform(std::min(side / 2.0, density.dp(8.0))));
    const QPainterPath path = outline.path(box);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipPath(path);
    painter.setPen(Qt::NoPen);

    const qreal ratio = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    const int pixelSide = std::max(1, static_cast<int>(qRound(side * ratio)));
    const QImage source = effectiveImage(pixelSide);

    if (!source.isNull()) {
        const QSizeF imageSize = source.deviceIndependentSize();
        if (!imageSize.isEmpty()) {
            // Cover: the image fills the box and the overhang is clipped.
            const qreal scale = std::max(box.width() / imageSize.width(),
                                         box.height() / imageSize.height());
            const QSizeF target(imageSize.width() * scale, imageSize.height() * scale);
            const QRectF targetRect(box.center().x() - target.width() / 2.0,
                                    box.center().y() - target.height() / 2.0,
                                    target.width(), target.height());
            painter.drawImage(targetRect, source);
        }
    } else {
        const QString text = effectiveInitials();
        const PlaceholderTint tint =
            placeholderTint(theme.colours(), text, placeholderColour_);

        painter.setBrush(tint.fill);
        painter.drawPath(path);

        if (!text.isEmpty()) {
            const QFont font = initialsFont(theme, side);
            painter.setFont(font);
            painter.setPen(tint.ink);
            painter.drawText(box, static_cast<int>(Qt::AlignCenter), text);
        }
    }

    painter.restore();

    const qreal ringWidth = density.dp(borderWidthDp_);
    if (ringWidth > 0.0) {
        const QColor ring = borderColour_.isValid() ? borderColour_ : theme.colours().outline;
        const qreal inset = ringWidth / 2.0;
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ring, ringWidth));
        painter.drawPath(outline.path(box.adjusted(inset, inset, -inset, -inset)));
        painter.restore();
    }
}

bool Avatar::focusable() const
{
    return false;
}

QImage Avatar::effectiveImage(int pixelSide) const
{
    if (!image_.isNull())
        return image_;

    if (icon_.isNull())
        return QImage();

    const qreal ratio = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    const QPixmap pixmap =
        icon_.pixmap(QSize(std::max(1, pixelSide), std::max(1, pixelSide)), ratio,
                     QIcon::Normal, QIcon::Off);
    return pixmap.isNull() ? QImage() : pixmap.toImage();
}

QColor Avatar::resolvedPlaceholderColour() const
{
    return placeholderTint(Theme::forWidget(this).colours(), effectiveInitials(),
                           placeholderColour_)
        .fill;
}

void Avatar::setLoading(bool loading)
{
    if (loading_ == loading)
        return;

    loading_ = loading;
    emit loadingChanged(loading_);
    requestUpdate();
}

void Avatar::applyLoadedImage(const QImage& image)
{
    image_ = image;
    icon_ = QIcon();
    setLoading(false);

    emit imageChanged();
    requestUpdate();
}

void Avatar::failLoad()
{
    image_ = QImage();
    icon_ = QIcon();
    setLoading(false);

    emit loadFailed(url_);
    emit imageChanged();
    requestUpdate();
}

void Avatar::beginLoad()
{
    const quint64 generation = loadGeneration_;

    if (imageLoader_) {
        const QImage loaded = imageLoader_(url_);
        if (generation != loadGeneration_)
            return;

        if (loaded.isNull())
            failLoad();
        else
            applyLoadedImage(loaded);
        return;
    }

    // A data: url names no remote resource, so decoding it in place cannot
    // block the UI and keeps the behaviour deterministic offscreen.
    if (url_.scheme().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0) {
        const QImage loaded = imageFromDataUrl(url_);
        if (generation != loadGeneration_)
            return;

        if (loaded.isNull())
            failLoad();
        else
            applyLoadedImage(loaded);
        return;
    }

    QNetworkRequest request(url_);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = networkManager()->get(request);
    reply_ = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (generation != loadGeneration_ || reply_ != reply) {
            reply->deleteLater();
            return;
        }

        reply_ = nullptr;
        const QByteArray payload = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            failLoad();
            return;
        }

        const QImage loaded = QImage::fromData(payload);
        if (loaded.isNull()) {
            failLoad();
            return;
        }

        applyLoadedImage(loaded);
    });
}

void Avatar::cancelTransfer()
{
    // The generation bump alone would make the old callback inert; aborting the
    // reply also releases the socket immediately.
    ++loadGeneration_;

    if (reply_ == nullptr)
        return;

    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

QNetworkAccessManager* Avatar::networkManager()
{
    if (network_ == nullptr)
        network_ = new QNetworkAccessManager(this);

    return network_;
}

} // namespace md
