// Periodicity — Video, a self-painted player surface without Qt Multimedia.
//
// The widget is only a surface and a transport. Frames arrive through setFrame
// (push) or a frame provider (pull); the component never decodes, never reads a
// file and never talks to an audio device. The 16 ms ticker advances the
// position while Playing, and pulls a provider frame for each new position.
//
// The visual contract is the Material 3 media control: a bottom scrim that
// fades from transparent to 60% black, a large play/pause on the left, the
// elapsed and total time in tabular figures, a thin progress track (buffered
// segment, played segment, round thumb) that can be dragged to seek, and
// volume plus full-screen on the right. The controls are hover-revealed and
// stay visible while paused; the seek is a drag, so the ticker is suspended
// for its duration.

#include <md/components/video.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QTimer>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace md {

namespace {

constexpr int kTickIntervalMs = 16;
constexpr int kFadeIntervalMs = 16;
constexpr qint64 kSeekStepMs = 5000;
constexpr qreal kVolumeStep = 0.05;

// The M3 media bar is 56 dp high; the play button is the large 48 dp target,
// the two right-hand buttons the 40 dp compact one.
constexpr qreal kBarHeightDp = 56.0;
constexpr qreal kPlayButtonDp = 48.0;
constexpr qreal kSmallButtonDp = 40.0;
constexpr qreal kBarPaddingDp = 8.0;
// The bar is 56 dp, so a 4 dp vertical margin leaves the full 48 dp M3 icon
// target for the transport. The bar padding positions the *row*, not the
// target: using it for both shrank the 48 dp play button to 40 dp.
constexpr qreal kButtonMarginDp = 4.0;
constexpr qreal kButtonGapDp = 8.0;
constexpr qreal kTimeWidthDp = 104.0;
constexpr qreal kTimeToTrackGapDp = 12.0;
constexpr qreal kTrackHeightDp = 4.0;
constexpr qreal kThumbRadiusDp = 6.0;
constexpr qreal kPlaceholderRadiusDp = 36.0;

// The scrim reaches from 60% black at the bottom edge up to nothing about two
// and a half bars above it, so the controls never sit on a hard black strip.
constexpr qreal kScrimBars = 2.4;
constexpr qreal kScrimOpacity = 0.6;

const QColor kLetterboxColour = QColor(0, 0, 0);
const QColor kPlaceholderColour = QColor(16, 16, 16);
const QColor kControlTextColour = QColor(255, 255, 255, 235);
const QColor kTrackColour = QColor(255, 255, 255, 60);
const QColor kBufferedColour = QColor(255, 255, 255, 115);
const QColor kThumbColour = QColor(255, 255, 255);

QFont fontFor(const TextStyle& style, const Density& density, const QWidget* context)
{
    return Font::resolve(context, style, density);
}

// The time label ticks once a second; proportional digits would make it
// twitch, so the label asks the font for tabular figures.
QFont tabularFont(const TextStyle& style, const Density& density, const QWidget* context)
{
    QFont font = fontFor(style, density, context);
    font.setFeature(QFont::Tag("tnum"), 1);
    return font;
}

QString formatTime(qint64 milliseconds)
{
    const qint64 secondsTotal = std::max<qint64>(0, milliseconds) / 1000;
    const qint64 seconds = secondsTotal % 60;
    const qint64 minutes = secondsTotal / 60;

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

qreal barPadding(const QRectF& bar, const Density& density)
{
    return std::min(density.dp(kBarPaddingDp), bar.height() / 6.0);
}

qreal buttonSide(const QRectF& bar, qreal preferredDp, const Density& density)
{
    const qreal margin = density.dp(kButtonMarginDp);
    return std::max(qreal(0.0),
                    std::min(density.dp(preferredDp), bar.height() - margin * 2.0));
}

} // namespace

Video::Video(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));

    ticker_ = new QTimer(this);
    ticker_->setObjectName(QStringLiteral("md.components.Video.ticker"));
    ticker_->setInterval(kTickIntervalMs);
    ticker_->setTimerType(Qt::PreciseTimer);
    connect(ticker_, &QTimer::timeout, this, &Video::onTick);

    fadeTimer_ = new QTimer(this);
    fadeTimer_->setObjectName(QStringLiteral("md.components.Video.controlsFade"));
    fadeTimer_->setInterval(kFadeIntervalMs);
    connect(fadeTimer_, &QTimer::timeout, this, [this] { stepControlsFade(); });

    // A stopped surface shows its transport: there is nothing to watch yet and
    // the controls are the only way to start.
    controlsOpacity_ = 1.0;
    controlsTarget_ = 1.0;
}

