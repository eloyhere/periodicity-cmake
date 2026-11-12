// Unit tests for the media/content component batch (framework.md §14).
//
// Every widget is exercised offscreen, exactly like tst_basic_widgets: the
// application theme is pinned to baseline light, geometry is measured from
// sizeHint(), widgets are rendered into a transparent QImage and sampled in
// logical coordinates, and pointer/key input is delivered with
// QApplication::sendEvent (no window activation, no virtual cursor).
//
// Network access is never used. The url path is covered with data: urls — which
// Avatar and Image decode in place — and with an injected image loader, which
// is the host replacement for QNetworkAccessManager. That keeps the tests
// deterministic and offline while still exercising the same state machine.

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QRegion>
#include <QSignalSpy>
#include <QSize>
#include <QStringList>
#include <QTest>
#include <QUrl>
#include <QWheelEvent>
#include <QWidget>

#include <md/components/avatar.h>
#include <md/components/button.h>
#include <md/components/card.h>
#include <md/components/image.h>
#include <md/components/skeleton.h>
#include <md/components/tag.h>
#include <md/components/transfer.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/elevation.h>

namespace {

const md::ColorScheme& lightColours()
{
    return md::Theme::applicationTheme().colours();
}

QImage solidImage(const QSize& size, const QColor& colour)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(colour);
    return image;
}

QString pngDataUrl(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    buffer.close();

    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

// Renders a widget into an explicitly transparent image. QWidget::render()
// would otherwise paint the palette's window background first - and
// QWidget::grab() leaves unpainted pixels uninitialised for widgets that do
// not fill their background - so every pixel assertion renders with
// DrawChildren only, over a cleared image.
QImage renderWidget(QWidget& widget)
{
    const qreal ratio = widget.devicePixelRatioF();
    QImage image(widget.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    widget.render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();

    return image;
}

QColor pixelAt(const QImage& image, const QPointF& logical)
{
    const qreal ratio = image.devicePixelRatio();
    const int x = qBound(0, qRound(logical.x() * ratio), image.width() - 1);
    const int y = qBound(0, qRound(logical.y() * ratio), image.height() - 1);
    return image.pixelColor(x, y);
}

int paintedPixels(const QImage& image)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) != 0)
                ++count;
        }
    }
    return count;
}

void sendMouse(QWidget& widget, QEvent::Type type, const QPointF& position,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    const QPoint global = widget.mapToGlobal(position.toPoint());
    QMouseEvent event(type, position, QPointF(global), button, buttons, modifiers);
    QApplication::sendEvent(&widget, &event);
}

void clickAt(QWidget& widget, const QPointF& position,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    sendMouse(widget, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton,
              modifiers);
    sendMouse(widget, QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton,
              modifiers);
}

} // namespace

class TestMediaBasics : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void avatarIconSource();
    void avatarBase64Source();
    void avatarDataUrlAndLoaderFallback();
    void avatarPlaceholder();

    void imageFitModes();
    void imageRoundedAndPlaceholder();
    void imageSourceLoaderAndFailure();
    void imageFixedSize();

    void cardVariants();
    void cardMediaAndActions();

    void tagSelectionAndClose();
    void tagVariants();

    void skeletonGeometryAndAnimation();

    void transferMovesAndSignals();
    void transferMultiSelectionAndScroll();
};

void TestMediaBasics::initTestCase()
{
    md::Theme::setApplicationTheme(md::Theme::baselineLight());
}

void TestMediaBasics::avatarIconSource()
{
    QPixmap source(24, 24);
    source.fill(Qt::transparent);
    {
        QPainter painter(&source);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::white));
        painter.drawRect(QRectF(4.0, 4.0, 16.0, 16.0));
    }

    md::Avatar avatar;
    QSignalSpy imageSpy(&avatar, &md::Avatar::imageChanged);
    avatar.setIcon(QIcon(source));
    avatar.setDiameter(40.0);
    avatar.resize(avatar.sizeHint());

    QVERIFY(avatar.hasImage());
    QVERIFY(!avatar.isLoading());
    QCOMPARE(imageSpy.count(), 1);
    QCOMPARE(avatar.sizeHint(), QSize(40, 40));

    const QImage rendered = renderWidget(avatar);
    QCOMPARE(pixelAt(rendered, QPointF(20.0, 20.0)).rgb(), QColor(Qt::white).rgb());
    QCOMPARE(pixelAt(rendered, QPointF(0.0, 0.0)).alpha(), 0);
}

