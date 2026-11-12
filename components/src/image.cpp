// Periodicity — Image (media/content batch).
//
// The paint path is a clip plus one drawImage(): Contain computes the largest
// letterboxed rectangle that fits, Cover the smallest rectangle that covers the
// frame, Fill the frame itself. Because the clip is the rounded shape, Cover's
// overhang never leaks outside the frame.
//
// The loader mirrors Avatar's policy — injected loader, in-place data: decode,
// asynchronous QNetworkAccessManager otherwise — but lives in this translation
// unit as well, because a public coupling between two leaf components would be
// worse than twenty duplicated lines. The reply is guarded by a generation
// counter exactly like Avatar's.

#include <md/components/image.h>

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

QFont placeholderFont(const Theme& theme)
{
    const TextStyle& style = theme.type().bodyMedium;
    QFont font(style.family);
    const Density& density = theme.density();
    font.setPixelSize(std::max(1, static_cast<int>(qRound(density.sp(style.sizeSp)))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

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

    const QByteArray bytes =
        QByteArray::fromBase64(compact.toLatin1(), QByteArray::Base64Encoding);
    return QImage::fromData(bytes);
}

QImage imageFromDataUrl(const QUrl& url)
{
    return imageFromBase64Text(url.toString());
}

// A widget pinned by setFixedSize() reports equal, positive minimum and
// maximum sizes. The measure pass honours that before its own natural size,
// which is what "the image follows setFixedSize" means for a View.
QSize fixedWidgetSize(const QWidget* widget)
{
    const QSize minimum = widget->minimumSize();
    const QSize maximum = widget->maximumSize();
    if (minimum.width() > 0 && minimum.height() > 0 && minimum == maximum)
        return minimum;

    return QSize();
}

} // namespace

Image::Image(QWidget* parent)
    : View(parent)
{
    setBackgroundColour(QColor(Qt::transparent));
    setFocusPolicy(Qt::NoFocus);
}

Image::Image(const QImage& image, QWidget* parent)
    : Image(parent)
{
    setImage(image);
}

Image::~Image()
{
    cancelTransfer();
}

void Image::setImage(const QImage& image)
{
    cancelTransfer();
    setLoading(false);

    image_ = image;
    source_ = QUrl();
    failed_ = false;

    updateGeometry();
    emit imageChanged();
    requestUpdate();
}

QImage Image::image() const
{
    return image_;
}

void Image::setPixmap(const QPixmap& pixmap)
{
    setImage(pixmap.toImage());
}

QPixmap Image::pixmap() const
{
    return QPixmap::fromImage(image_);
}

void Image::setSource(const QUrl& url)
{
    if (source_ == url && (loading_ || !image_.isNull()))
        return;

    cancelTransfer();

    source_ = url;
    image_ = QImage();
    failed_ = false;

    if (url.isEmpty() || !url.isValid()) {
        setLoading(false);
        emit imageChanged();
        requestUpdate();
        return;
    }

    setLoading(true);
    beginLoad();
}

QUrl Image::source() const
{
    return source_;
}

void Image::setImageLoader(std::function<QImage(const QUrl&)> loader)
{
    imageLoader_ = std::move(loader);

    if (!source_.isEmpty() && source_.isValid()) {
        cancelTransfer();
        image_ = QImage();
        failed_ = false;
        setLoading(true);
        beginLoad();
    }
}

void Image::setFit(FitMode fit)
{
    if (fit_ == fit)
        return;

    fit_ = fit;
    requestUpdate();
}

Image::FitMode Image::fit() const noexcept
{
    return fit_;
}

void Image::setCornerRadius(qreal dp)
{
    const qreal clamped = std::max(qreal(0.0), dp);
    if (qFuzzyCompare(cornerRadiusDp_ + 1.0, clamped + 1.0))
        return;

    cornerRadiusDp_ = clamped;
    requestUpdate();
}

qreal Image::cornerRadius() const noexcept
{
    return cornerRadiusDp_;
}

void Image::setPlaceholderColour(const QColor& colour)
{
    if (placeholderColour_ == colour)
        return;

    placeholderColour_ = colour;
    requestUpdate();
}

QColor Image::placeholderColour() const
{
    return resolvedPlaceholderColour();
}

void Image::setPlaceholderText(const QString& text)
{
    if (placeholderText_ == text)
        return;

    placeholderText_ = text;
    requestUpdate();
}

QString Image::placeholderText() const
{
    return placeholderText_;
}

void Image::setErrorPlaceholderEnabled(bool enabled)
{
    if (errorPlaceholderEnabled_ == enabled)
        return;

    errorPlaceholderEnabled_ = enabled;
    requestUpdate();
}

bool Image::isErrorPlaceholderEnabled() const noexcept
{
    return errorPlaceholderEnabled_;
}

void Image::setErrorText(const QString& text)
{
    if (errorText_ == text)
        return;

    errorText_ = text;
    requestUpdate();
}

QString Image::errorText() const
{
    return errorText_;
}

bool Image::hasImage() const noexcept
{
    return !image_.isNull();
}

bool Image::isLoading() const noexcept
{
    return loading_;
}

bool Image::hasFailed() const noexcept
{
    return failed_;
}

void Image::clear()
{
    cancelTransfer();
    setLoading(false);

    image_ = QImage();
    source_ = QUrl();
    failed_ = false;

    updateGeometry();
    emit imageChanged();
    requestUpdate();
}

void Image::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Theme& theme = Theme::forWidget(this);

    QSizeF natural;
    const QSize fixed = fixedWidgetSize(this);
    if (fixed.isValid())
        natural = QSizeF(fixed);
    else if (!image_.isNull())
        natural = image_.deviceIndependentSize();

    if (natural.width() <= 0.0 || natural.height() <= 0.0)
        natural = QSizeF(theme.density().dp(160.0), theme.density().dp(120.0));

    setMeasuredSize(QSizeF(width.resolve(natural.width()), height.resolve(natural.height())));
}

