// Periodicity — Avatar (media/content batch).
//
// A circular or rounded-square identity glyph. One source is active at a time:
// a QIcon, a QImage/QPixmap, a base64 string (a data:image/...;base64, prefix
// is accepted) or a QUrl. A url is downloaded asynchronously through
// QNetworkAccessManager, or synchronously through a host-supplied loader that
// replaces the network policy entirely (setImageLoader()). A data: url is
// decoded in place because it names no remote resource and must not block.
//
// With no usable image the component paints the placeholder colour — chosen by
// hashing the initials, so one name always maps to one tint — and the initials
// themselves. A failed load falls back to the same placeholder and reports
// loadFailed(); it never leaves the widget blank.
//
// The component owns no interaction: it is a static identity mark, so it is
// never focusable and paints no container besides its own shape.

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QPainter;
class QRectF;

namespace md {

class Avatar : public View
{
    Q_OBJECT

public:
    // The outline: a full circle, or a rounded square.
    enum class ShapeMode {
        Circle,
        Square,
    };

    explicit Avatar(QWidget* parent = nullptr);
    explicit Avatar(const QString& initials, QWidget* parent = nullptr);
    ~Avatar() override;

    void setShapeMode(ShapeMode mode);
    [[nodiscard]] ShapeMode shapeMode() const noexcept;

    // The requested box, in dp. A circle always paints a square box centred in
    // the widget, so a non-square resize never yields an ellipse.
    void setDiameter(qreal dp);
    [[nodiscard]] qreal diameter() const noexcept;

    // An optional ring. A width of zero (the default) paints no ring.
    void setBorderWidth(qreal dp);
    [[nodiscard]] qreal borderWidth() const noexcept;
    void setBorderColour(const QColor& colour);
    [[nodiscard]] QColor borderColour() const;

    // The four sources. Setting one clears the others; the last call wins.
    void setIcon(const QIcon& icon);
    [[nodiscard]] QIcon icon() const;

    void setImage(const QImage& image);
    [[nodiscard]] QImage image() const;

    void setPixmap(const QPixmap& pixmap);

    void setBase64(const QString& encoded);
    [[nodiscard]] QString base64() const;

    void setUrl(const QUrl& url);
    [[nodiscard]] QUrl url() const;

    // Host replacement for the network path. The loader runs on the calling
    // thread and returns the decoded image, or a null image to fail the load.
    // While it is installed no QNetworkAccessManager request is made.
    void setImageLoader(std::function<QImage(const QUrl&)> loader);

    // The fallback text. When empty, a multi-word string is reduced to the
    // first letter of its first two words, and a single word to its first two
    // characters.
    void setInitials(const QString& initials);
    [[nodiscard]] QString initials() const;
    [[nodiscard]] QString effectiveInitials() const;

    // The placeholder fill. An invalid colour (the default) is resolved from
    // the initials hash; placeholderColour() reports what is actually painted.
    void setPlaceholderColour(const QColor& colour);
    [[nodiscard]] QColor placeholderColour() const;

    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] bool isLoading() const noexcept;

    // Drops every source and returns to the placeholder.
    void clear();

signals:
    // The effective image changed: a source was set, a download finished, or a
    // load failed and the placeholder took over.
    void imageChanged();

    // A url download started (true) or settled (false).
    void loadingChanged(bool loading);

    // A url could not be fetched or decoded. The placeholder is active.
    void loadFailed(const QUrl& url);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    [[nodiscard]] bool focusable() const override;

private:
    // Resolves `icon_` at the pixel size the current box needs. An icon is
    // rasterised at paint time so it stays crisp at every device pixel ratio.
    [[nodiscard]] QImage effectiveImage(int pixelSide) const;
    [[nodiscard]] QColor resolvedPlaceholderColour() const;

    void setLoading(bool loading);
    void applyLoadedImage(const QImage& image);
    void failLoad();
    void beginLoad();
    void cancelTransfer();

    [[nodiscard]] QNetworkAccessManager* networkManager();

    QImage image_;
    QIcon icon_;
    QString base64_;
    QUrl url_;
    std::function<QImage(const QUrl&)> imageLoader_;

    ShapeMode mode_ = ShapeMode::Circle;

    QString initials_;
    QColor placeholderColour_;
    QColor borderColour_;

    qreal diameterDp_ = 40.0;
    qreal borderWidthDp_ = 0.0;

    bool loading_ = false;
    // Invalidates the callback of an in-flight reply when the source changes.
    quint64 loadGeneration_ = 0;

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
};

} // namespace md