void Video::setFrame(const QImage& frame)
{
    frame_ = frame;
    emit frameChanged();
    requestUpdate();
}

QImage Video::frame() const
{
    return frame_;
}

bool Video::hasFrame() const noexcept
{
    return !frame_.isNull();
}

void Video::clearFrame()
{
    setFrame(QImage());
}

void Video::setFrameProvider(std::function<QImage(qint64)> provider)
{
    provider_ = std::move(provider);

    if (provider_)
        pullFrame();

    requestUpdate();
}

bool Video::hasFrameProvider() const noexcept
{
    return static_cast<bool>(provider_);
}

void Video::setDuration(qint64 ms)
{
    const qint64 next = std::max<qint64>(0, ms);
    if (duration_ == next)
        return;

    duration_ = next;
    if (position_ > duration_)
        setPositionInternal(duration_, true);

    // A known length with no explicit buffer fills the track: the host that
    // pushed a frame usually holds the whole clip. A buffer that was reported
    // explicitly is only clamped.
    if (duration_ <= 0)
        buffered_ = 0;
    else
        buffered_ = std::min(buffered_ > 0 ? buffered_ : duration_, duration_);

    emit durationChanged(duration_);
    requestUpdate();
}

qint64 Video::duration() const noexcept
{
    return duration_;
}

void Video::setBuffered(qint64 ms)
{
    const qint64 next = std::max<qint64>(0, ms);
    const qint64 clamped = duration_ > 0 ? std::min(next, duration_) : next;
    if (buffered_ == clamped)
        return;

    buffered_ = clamped;
    requestUpdate();
}

qint64 Video::buffered() const noexcept
{
    return buffered_;
}

void Video::setPosition(qint64 ms)
{
    setPositionInternal(clampPosition(ms), true);
}

qint64 Video::position() const noexcept
{
    return position_;
}

void Video::setState(PlaybackState state)
{
    applyState(state);
}

Video::PlaybackState Video::state() const noexcept
{
    return state_;
}

void Video::play()
{
    applyState(PlaybackState::Playing);
}

void Video::pause()
{
    applyState(PlaybackState::Paused);
}

void Video::stop()
{
    applyState(PlaybackState::Stopped);
    setPositionInternal(0, true);
}

void Video::togglePlayback()
{
    applyState(state_ == PlaybackState::Playing ? PlaybackState::Paused
                                                : PlaybackState::Playing);
}

void Video::setVolume(qreal volume)
{
    const qreal clamped = std::clamp(volume, qreal(0.0), qreal(1.0));
    if (qFuzzyCompare(volume_ + 1.0, clamped + 1.0))
        return;

    volume_ = clamped;
    emit volumeChanged(volume_);
    requestUpdate();
}

qreal Video::volume() const noexcept
{
    return volume_;
}

void Video::setMuted(bool muted)
{
    if (muted_ == muted)
        return;

    muted_ = muted;
    emit mutedChanged(muted_);
    requestUpdate();
}

bool Video::isMuted() const noexcept
{
    return muted_;
}

void Video::setLoop(bool loop)
{
    if (loop_ == loop)
        return;

    loop_ = loop;
    requestUpdate();
}

bool Video::loop() const noexcept
{
    return loop_;
}

bool Video::controlsVisible() const noexcept
{
    return controlsOpacity_ > 0.01;
}

bool Video::isScrubbing() const noexcept
{
    return scrubbing_;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

QRectF Video::controlBarRect() const
{
    const QRectF content = contentRect();
    if (content.isEmpty())
        return QRectF();

    const qreal height =
        std::min(Theme::forWidget(this).density().dp(kBarHeightDp), content.height());
    return QRectF(content.left(), content.bottom() - height, content.width(), height);
}

QRectF Video::playButtonRect() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal padding = barPadding(bar, density);
    const qreal side = buttonSide(bar, kPlayButtonDp, density);

    return QRectF(bar.left() + padding, bar.center().y() - side / 2.0, side, side);
}