void Image::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const QPainterPath frame =
        Shape::rounded(CornerRadii::uniform(resolvedCornerRadius())).path(content);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipPath(frame);

    if (!image_.isNull()) {
        const QSizeF imageSize = image_.deviceIndependentSize();
        if (!imageSize.isEmpty()) {
            QRectF target = content;
            if (fit_ != FitMode::Fill) {
                const qreal scaleX = content.width() / imageSize.width();
                const qreal scaleY = content.height() / imageSize.height();
                const qreal scale =
                    fit_ == FitMode::Cover ? std::max(scaleX, scaleY)
                                           : std::min(scaleX, scaleY);
                const QSizeF scaled(imageSize.width() * scale, imageSize.height() * scale);
                target = QRectF(content.center().x() - scaled.width() / 2.0,
                                content.center().y() - scaled.height() / 2.0,
                                scaled.width(), scaled.height());
            }

            painter.drawImage(target, image_);
        }
    } else {
        const bool error = failed_ && errorPlaceholderEnabled_;
        const QColor fill =
            error ? theme.colours().errorContainer : resolvedPlaceholderColour();
        const QColor ink = error ? theme.colours().onErrorContainer : theme.colours().onSurfaceVariant;

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPath(frame);

        const QString text = error ? errorText_ : placeholderText_;
        if (!text.isEmpty() && content.width() > 0.0 && content.height() > 0.0) {
            const QFont font = placeholderFont(theme);
            const QFontMetricsF metrics(font);
            const qreal inset = theme.density().dp(8.0);
            const qreal available = std::max(content.width() - inset * 2.0, qreal(0.0));
            const QString elided = metrics.elidedText(text, Qt::ElideRight, available);

            painter.setFont(font);
            painter.setPen(ink);
            painter.drawText(content, static_cast<int>(Qt::AlignCenter), elided);
        }
    }

    painter.restore();
}

bool Image::focusable() const
{
    return false;
}

void Image::setLoading(bool loading)
{
    if (loading_ == loading)
        return;

    loading_ = loading;
    emit loadingChanged(loading_);
    requestUpdate();
}

void Image::applyLoadedImage(const QImage& image)
{
    image_ = image;
    failed_ = false;
    setLoading(false);

    updateGeometry();
    emit imageChanged();
    requestUpdate();
}

void Image::failLoad()
{
    image_ = QImage();
    failed_ = true;
    setLoading(false);

    updateGeometry();
    emit loadFailed(source_);
    emit imageChanged();
    requestUpdate();
}

void Image::beginLoad()
{
    const quint64 generation = loadGeneration_;

    if (imageLoader_) {
        const QImage loaded = imageLoader_(source_);
        if (generation != loadGeneration_)
            return;

        if (loaded.isNull())
            failLoad();
        else
            applyLoadedImage(loaded);
        return;
    }

    if (source_.scheme().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0) {
        const QImage loaded = imageFromDataUrl(source_);
        if (generation != loadGeneration_)
            return;

        if (loaded.isNull())
            failLoad();
        else
            applyLoadedImage(loaded);
        return;
    }

    QNetworkRequest request(source_);
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

void Image::cancelTransfer()
{
    ++loadGeneration_;

    if (reply_ == nullptr)
        return;

    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

qreal Image::resolvedCornerRadius() const
{
    const qreal radius = Theme::forWidget(this).density().dp(cornerRadiusDp_);
    const qreal limit = std::min(rect().width(), rect().height()) / 2.0;
    return std::clamp(radius, qreal(0.0), std::max(qreal(0.0), limit));
}

QColor Image::resolvedPlaceholderColour() const
{
    if (placeholderColour_.isValid())
        return placeholderColour_;

    return Theme::forWidget(this).colours().surfaceContainerHighest;
}

QNetworkAccessManager* Image::networkManager()
{
    if (network_ == nullptr)
        network_ = new QNetworkAccessManager(this);

    return network_;
}

} // namespace md