void TestMediaBasics::avatarBase64Source()
{
    const QImage blue = solidImage(QSize(24, 24), QColor(0, 120, 255));

    md::Avatar avatar;
    QSignalSpy imageSpy(&avatar, &md::Avatar::imageChanged);
    avatar.setBase64(pngDataUrl(blue));
    avatar.resize(avatar.sizeHint());

    QVERIFY(avatar.hasImage());
    QVERIFY(!avatar.base64().isEmpty());
    QCOMPARE(imageSpy.count(), 1);
    QCOMPARE(pixelAt(renderWidget(avatar), QPointF(20.0, 20.0)).rgb(),
             QColor(0, 120, 255).rgb());

    // An invalid payload falls back to the placeholder rather than clearing
    // the signal path or painting a broken image.
    md::Avatar invalid;
    invalid.setBase64(QStringLiteral("data:image/png;base64,bm90IGFuIGltYWdl"));
    QVERIFY(!invalid.hasImage());
    QVERIFY(invalid.base64().startsWith(QStringLiteral("data:image/png")));
}

void TestMediaBasics::avatarDataUrlAndLoaderFallback()
{
    const QImage green = solidImage(QSize(16, 16), QColor(0, 180, 60));

    // 1. A data: url is decoded in place, without a loader and without a
    //    network request.
    md::Avatar direct;
    direct.setUrl(QUrl(pngDataUrl(green)));
    QVERIFY(direct.hasImage());
    QVERIFY(!direct.isLoading());
    direct.resize(direct.sizeHint());
    QCOMPARE(pixelAt(renderWidget(direct), QPointF(20.0, 20.0)).rgb(),
             QColor(0, 180, 60).rgb());

    // 2. The host loader replaces the network path and reports loading.
    md::Avatar loaded;
    QSignalSpy imageSpy(&loaded, &md::Avatar::imageChanged);
    QSignalSpy loadingSpy(&loaded, &md::Avatar::loadingChanged);
    QSignalSpy failureSpy(&loaded, &md::Avatar::loadFailed);

    loaded.setImageLoader([green](const QUrl&) { return green; });
    loaded.setUrl(QUrl(QStringLiteral("https://example.invalid/avatar.png")));

    QVERIFY(loaded.hasImage());
    QVERIFY(!loaded.isLoading());
    QCOMPARE(imageSpy.count(), 1);
    QCOMPARE(loadingSpy.count(), 2);
    QVERIFY(loadingSpy.at(0).at(0).toBool());
    QVERIFY(!loadingSpy.at(1).at(0).toBool());
    QCOMPARE(failureSpy.count(), 0);

    // 3. A failed load reports loadFailed() and shows the hashed placeholder.
    md::Avatar failing;
    QSignalSpy failingSpy(&failing, &md::Avatar::loadFailed);
    failing.setImageLoader([](const QUrl&) { return QImage(); });
    failing.setUrl(QUrl(QStringLiteral("https://example.invalid/missing.png")));

    QVERIFY(!failing.hasImage());
    QVERIFY(!failing.isLoading());
    QCOMPARE(failingSpy.count(), 1);
    QCOMPARE(failingSpy.at(0).at(0).toUrl(),
             QUrl(QStringLiteral("https://example.invalid/missing.png")));

    failing.resize(40, 40);
    QCOMPARE(pixelAt(renderWidget(failing), QPointF(20.0, 6.0)).rgb(),
             failing.placeholderColour().rgb());
}