QRectF Video::fullscreenButtonRect() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal padding = barPadding(bar, density);
    const qreal side = buttonSide(bar, kSmallButtonDp, density);

    return QRectF(bar.right() - padding - side, bar.center().y() - side / 2.0, side, side);
}

QRectF Video::volumeButtonRect() const
{
    const QRectF bar = controlBarRect();
    const QRectF fullscreen = fullscreenButtonRect();
    if (bar.isEmpty() || fullscreen.isEmpty())
        return QRectF();

    const qreal gap = Theme::forWidget(this).density().dp(kButtonGapDp);

    return QRectF(fullscreen.left() - gap - fullscreen.width(), fullscreen.top(),
                  fullscreen.width(), fullscreen.height());
}

QRectF Video::timeLabelRectFor(const QRectF& bar, const QRectF& play,
                               const QRectF& volume) const
{
    const Density& density = Theme::forWidget(this).density();
    const qreal gap = density.dp(kButtonGapDp);
    const qreal left = play.right() + gap;
    const qreal available = std::max(qreal(0.0), volume.left() - gap - left);

    // The label may shrink on a narrow surface, but it never eats the track:
    // 48 dp plus the gap stay reserved for the progress bar.
    const qreal maximum =
        std::max(qreal(0.0), available - density.dp(kTimeToTrackGapDp) - density.dp(48.0));
    const qreal width = std::min(density.dp(kTimeWidthDp), maximum);

    return QRectF(left, bar.top(), width, bar.height());
}

QRectF Video::timeLabelRect() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    return timeLabelRectFor(bar, playButtonRect(), volumeButtonRect());
}

QRectF Video::progressRect() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const QRectF time = timeLabelRectFor(bar, playButtonRect(), volumeButtonRect());
    const qreal gap = density.dp(kTimeToTrackGapDp);
    const qreal left = time.right() + gap;
    const qreal right = volumeButtonRect().left() - gap;
    const qreal trackHeight =
        std::min(density.dp(kTrackHeightDp), std::max(qreal(0.0), bar.height() / 3.0));

    return QRectF(left, bar.center().y() - trackHeight / 2.0,
                  std::max(qreal(0.0), right - left), trackHeight);
}

QRectF Video::scrimRect() const
{
    const QRectF content = contentRect();
    const QRectF bar = controlBarRect();
    if (content.isEmpty() || bar.isEmpty())
        return QRectF();

    const qreal height = std::min(content.height(), bar.height() * kScrimBars);
    return QRectF(content.left(), content.bottom() - height, content.width(), height);
}

QRectF Video::progressHitRect() const
{
    const QRectF track = progressRect();
    if (track.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal grow =
        std::max(density.dp(8.0), (controlBarRect().height() - track.height()) / 2.0);
    return track.adjusted(0.0, -grow, 0.0, grow);
}

void Video::measureOverride(MeasureSpec width, MeasureSpec height)
{
    const Density& density = Theme::forWidget(this).density();
    setMeasuredSize(QSizeF(width.resolve(density.dp(320.0)),
                           height.resolve(density.dp(180.0))));
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void Video::paintContent(QPainter& painter, const QRectF& content)
{
    if (content.isEmpty())
        return;

    painter.save();
    painter.fillRect(content, kLetterboxColour);
    painter.setClipRect(content);

    const QSizeF imageSize = frame_.deviceIndependentSize();
    if (!frame_.isNull() && !imageSize.isEmpty()) {
        const qreal scale = std::min(content.width() / imageSize.width(),
                                     content.height() / imageSize.height());
        const QSizeF targetSize(imageSize.width() * scale, imageSize.height() * scale);
        const QRectF target(content.center().x() - targetSize.width() / 2.0,
                            content.center().y() - targetSize.height() / 2.0,
                            targetSize.width(), targetSize.height());

        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(target, frame_);
    } else {
        painter.fillRect(content, kPlaceholderColour);
        paintPlaceholder(painter, content);
    }

    paintScrim(painter);
    paintControls(painter, content);
    painter.restore();
}

void Video::paintScrim(QPainter& painter)
{
    if (controlsOpacity_ <= 0.01)
        return;

    const QRectF scrim = scrimRect();
    if (scrim.isEmpty())
        return;

    QLinearGradient gradient(scrim.topLeft(), scrim.bottomLeft());
    // The scrim is the theme's `scrim` role, not a literal black: it stays the
    // same colour in both schemes by definition, but it is still a token, so a
    // scheme that redefines scrim moves the whole media overlay with it.
    const QColor scrimColour = Theme::forWidget(this).colours().scrim;
    QColor scrimTop = scrimColour;
    scrimTop.setAlpha(0);
    QColor scrimBottom = scrimColour;
    scrimBottom.setAlphaF(static_cast<float>(kScrimOpacity));
    gradient.setColorAt(0.0, scrimTop);
    gradient.setColorAt(1.0, scrimBottom);

    painter.save();
    painter.setOpacity(controlsOpacity_);
    painter.fillRect(scrim, QBrush(gradient));
    painter.restore();
}

void Video::paintPlaceholder(QPainter& painter, const QRectF& content)
{
    const Density& density = Theme::forWidget(this).density();
    const qreal radius = std::min(
        density.dp(kPlaceholderRadiusDp),
        std::min(content.width(), content.height()) / 4.0);
    if (radius <= 0.0)
        return;

    const QPointF center = content.center();
    const qreal side = radius * 0.85;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 36));
    painter.drawEllipse(center, radius, radius);

    QPainterPath triangle;
    triangle.moveTo(center.x() - side * 0.35, center.y() - side * 0.55);
    triangle.lineTo(center.x() - side * 0.35, center.y() + side * 0.55);
    triangle.lineTo(center.x() + side * 0.6, center.y());
    triangle.closeSubpath();

    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawPath(triangle);
    painter.restore();
}

