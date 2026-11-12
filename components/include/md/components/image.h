// Periodicity — Image (media/content batch).
//
// A self-painted picture surface. It shows a QImage or QPixmap with one of the
// three fit modes — Contain (letterbox), Cover (crop) and Fill (stretch) —
// clips every frame to a rounded rectangle, and paints a themed placeholder
// when there is nothing to show. A source QUrl is loaded the same way Avatar
// loads one: a host loader when injected, an in-place decode for data: urls,
// and QNetworkAccessManager for everything else. A failed load can fall back to
// an error placeholder instead of pretending the image is still coming.
//
// The natural size is the image's device-independent size; setFixedSize() wins
// over it because the measure pass consults the widget's fixed bounds before
// the image. With no image the natural size is a 160 x 120 dp placeholder box.

#pragma once

#include <md/view/view.h>

#include <QColor>
#include <QImage>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QPainter;
class QPixmap;
class QRectF;

namespace md {

class Image : public View
{
    Q_OBJECT

public:
    enum class FitMode {
        Contain, // The whole image, letterboxed inside the frame.
        Cover,   // The frame is filled; the overhang is cropped.
        Fill,    // The image is stretched to the frame.
    };

    explicit Image(QWidget* parent = nullptr);
    explicit Image(const QImage& image, QWidget* parent = nullptr);
    ~Image() override;

    void setImage(const QImage& image);
    [[nodiscard]] QImage image() const;

    void setPixmap(const QPixmap& pixmap);
    [[nodiscard]] QPixmap pixmap() const;

    // Loads `url`, replacing any current image. An empty url clears the source
    // and shows the placeholder.
    void setSource(const QUrl& url);
    [[nodiscard]] QUrl source() const;

    // Host replacement for the network path, run on the calling thread.
    void setImageLoader(std::function<QImage(const QUrl&)> loader);

    void setFit(FitMode fit);
    [[nodiscard]] FitMode fit() const noexcept;

    // The frame radius, in dp. Zero paints a plain rectangle.
    void setCornerRadius(qreal dp);
    [[nodiscard]] qreal cornerRadius() const noexcept;

    // The empty-state fill. An invalid colour (the default) resolves to the
    // theme's surfaceContainerHighest; placeholderColour() reports the fill
    // that is actually painted.
    void setPlaceholderColour(const QColor& colour);
    [[nodiscard]] QColor placeholderColour() const;

    void setPlaceholderText(const QString& text);
    [[nodiscard]] QString placeholderText() const;

    // When enabled (the default), a failed source paints the error role and
    // errorText() instead of the ordinary placeholder.
    void setErrorPlaceholderEnabled(bool enabled);
    [[nodiscard]] bool isErrorPlaceholderEnabled() const noexcept;

    void setErrorText(const QString& text);
    [[nodiscard]] QString errorText() const;

    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] bool isLoading() const noexcept;
    [[nodiscard]] bool hasFailed() const noexcept;

    void clear();

signals:
    void imageChanged();
    void loadingChanged(bool loading);
    void loadFailed(const QUrl& url);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    [[nodiscard]] bool focusable() const override;

private:
    void setLoading(bool loading);
    void applyLoadedImage(const QImage& image);
    void failLoad();
    void beginLoad();
    void cancelTransfer();

    [[nodiscard]] qreal resolvedCornerRadius() const;
    [[nodiscard]] QColor resolvedPlaceholderColour() const;
    [[nodiscard]] QNetworkAccessManager* networkManager();

    QImage image_;
    QUrl source_;
    std::function<QImage(const QUrl&)> imageLoader_;

    FitMode fit_ = FitMode::Contain;
    qreal cornerRadiusDp_ = 12.0;
    QColor placeholderColour_;
    QString placeholderText_ = QStringLiteral("No image");
    QString errorText_ = QStringLiteral("Failed to load");
    bool errorPlaceholderEnabled_ = true;

    bool loading_ = false;
    bool failed_ = false;
    quint64 loadGeneration_ = 0;

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
};

} // namespace md