void TestMediaBasics::avatarPlaceholder()
{
    md::Avatar avatar(QStringLiteral("Ada Lovelace"));
    QCOMPARE(avatar.effectiveInitials(), QStringLiteral("AL"));
    QVERIFY(!avatar.hasImage());
    QVERIFY(!avatar.isLoading());
    QVERIFY(avatar.placeholderColour().isValid());
    QCOMPARE(avatar.placeholderColour().alpha(), 255);

    avatar.setPlaceholderColour(QColor(12, 34, 56));
    avatar.setDiameter(40.0);
    avatar.setBorderWidth(2.0);
    avatar.setBorderColour(QColor(200, 10, 10));
    avatar.resize(avatar.sizeHint());

    QCOMPARE(avatar.placeholderColour().rgb(), QColor(12, 34, 56).rgb());
    QCOMPARE(avatar.borderWidth(), 2.0);
    QCOMPARE(pixelAt(renderWidget(avatar), QPointF(20.0, 6.0)).rgb(),
             QColor(12, 34, 56).rgb());

    QVERIFY(avatar.shapeMode() == md::Avatar::ShapeMode::Circle);
    avatar.setShapeMode(md::Avatar::ShapeMode::Square);
    QVERIFY(avatar.shapeMode() == md::Avatar::ShapeMode::Square);

    // A cleared avatar is back to the placeholder and reports the change.
    md::Avatar cleared;
    cleared.setImage(solidImage(QSize(8, 8), Qt::red));
    QSignalSpy imageSpy(&cleared, &md::Avatar::imageChanged);
    cleared.clear();
    QVERIFY(!cleared.hasImage());
    QCOMPARE(imageSpy.count(), 1);
}

void TestMediaBasics::imageFitModes()
{
    const QImage red = solidImage(QSize(40, 20), QColor(255, 0, 0));

    md::Image contain(red);
    QCOMPARE(contain.sizeHint(), QSize(40, 20));
    contain.setCornerRadius(0.0);
    contain.resize(100, 100);

    QImage rendered = renderWidget(contain);
    QCOMPARE(pixelAt(rendered, QPointF(50.0, 50.0)).rgb(), QColor(255, 0, 0).rgb());
    QCOMPARE(pixelAt(rendered, QPointF(50.0, 2.0)).alpha(), 0);
    QCOMPARE(pixelAt(rendered, QPointF(2.0, 50.0)).rgb(), QColor(255, 0, 0).rgb());

    contain.setFit(md::Image::FitMode::Cover);
    rendered = renderWidget(contain);
    QCOMPARE(pixelAt(rendered, QPointF(50.0, 2.0)).rgb(), QColor(255, 0, 0).rgb());
    QCOMPARE(pixelAt(rendered, QPointF(2.0, 2.0)).rgb(), QColor(255, 0, 0).rgb());

    contain.setFit(md::Image::FitMode::Fill);
    rendered = renderWidget(contain);
    QCOMPARE(pixelAt(rendered, QPointF(2.0, 2.0)).rgb(), QColor(255, 0, 0).rgb());
    QCOMPARE(pixelAt(rendered, QPointF(98.0, 98.0)).rgb(), QColor(255, 0, 0).rgb());
}

void TestMediaBasics::imageRoundedAndPlaceholder()
{
    const QImage red = solidImage(QSize(40, 20), QColor(255, 0, 0));

    md::Image rounded(red);
    rounded.setFit(md::Image::FitMode::Fill);
    rounded.setCornerRadius(20.0);
    rounded.resize(100, 100);

    const QImage roundedImage = renderWidget(rounded);
    QCOMPARE(pixelAt(roundedImage, QPointF(1.0, 1.0)).alpha(), 0);
    QCOMPARE(pixelAt(roundedImage, QPointF(50.0, 50.0)).rgb(), QColor(255, 0, 0).rgb());

    md::Image placeholder;
    placeholder.setPlaceholderColour(QColor(10, 20, 30));
    placeholder.setPlaceholderText(QStringLiteral("Preview"));
    placeholder.setCornerRadius(0.0);
    placeholder.resize(120, 80);

    QCOMPARE(placeholder.placeholderColour().rgb(), QColor(10, 20, 30).rgb());
    QCOMPARE(placeholder.placeholderText(), QStringLiteral("Preview"));
    QVERIFY(!placeholder.hasImage());
    QVERIFY(!placeholder.hasFailed());

    const QImage placed = renderWidget(placeholder);
    QVERIFY(paintedPixels(placed) > 0);
    QCOMPARE(pixelAt(placed, QPointF(2.0, 2.0)).rgb(), QColor(10, 20, 30).rgb());
}