void Video::paintControls(QPainter& painter, const QRectF& content)
{
    Q_UNUSED(content);
    if (controlsOpacity_ <= 0.01)
        return;

    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const QRectF play = playButtonRect();
    const QRectF volume = volumeButtonRect();
    const QRectF fullscreen = fullscreenButtonRect();
    const QRectF time = timeLabelRectFor(bar, play, volume);
    const QRectF progress = progressRect();

    painter.save();
    painter.setOpacity(controlsOpacity_);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Left: the large transport button.
    paintPlayPause(painter, play);

    // Time label: elapsed and total, tabular figures, before the track.
    if (time.width() > 0.0) {
        const QFont font = tabularFont(theme.type().labelSmall, density, this);
        const QFontMetricsF metrics(font);
        const QString text =
            formatTime(position_) + QStringLiteral(" / ") + formatTime(duration_);
        const QString elided = metrics.elidedText(text, Qt::ElideRight, time.width());
        painter.setFont(font);
        painter.setPen(kControlTextColour);
        painter.drawText(time, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter), elided);
    }

    // The track: unplayed, buffered, played, thumb.
    if (!progress.isEmpty() && progress.width() > 0.0) {
        const qreal fraction = progressFraction();
        const qreal bufferedFraction = duration_ > 0
            ? std::clamp(static_cast<qreal>(buffered_) / static_cast<qreal>(duration_),
                         qreal(0.0), qreal(1.0))
            : 0.0;
        const qreal radius = progress.height() / 2.0;

        painter.setPen(Qt::NoPen);
        painter.setBrush(kTrackColour);
        painter.drawRoundedRect(progress, radius, radius);

        if (bufferedFraction > fraction) {
            painter.setBrush(kBufferedColour);
            painter.drawRoundedRect(
                QRectF(progress.left(), progress.top(), progress.width() * bufferedFraction,
                       progress.height()),
                radius, radius);
        }

        const qreal played = progress.width() * fraction;
        if (played > 0.0) {
            painter.setBrush(theme.colours().primary);
            painter.drawRoundedRect(
                QRectF(progress.left(), progress.top(), played, progress.height()), radius,
                radius);
        }

        const qreal thumbRadius = std::min(
            density.dp(kThumbRadiusDp), std::max(qreal(0.0), bar.height() / 4.0));
        painter.setBrush(kThumbColour);
        painter.drawEllipse(QPointF(progress.left() + played, progress.center().y()),
                            thumbRadius, thumbRadius);
    }

    // Right: volume and full-screen.
    paintVolume(painter, volume);
    paintFullscreen(painter, fullscreen);

    painter.restore();
}

void Video::paintPlayPause(QPainter& painter, const QRectF& rect)
{
    if (rect.isEmpty())
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kControlTextColour);

    if (state_ == PlaybackState::Playing) {
        const qreal barWidth = std::max(qreal(1.0), rect.width() * 0.15);
        const qreal gap = rect.width() * 0.11;
        const qreal height = rect.height() * 0.42;
        const qreal top = rect.center().y() - height / 2.0;
        painter.drawRoundedRect(QRectF(rect.center().x() - gap - barWidth, top, barWidth,
                                       height),
                                barWidth / 3.0, barWidth / 3.0);
        painter.drawRoundedRect(QRectF(rect.center().x() + gap, top, barWidth, height),
                                barWidth / 3.0, barWidth / 3.0);
    } else {
        QPainterPath triangle;
        const qreal side = rect.height() * 0.42;
        triangle.moveTo(rect.center().x() - side * 0.4, rect.center().y() - side * 0.7);
        triangle.lineTo(rect.center().x() - side * 0.4, rect.center().y() + side * 0.7);
        triangle.lineTo(rect.center().x() + side * 0.8, rect.center().y());
        triangle.closeSubpath();
        painter.drawPath(triangle);
    }

    painter.restore();
}

void Video::paintVolume(QPainter& painter, const QRectF& rect)
{
    if (rect.isEmpty())
        return;

    const Density& density = Theme::forWidget(this).density();
    const QPointF center = rect.center();
    const qreal size = std::min(rect.width(), rect.height()) * 0.5;
    const qreal stroke = std::max(qreal(1.0), density.dp(1.5));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QPolygonF speaker;
    speaker << QPointF(center.x() - size * 0.7, center.y() - size * 0.25)
            << QPointF(center.x() - size * 0.4, center.y() - size * 0.25)
            << QPointF(center.x() - size * 0.05, center.y() - size * 0.6)
            << QPointF(center.x() - size * 0.05, center.y() + size * 0.6)
            << QPointF(center.x() - size * 0.4, center.y() + size * 0.25)
            << QPointF(center.x() - size * 0.7, center.y() + size * 0.25);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kControlTextColour);
    painter.drawPolygon(speaker);

    QPen pen(kControlTextColour, stroke, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (muted_) {
        const qreal cross = size * 0.35;
        painter.drawLine(QPointF(center.x() + size * 0.2, center.y() - cross),
                         QPointF(center.x() + size * 0.2 + cross * 2.0,
                                 center.y() + cross));
        painter.drawLine(QPointF(center.x() + size * 0.2, center.y() + cross),
                         QPointF(center.x() + size * 0.2 + cross * 2.0,
                                 center.y() - cross));
    } else if (volume_ > 0.0) {
        const qreal arcSize = size * (0.55 + volume_ * 0.45);
        const QRectF arc(center.x() - arcSize / 2.0, center.y() - arcSize,
                         arcSize * 2.0, arcSize * 2.0);
        painter.drawArc(arc, -45 * 16, 90 * 16);
        if (volume_ > 0.5) {
            const QRectF outer(center.x() - arcSize * 0.75, center.y() - arcSize * 1.35,
                               arcSize * 2.8, arcSize * 2.8);
            painter.drawArc(outer, -45 * 16, 90 * 16);
        }
    }

    painter.restore();
}

void Video::paintFullscreen(QPainter& painter, const QRectF& rect)
{
    if (rect.isEmpty())
        return;

    const Density& density = Theme::forWidget(this).density();
    const qreal stroke = std::max(qreal(1.0), density.dp(1.8));
    const qreal arm = std::min(rect.width(), rect.height()) * 0.28;
    const QRectF frame = rect.adjusted(rect.width() * 0.22, rect.height() * 0.22,
                                       -rect.width() * 0.22, -rect.height() * 0.22);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(kControlTextColour, stroke, Qt::SolidLine, Qt::SquareCap));
    painter.setBrush(Qt::NoBrush);

    painter.drawLine(frame.topLeft(), frame.topLeft() + QPointF(arm, 0.0));
    painter.drawLine(frame.topLeft(), frame.topLeft() + QPointF(0.0, arm));
    painter.drawLine(frame.topRight(), frame.topRight() - QPointF(arm, 0.0));
    painter.drawLine(frame.topRight(), frame.topRight() + QPointF(0.0, arm));
    painter.drawLine(frame.bottomLeft(), frame.bottomLeft() + QPointF(arm, 0.0));
    painter.drawLine(frame.bottomLeft(), frame.bottomLeft() - QPointF(0.0, arm));
    painter.drawLine(frame.bottomRight(), frame.bottomRight() - QPointF(arm, 0.0));
    painter.drawLine(frame.bottomRight(), frame.bottomRight() - QPointF(0.0, arm));

    painter.restore();
}