void TestMediaBasics::imageSourceLoaderAndFailure()
{
    const QImage green = solidImage(QSize(8, 8), QColor(0, 180, 0));

    md::Image loaded;
    loaded.setImageLoader([green](const QUrl&) { return green; });
    QSignalSpy imageSpy(&loaded, &md::Image::imageChanged);
    QSignalSpy loadingSpy(&loaded, &md::Image::loadingChanged);

    loaded.setSource(QUrl(QStringLiteral("https://example.invalid/picture.png")));
    QVERIFY(loaded.hasImage());
    QVERIFY(!loaded.hasFailed());
    QVERIFY(!loaded.isLoading());
    QCOMPARE(imageSpy.count(), 1);
    QCOMPARE(loadingSpy.count(), 2);

    md::Image failing;
    failing.setImageLoader([](const QUrl&) { return QImage(); });
    QSignalSpy failureSpy(&failing, &md::Image::loadFailed);
    failing.setSource(QUrl(QStringLiteral("https://example.invalid/missing.png")));

    QVERIFY(!failing.hasImage());
    QVERIFY(failing.hasFailed());
    QVERIFY(!failing.isLoading());
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(failureSpy.at(0).at(0).toUrl(),
             QUrl(QStringLiteral("https://example.invalid/missing.png")));

    failing.setCornerRadius(0.0);
    failing.resize(100, 80);
    QCOMPARE(pixelAt(renderWidget(failing), QPointF(2.0, 2.0)).rgb(),
             lightColours().errorContainer.rgb());

    failing.setErrorPlaceholderEnabled(false);
    QCOMPARE(pixelAt(renderWidget(failing), QPointF(2.0, 2.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());
}

void TestMediaBasics::imageFixedSize()
{
    const QImage red = solidImage(QSize(40, 20), QColor(255, 0, 0));

    md::Image fixed(red);
    fixed.setFixedSize(64, 32);
    QCOMPARE(fixed.sizeHint(), QSize(64, 32));

    md::Image natural(red);
    QCOMPARE(natural.sizeHint(), QSize(40, 20));
}

void TestMediaBasics::cardVariants()
{
    md::Card elevated;
    elevated.setVariant(md::Card::CardVariant::Elevated);
    elevated.resize(elevated.sizeHint());
    QCOMPARE(pixelAt(renderWidget(elevated), QPointF(10.0, 10.0)).rgb(),
             lightColours().surfaceContainerLow.rgb());

    md::Card filled;
    filled.resize(filled.sizeHint());
    QVERIFY(filled.variant() == md::Card::CardVariant::Filled);
    QCOMPARE(pixelAt(renderWidget(filled), QPointF(10.0, 10.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());

    md::Card outlined;
    outlined.setVariant(md::Card::CardVariant::Outlined);
    outlined.resize(outlined.sizeHint());
    QCOMPARE(pixelAt(renderWidget(outlined), QPointF(10.0, 10.0)).rgb(),
             lightColours().surface.rgb());

    QVERIFY(outlined.sizeHint().height() >= 32);
}

void TestMediaBasics::cardMediaAndActions()
{
    md::Card plain;
    const int plainHeight = plain.sizeHint().height();

    md::Card card;
    card.setTitle(QStringLiteral("Title"));
    card.setSubtitle(QStringLiteral("Subtitle"));
    card.setBody(QStringLiteral("Supporting body text that wraps at the card width."));
    card.setInteractive(true);
    QVERIFY(card.isInteractive());
    QVERIFY(card.sizeHint().height() > plainHeight);
    QCOMPARE(card.title(), QStringLiteral("Title"));
    QCOMPARE(card.subtitle(), QStringLiteral("Subtitle"));

    md::Card mediaCard;
    md::Image media(solidImage(QSize(80, 40), QColor(255, 0, 0)));
    mediaCard.setMedia(&media);
    QVERIFY(mediaCard.media() == &media);
    QVERIFY(mediaCard.sizeHint().height() > plainHeight);

    auto* action = new md::Button(QStringLiteral("Action"));
    card.addAction(action);
    QVERIFY(card.actions().contains(action));
    QVERIFY(action->parentWidget() == &card);

    card.resize(card.sizeHint());

    QSignalSpy actionSpy(action, &md::Button::clicked);
    clickAt(*action, QPointF(action->width() / 2.0, action->height() / 2.0));
    QCOMPARE(actionSpy.count(), 1);

    QSignalSpy cardSpy(&card, &md::Card::clicked);
    clickAt(card, QPointF(20.0, 20.0));
    QCOMPARE(cardSpy.count(), 1);

    // A press that leaves the card is not a click.
    sendMouse(card, QEvent::MouseButtonPress, QPointF(20.0, 20.0), Qt::LeftButton,
              Qt::LeftButton);
    sendMouse(card, QEvent::MouseButtonRelease, QPointF(card.width() + 30.0, 20.0),
              Qt::LeftButton, Qt::NoButton);
    QCOMPARE(cardSpy.count(), 1);

    md::Card passive;
    passive.setTitle(QStringLiteral("Title"));
    passive.resize(passive.sizeHint());
    QSignalSpy passiveSpy(&passive, &md::Card::clicked);
    clickAt(passive, QPointF(20.0, 20.0));
    QCOMPARE(passiveSpy.count(), 0);

    card.clearActions();
    QVERIFY(card.actions().isEmpty());
}

void TestMediaBasics::tagSelectionAndClose()
{
    md::Tag tag(QStringLiteral("Filter"));
    tag.resize(tag.sizeHint());
    QCOMPARE(tag.sizeHint().height(), 32);

    QSignalSpy clickedSpy(&tag, &md::Tag::clicked);
    QSignalSpy selectedSpy(&tag, &md::Tag::selectedChanged);

    clickAt(tag, QPointF(tag.width() / 2.0, 16.0));
    QVERIFY(tag.isSelected());
    QCOMPARE(selectedSpy.count(), 1);
    QCOMPARE(clickedSpy.count(), 1);
    QCOMPARE(selectedSpy.at(0).at(0).toBool(), true);
    QCOMPARE(pixelAt(renderWidget(tag), QPointF(8.0, 16.0)).rgb(),
             lightColours().primary.rgb());

    clickAt(tag, QPointF(tag.width() / 2.0, 16.0));
    QVERIFY(!tag.isSelected());
    QCOMPARE(selectedSpy.count(), 2);
    QCOMPARE(clickedSpy.count(), 2);
    QCOMPARE(pixelAt(renderWidget(tag), QPointF(8.0, 16.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());

    // The close region is a separate hit target: it never changes selection.
    tag.setClosable(true);
    tag.resize(tag.sizeHint());
    QSignalSpy closeSpy(&tag, &md::Tag::closeClicked);
    const int selectedBefore = selectedSpy.count();
    clickAt(tag, QPointF(tag.width() - 15.0, 16.0));
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(selectedSpy.count(), selectedBefore);
    QVERIFY(!tag.isSelected());

    tag.setSelected(true);
    QVERIFY(tag.isSelected());
    QCOMPARE(selectedSpy.count(), selectedBefore + 1);

    // Space toggles the body when the tag has focus; Delete would close it.
    tag.setSelected(false);
    const int beforeKeyboard = selectedSpy.count();
    QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&tag, &spacePress);
    QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&tag, &spaceRelease);
    QVERIFY(tag.isSelected());
    QCOMPARE(selectedSpy.count(), beforeKeyboard + 1);

    tag.setEnabled(false);
    const int clicksBeforeDisabled = clickedSpy.count();
    clickAt(tag, QPointF(tag.width() / 2.0, 16.0));
    QCOMPARE(clickedSpy.count(), clicksBeforeDisabled);
}

void TestMediaBasics::tagVariants()
{
    md::Tag filled(QStringLiteral("Tag"));
    filled.resize(filled.sizeHint());
    QCOMPARE(pixelAt(renderWidget(filled), QPointF(8.0, 16.0)).rgb(),
             lightColours().surfaceContainerHighest.rgb());

    md::Tag tonal(QStringLiteral("Tag"));
    tonal.setVariant(md::Tag::TagVariant::Tonal);
    tonal.resize(tonal.sizeHint());
    QVERIFY(tonal.variant() == md::Tag::TagVariant::Tonal);
    QCOMPARE(pixelAt(renderWidget(tonal), QPointF(8.0, 16.0)).rgb(),
             lightColours().surfaceContainerHigh.rgb());

    md::Tag outlined(QStringLiteral("Tag"));
    outlined.setVariant(md::Tag::TagVariant::Outlined);
    outlined.resize(outlined.sizeHint());
    QCOMPARE(pixelAt(renderWidget(outlined), QPointF(8.0, 16.0)).alpha(), 0);

    outlined.setColourDot(true);
    QVERIFY(outlined.hasColourDot());
    outlined.setDotColour(QColor(1, 2, 3));
    QCOMPARE(outlined.dotColour().rgb(), QColor(1, 2, 3).rgb());
    outlined.resize(outlined.sizeHint());
    QVERIFY(renderWidget(outlined).width() > 0);
}

void TestMediaBasics::skeletonGeometryAndAnimation()
{
    md::Skeleton rectangle;
    QCOMPARE(rectangle.sizeHint(), QSize(160, 16));
    QVERIFY(rectangle.skeletonShape() == md::Skeleton::SkeletonShape::Rect);

    rectangle.setSkeletonWidth(120.0);
    rectangle.setSkeletonHeight(12.0);
    rectangle.setCornerRadius(6.0);
    QCOMPARE(rectangle.sizeHint(), QSize(120, 12));
    QCOMPARE(rectangle.skeletonWidth(), 120.0);
    QCOMPARE(rectangle.skeletonHeight(), 12.0);
    QCOMPARE(rectangle.cornerRadius(), 6.0);

    md::Skeleton circle(md::Skeleton::SkeletonShape::Circle);
    QCOMPARE(circle.sizeHint(), QSize(48, 48));
    circle.setSkeletonWidth(40.0);
    circle.setSkeletonHeight(40.0);
    QCOMPARE(circle.sizeHint(), QSize(40, 40));

    // A stopped skeleton is a flat fill: no timer, no gradient.
    md::Skeleton stopped;
    stopped.setSkeletonWidth(160.0);
    stopped.setSkeletonHeight(16.0);
    stopped.setBaseColour(QColor(210, 210, 210));
    stopped.resize(stopped.sizeHint());
    QVERIFY(!stopped.isAnimating());
    QCOMPARE(pixelAt(renderWidget(stopped), QPointF(80.0, 8.0)).rgb(),
             QColor(210, 210, 210).rgb());

    md::Skeleton shimmer;
    shimmer.setSkeletonWidth(160.0);
    shimmer.setSkeletonHeight(16.0);
    shimmer.setBaseColour(QColor(220, 220, 220));
    shimmer.setHighlightColour(QColor(255, 255, 255));
    shimmer.resize(shimmer.sizeHint());

    shimmer.setAnimating(true);
    // Hidden: the request is remembered but no timer runs.
    QVERIFY(!shimmer.isAnimating());

    shimmer.show();
    QTest::qWait(60);
    QVERIFY(shimmer.isAnimating());

    const QImage first = renderWidget(shimmer);
    QTest::qWait(120);
    const QImage second = renderWidget(shimmer);
    QVERIFY(first != second);

    shimmer.setAnimating(false);
    QVERIFY(!shimmer.isAnimating());

    // A stopped skeleton paints one flat fill. The check uses a fresh instance:
    // showing the animated one synthesises a hover on the offscreen platform,
    // and the View pipeline then paints the hover state layer over the fill.
    md::Skeleton flat;
    flat.setSkeletonWidth(160.0);
    flat.setSkeletonHeight(16.0);
    flat.setBaseColour(QColor(220, 220, 220));
    flat.resize(flat.sizeHint());
    QCOMPARE(pixelAt(renderWidget(flat), QPointF(80.0, 8.0)).rgb(),
             QColor(220, 220, 220).rgb());

    shimmer.setAnimating(true);
    QTest::qWait(40);
    QVERIFY(shimmer.isAnimating());
    shimmer.hide();
    QVERIFY(!shimmer.isAnimating());
}

void TestMediaBasics::transferMovesAndSignals()
{
    md::Transfer transfer;
    QSignalSpy spy(&transfer, &md::Transfer::itemsChanged);

    transfer.setItems(QStringList{QStringLiteral("alpha"), QStringLiteral("beta"),
                                  QStringLiteral("gamma"), QStringLiteral("delta")});
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toStringList()
            == (QStringList{QStringLiteral("alpha"), QStringLiteral("beta"),
                            QStringLiteral("gamma"), QStringLiteral("delta")}));
    QVERIFY(spy.at(0).at(1).toStringList().isEmpty());

    transfer.resize(transfer.sizeHint());
    QCOMPARE(transfer.sizeHint(), QSize(520, 280));
    QCOMPARE(transfer.availableCount(), 4);
    QCOMPARE(transfer.selectedCount(), 0);
    QVERIFY(paintedPixels(renderWidget(transfer)) > 0);

    transfer.selectAvailable(1);
    QVERIFY(transfer.availableSelection() == (QList<int>{1}));
    transfer.moveSelectedRight();

    QVERIFY(transfer.availableItems()
            == (QStringList{QStringLiteral("alpha"), QStringLiteral("gamma"),
                            QStringLiteral("delta")}));
    QVERIFY(transfer.selectedItems() == (QStringList{QStringLiteral("beta")}));
    QCOMPARE(spy.count(), 2);
    QVERIFY(spy.at(1).at(1).toStringList() == (QStringList{QStringLiteral("beta")}));

    transfer.selectSelected(0);
    transfer.moveSelectedLeft();
    QVERIFY(transfer.availableItems()
            == (QStringList{QStringLiteral("alpha"), QStringLiteral("gamma"),
                            QStringLiteral("delta"), QStringLiteral("beta")}));
    QVERIFY(transfer.selectedItems().isEmpty());
    QCOMPARE(spy.count(), 3);

    transfer.moveAllRight();
    QVERIFY(transfer.availableItems().isEmpty());
    QVERIFY(transfer.selectedItems()
            == (QStringList{QStringLiteral("alpha"), QStringLiteral("gamma"),
                            QStringLiteral("delta"), QStringLiteral("beta")}));
    QCOMPARE(spy.count(), 4);

    transfer.moveAllLeft();
    QVERIFY(transfer.availableItems()
            == (QStringList{QStringLiteral("alpha"), QStringLiteral("gamma"),
                            QStringLiteral("delta"), QStringLiteral("beta")}));
    QVERIFY(transfer.selectedItems().isEmpty());
    QCOMPARE(spy.count(), 5);
    QCOMPARE(transfer.availableCount(), 4);

    // Moving with an empty selection is a no-op and emits nothing.
    transfer.moveSelectedRight();
    QCOMPARE(spy.count(), 5);
}

void TestMediaBasics::transferMultiSelectionAndScroll()
{
    md::Transfer transfer;
    transfer.setItems(QStringList{QStringLiteral("a"), QStringLiteral("b"),
                                  QStringLiteral("c"), QStringLiteral("d")});
    transfer.resize(transfer.sizeHint());

    transfer.selectAvailable(0);
    transfer.selectAvailable(2, true);
    QVERIFY(transfer.availableSelection() == (QList<int>{0, 1, 2}));

    // Ctrl-click row 3 toggles it into the selection (anchor moves to 3).
    const qreal rowHeight = 36.0;
    const qreal header = 40.0;
    const QPointF rowThree(60.0, header + rowHeight * 3.0 + rowHeight / 2.0);
    clickAt(transfer, rowThree, Qt::ControlModifier);
    QVERIFY(transfer.availableSelection() == (QList<int>{0, 1, 2, 3}));

    // Shift-click row 1 extends from the anchor 3 back to row 1.
    const QPointF rowOne(60.0, header + rowHeight + rowHeight / 2.0);
    clickAt(transfer, rowOne, Qt::ShiftModifier);
    QVERIFY(transfer.availableSelection() == (QList<int>{1, 2, 3}));

    transfer.moveSelectedRight();
    QVERIFY(transfer.availableItems() == (QStringList{QStringLiteral("a")}));
    QVERIFY(transfer.selectedItems()
            == (QStringList{QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}));
    transfer.moveAllLeft();
    QVERIFY(transfer.availableCount() == 4);

    // Each pane scrolls independently, and the offsets clamp to the content.
    QStringList many;
    for (int index = 0; index < 40; ++index)
        many.append(QString::number(index));
    transfer.setItems(many);
    transfer.resize(transfer.sizeHint());

    QWheelEvent wheel(QPointF(60.0, 166.0), QPointF(transfer.mapToGlobal(QPoint(60, 166))),
                      QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&transfer, &wheel);

    QVERIFY(transfer.availableScrollOffset() > 0.0);
    QCOMPARE(transfer.selectedScrollOffset(), 0.0);

    transfer.setAvailableScrollOffset(100000.0);
    QVERIFY(transfer.availableScrollOffset() > 0.0);
    transfer.setAvailableScrollOffset(-5.0);
    QCOMPARE(transfer.availableScrollOffset(), 0.0);
}

QTEST_MAIN(TestMediaBasics)
#include "tst_media_basics.moc"