// ---------------------------------------------------------------------------
// Position and state
// ---------------------------------------------------------------------------

qint64 Video::clampPosition(qint64 ms) const
{
    const qint64 low = std::max<qint64>(0, ms);
    if (duration_ <= 0)
        return low;

    return std::min(low, duration_);
}

qint64 Video::positionForX(qreal x) const
{
    const QRectF track = progressRect();
    if (track.width() <= 0.0 || duration_ <= 0)
        return 0;

    const qreal fraction =
        std::clamp((x - track.left()) / track.width(), qreal(0.0), qreal(1.0));
    return qRound64(fraction * static_cast<qreal>(duration_));
}

qreal Video::progressFraction() const
{
    if (duration_ <= 0)
        return 0.0;

    return std::clamp(static_cast<qreal>(position_) / static_cast<qreal>(duration_),
                      qreal(0.0), qreal(1.0));
}

// ---------------------------------------------------------------------------
// Controls reveal
// ---------------------------------------------------------------------------

int Video::fadeDurationMs() const
{
    return static_cast<int>(Theme::forWidget(this).motion().short4().duration.count());
}

void Video::updateControlsTarget()
{
    const bool visible = hovered_ || scrubbing_ || state_ != PlaybackState::Playing;
    controlsTarget_ = visible ? 1.0 : 0.0;

    if (fadeTimer_ == nullptr)
        return;

    if (fadeDurationMs() <= 0) {
        // Reduced motion: the controls appear and disappear in one step.
        fadeTimer_->stop();
        if (!qFuzzyCompare(controlsOpacity_ + 1.0, controlsTarget_ + 1.0)) {
            controlsOpacity_ = controlsTarget_;
            requestUpdate();
        }
        return;
    }

    if (qFuzzyCompare(controlsOpacity_ + 1.0, controlsTarget_ + 1.0)) {
        fadeTimer_->stop();
        return;
    }

    if (!fadeTimer_->isActive())
        fadeTimer_->start();
}

void Video::stepControlsFade()
{
    if (fadeTimer_ == nullptr)
        return;

    const int duration = fadeDurationMs();
    if (duration <= 0) {
        fadeTimer_->stop();
        controlsOpacity_ = controlsTarget_;
        requestUpdate();
        return;
    }

    const qreal step = qreal(fadeTimer_->interval()) / qreal(duration);
    if (controlsOpacity_ < controlsTarget_)
        controlsOpacity_ = std::min(controlsTarget_, controlsOpacity_ + step);
    else
        controlsOpacity_ = std::max(controlsTarget_, controlsOpacity_ - step);

    if (qFuzzyCompare(controlsOpacity_ + 1.0, controlsTarget_ + 1.0))
        fadeTimer_->stop();

    requestUpdate();
}

void Video::enterEvent(QEnterEvent* event)
{
    hovered_ = true;
    updateControlsTarget();
    View::enterEvent(event);
}

void Video::leaveEvent(QEvent* event)
{
    hovered_ = false;
    updateControlsTarget();
    View::leaveEvent(event);
}

// ---------------------------------------------------------------------------
// Ticker
// ---------------------------------------------------------------------------

void Video::startTicker()
{
    if (ticker_ == nullptr || ticker_->isActive())
        return;

    // LifecycleState::Hidden means "was shown and is now hidden": playback must
    // not keep advancing, or pulling provider frames, behind a hidden widget.
    // A never-shown surface still ticks, because play() is a transport request
    // that does not depend on being on screen yet.
    if (lifecycleState() == LifecycleState::Hidden)
        return;

    ticker_->start();
}

void Video::stopTicker()
{
    if (ticker_ != nullptr)
        ticker_->stop();
}

void Video::onShown()
{
    // A surface that was hidden while playing resumes where it froze; the
    // playback state itself never changed, so no signal is emitted.
    if (state_ == PlaybackState::Playing)
        startTicker();

    updateControlsTarget();
}

void Video::onHidden()
{
    stopTicker();
    if (fadeTimer_ != nullptr)
        fadeTimer_->stop();
}

void Video::onTick()
{
    if (state_ != PlaybackState::Playing) {
        stopTicker();
        return;
    }

    const qint64 next = position_ + kTickIntervalMs;
    if (duration_ > 0 && next >= duration_) {
        if (!loop_) {
            setPositionInternal(duration_, true);
            applyState(PlaybackState::Stopped);
            return;
        }

        position_ = 0;
        emit positionChanged(position_);
        pullFrame();
        requestUpdate();
        return;
    }

    position_ = next;
    emit positionChanged(position_);
    pullFrame();
    requestUpdate();
}

void Video::applyState(PlaybackState next)
{
    if (state_ == next)
        return;

    if (next == PlaybackState::Playing) {
        if (duration_ > 0 && position_ >= duration_) {
            position_ = 0;
            emit positionChanged(position_);
        }
        state_ = next;
        startTicker();
        if (provider_)
            pullFrame();
    } else {
        state_ = next;
        stopTicker();
    }

    // Playback hides the controls unless the pointer is on them; pausing keeps
    // them on screen so the frame can be studied.
    updateControlsTarget();

    emit playbackStateChanged(state_);
    requestUpdate();
}

void Video::setPositionInternal(qint64 ms, bool pullFrameNow)
{
    const qint64 next = clampPosition(ms);
    if (position_ == next)
        return;

    position_ = next;
    emit positionChanged(position_);
    if (pullFrameNow)
        pullFrame();

    requestUpdate();
}

void Video::pullFrame()
{
    if (!provider_)
        return;

    const QImage next = provider_(position_);
    if (next.isNull())
        return;

    frame_ = next;
    emit frameChanged();
    requestUpdate();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void Video::mousePressEvent(QMouseEvent* event)
{
    View::mousePressEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    setFocus(Qt::MouseFocusReason);
    hovered_ = true;
    updateControlsTarget();

    const QPointF position = event->position();

    if (playButtonRect().contains(position)) {
        togglePlayback();
        event->accept();
        return;
    }

    if (volumeButtonRect().contains(position)) {
        setMuted(!muted_);
        event->accept();
        return;
    }

    if (fullscreenButtonRect().contains(position)) {
        emit fullscreenRequested();
        event->accept();
        return;
    }

    if (!progressHitRect().isEmpty() && progressHitRect().contains(position)) {
        if (duration_ > 0) {
            scrubbing_ = true;
            // A seek in flight must not fight the ticker, or the thumb would
            // jump forward between two move events.
            scrubResume_ = state_ == PlaybackState::Playing;
            if (scrubResume_)
                stopTicker();
            setPosition(positionForX(position.x()));
            updateControlsTarget();
        }
        event->accept();
        return;
    }

    if (position.y() < controlBarRect().top() && contentRect().contains(position)) {
        togglePlayback();
        event->accept();
    }
}

void Video::mouseMoveEvent(QMouseEvent* event)
{
    View::mouseMoveEvent(event);

    if (!scrubbing_)
        return;

    hovered_ = true;
    setPosition(positionForX(event->position().x()));
    event->accept();
}

void Video::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    if (scrubbing_) {
        setPosition(positionForX(event->position().x()));
        scrubbing_ = false;
        if (scrubResume_)
            startTicker();
        scrubResume_ = false;
        updateControlsTarget();
    }

    event->accept();
}

void Video::keyPressEvent(QKeyEvent* event)
{
    bool handled = true;

    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        togglePlayback();
        break;
    case Qt::Key_Left:
        setPosition(position_ - kSeekStepMs);
        break;
    case Qt::Key_Right:
        setPosition(position_ + kSeekStepMs);
        break;
    case Qt::Key_Up:
        setMuted(false);
        setVolume(volume_ + kVolumeStep);
        break;
    case Qt::Key_Down:
        setVolume(volume_ - kVolumeStep);
        break;
    case Qt::Key_M:
        setMuted(!muted_);
        break;
    case Qt::Key_F:
        emit fullscreenRequested();
        break;
    case Qt::Key_Home:
        setPosition(0);
        break;
    case Qt::Key_End:
        if (duration_ > 0)
            setPosition(duration_);
        break;
    default:
        handled = false;
        break;
    }

    if (!handled) {
        View::keyPressEvent(event);
        return;
    }

    event->accept();
}

} // namespace md
