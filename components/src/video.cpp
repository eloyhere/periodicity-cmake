// Periodicity — Video, a self-painted player surface without Qt Multimedia.
//
// The widget is only a surface and a transport. Frames arrive through setFrame
// (push) or a frame provider (pull); the component never decodes, never reads a
// file and never talks to an audio device. The 16 ms ticker advances the
// position while Playing, and pulls a provider frame for each new position.
//
// The visual contract is the baseline media control: a bottom scrim that
// fades from transparent to 60% black, a large play/pause on the left, the
// elapsed and total time in tabular figures, a thin progress track (buffered
// segment, played segment, round thumb) that can be dragged to seek, and
// volume, settings and full-screen on the right. The controls are
// hover-revealed and stay visible while paused; the seek is a drag, so the
// ticker is suspended for its duration.
//
// The icons do not come from a font and are not fitted by eye per call site:
// videoIcons() below is the whole set: the filled 24 dp path data parsed
// once at start-up. Every glyph is centred in a 48 dp target by one painter, so
// the eleven shapes share one grid, one weight and one optical centre.

#include <md/components/video.h>

#include <md/components/font.h>
#include <md/theme/theme.h>

#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace md {

namespace {

constexpr int kTickIntervalMs = 16;
constexpr int kFadeIntervalMs = 16;
constexpr qint64 kSeekStepMs = 5000;
constexpr qreal kVolumeStep = 0.05;

// Layout, in dp. The bar is the baseline 56 dp media strip. Every action keeps a
// full 48 dp target inside it, with 8 dp between neighbours and 12 dp to the
// edge, so a finger never lands between two actions and a pointer never has to
// aim at a 40 dp compromise.
constexpr qreal kBarHeightDp = 56.0;
constexpr qreal kBarInsetDp = 12.0;
constexpr qreal kTouchTargetDp = 48.0;
constexpr qreal kControlGapDp = 8.0;
constexpr qreal kIconDp = 24.0;
constexpr qreal kIconGridDp = 24.0;
constexpr qreal kTimeWidthDp = 104.0;
constexpr qreal kTimeMinWidthDp = 56.0;
constexpr qreal kTimeToTrackGapDp = 12.0;
constexpr qreal kTrackHeightDp = 4.0;
constexpr qreal kMinTrackDp = 48.0;
constexpr qreal kThumbRadiusDp = 6.0;
constexpr qreal kPlaceholderRadiusDp = 36.0;
constexpr qreal kPlaceholderIconDp = 40.0;

// Below this width the bar keeps the primary transport (play, volume, settings,
// full screen) and puts replay and the two seeks into the settings panel: the
// alternative — a 40 dp or 32 dp target — is what the touch model forbids.
constexpr qreal kFullBarMinDp = 600.0;

// State layers and the disabled ink (M3 media controls: 12% hover, 16% press,
// 38% for a disabled action).
constexpr qreal kHoverLayerAlpha = 0.12;
constexpr qreal kPressedLayerAlpha = 0.16;
constexpr qreal kDisabledAlpha = 0.38;
constexpr qreal kStateLayerDp = 40.0;

// The settings panel.
constexpr qreal kPanelWidthDp = 216.0;
constexpr qreal kPanelPaddingDp = 8.0;
constexpr qreal kPanelRowHeightDp = 40.0;
constexpr qreal kPanelSeparatorHeightDp = 9.0;
constexpr qreal kPanelRadiusDp = 12.0;
constexpr qreal kPanelOffsetDp = 8.0;
constexpr int kPanelRowCount = 4; // replay, back, forward, loop

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

// The bar is an overlay on the letterbox, not a themed surface: the picture
// underneath is arbitrary, so its content uses the light on-scrim pair, exactly
// as the elapsed time always has. These are the fallbacks `onScrimInk()` keeps
// for the schemes whose surface roles are too dark to read on the scrim.
const QColor kScrimInk = QColor(255, 255, 255, 235);
const QColor kScrimInkActive = QColor(255, 255, 255);

QColor withAlpha(const QColor& colour, qreal alpha)
{
    QColor result = colour;
    result.setAlphaF(static_cast<float>(std::clamp(alpha, qreal(0.0), qreal(1.0))));
    return result;
}

qreal linearChannel(qreal channel)
{
    return channel <= 0.03928 ? channel / 12.92
                              : std::pow((channel + 0.055) / 1.055, 2.4);
}

// WCAG relative luminance: the scrim is black, so a role needs a luminance of
// about 0.1 to clear 3:1 against it.
qreal relativeLuminance(const QColor& colour)
{
    return 0.2126 * linearChannel(colour.redF()) + 0.7152 * linearChannel(colour.greenF())
        + 0.0722 * linearChannel(colour.blueF());
}

// The bar paints on the scrim, so a surface role is only usable when it is light
// enough to read there. The dark scheme's onSurface/onSurfaceVariant are; the
// light scheme's are not (#1D1B20 on a 60% black scrim is invisible), so those
// keep the on-scrim pair. The emphasis step is preserved either way, which is
// what the state model is about.
QColor onScrimInk(const QColor& role, const QColor& fallback)
{
    return relativeLuminance(role) >= 0.1 ? role : fallback;
}

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

// Rounds a logical coordinate onto the device pixel grid, so a 24 dp icon at a
// fractional density still lands on whole device pixels instead of a half-pixel
// seam that antialiasing turns into a grey fringe.
qreal snapToPixel(qreal value, qreal ratio)
{
    if (ratio <= 0.0)
        return std::round(value);
    return std::round(value * ratio) / ratio;
}

// ---------------------------------------------------------------------------
// The icon set
// ---------------------------------------------------------------------------

// The eleven shapes the surface can paint: the transport, the volume pair, the
// settings/menu pair, full screen in both directions, the two seeks and the
// check the settings panel marks a toggle with.
enum class Glyph {
    Play,
    Pause,
    Replay,
    VolumeOn,
    VolumeMuted,
    Settings,
    FullscreenEnter,
    FullscreenExit,
    SkipBack,
    SkipForward,
    Check,
};

// The set is uniformly filled. A media bar is read at a glance over moving
// picture, and the filled 24 dp paths carry more weight per pixel than a
// 2 dp outline does at this size; mixing the two was what made the old bar look
// assembled rather than designed. Each entry below is the canonical reference
// `filled` path, read once by a small SVG reader so the table stays reviewable
// as the path data it is instead of a wall of cubic control points.

struct SvgPathReader
{
    QString data;
    int index = 0;

    void skipSeparators()
    {
        while (index < data.size()
               && (data.at(index).isSpace() || data.at(index) == QLatin1Char(',')))
            ++index;
    }

    [[nodiscard]] bool atEnd()
    {
        skipSeparators();
        return index >= data.size();
    }

    // Returns the next command character, or a null QChar when the next token
    // continues the previous command (SVG's implicit repetition).
    QChar takeCommand()
    {
        skipSeparators();
        if (index >= data.size() || !data.at(index).isLetter())
            return QChar();
        return data.at(index++);
    }

    bool number(qreal& value)
    {
        skipSeparators();
        const int start = index;
        if (index < data.size()
            && (data.at(index) == QLatin1Char('-') || data.at(index) == QLatin1Char('+')))
            ++index;
        while (index < data.size() && data.at(index).isDigit())
            ++index;
        if (index < data.size() && data.at(index) == QLatin1Char('.')) {
            ++index;
            while (index < data.size() && data.at(index).isDigit())
                ++index;
        }
        if (index == start)
            return false;
        value = data.mid(start, index - start).toDouble();
        return true;
    }

    bool point(QPointF& value)
    {
        qreal x = 0.0;
        qreal y = 0.0;
        if (!number(x) || !number(y))
            return false;
        value = QPointF(x, y);
        return true;
    }
};

QPainterPath pathFromSvg(const char* source)
{
    SvgPathReader reader{QString::fromLatin1(source)};
    QPainterPath path;
    // The path data is authored for the non-zero fill rule: the settings
    // hole and the volume slash both depend on it.
    path.setFillRule(Qt::WindingFill);

    QPointF current;
    QPointF subpath;
    QPointF control;
    QChar command;
    bool hasControl = false;

    while (!reader.atEnd()) {
        const QChar next = reader.takeCommand();
        if (!next.isNull())
            command = next;
        if (command.isNull())
            break;

        const bool relative = command.isLower();
        switch (command.toLatin1()) {
        case 'M':
        case 'm': {
            QPointF value;
            if (!reader.point(value))
                return path;
            current = relative ? current + value : value;
            subpath = current;
            path.moveTo(current);
            hasControl = false;
            // A repeated moveto coordinate pair is an implicit lineto.
            command = relative ? QLatin1Char('l') : QLatin1Char('L');
            break;
        }
        case 'L':
        case 'l': {
            QPointF value;
            if (!reader.point(value))
                return path;
            current = relative ? current + value : value;
            path.lineTo(current);
            hasControl = false;
            break;
        }
        case 'H':
        case 'h': {
            qreal value = 0.0;
            if (!reader.number(value))
                return path;
            current.setX(relative ? current.x() + value : value);
            path.lineTo(current);
            hasControl = false;
            break;
        }
        case 'V':
        case 'v': {
            qreal value = 0.0;
            if (!reader.number(value))
                return path;
            current.setY(relative ? current.y() + value : value);
            path.lineTo(current);
            hasControl = false;
            break;
        }
        case 'C':
        case 'c': {
            QPointF first;
            QPointF second;
            QPointF end;
            if (!reader.point(first) || !reader.point(second) || !reader.point(end))
                return path;
            if (relative) {
                first += current;
                second += current;
                end += current;
            }
            path.cubicTo(first, second, end);
            control = second;
            current = end;
            hasControl = true;
            break;
        }
        case 'S':
        case 's': {
            QPointF second;
            QPointF end;
            if (!reader.point(second) || !reader.point(end))
                return path;
            if (relative) {
                second += current;
                end += current;
            }
            // The shorthand's first control point is the reflection of the
            // previous one through the current point.
            const QPointF first = hasControl ? current * 2.0 - control : current;
            path.cubicTo(first, second, end);
            control = second;
            current = end;
            hasControl = true;
            break;
        }
        case 'Z':
        case 'z':
            path.closeSubpath();
            current = subpath;
            hasControl = false;
            break;
        default:
            return path;
        }
    }

    return path;
}

// The settings glyph fills almost the whole 24 dp box; at 88% around the grid
// centre it sits a touch under the play triangle's optical weight instead of
// dominating the bar.
QPainterPath settingsPath()
{
    const QPainterPath gear = pathFromSvg(
        "M19.43 12.98c.04-.32.07-.64.07-.98s-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42"
        ".12-.64l-2-3.46c-.12-.22-.39-.3-.61-.22l-2.49 1c-.52-.4-1.08-.73-1.69-.98"
        "l-.38-2.65C14.46 2.18 14.25 2 14 2h-4c-.25 0-.46.18-.49.42l-.38 2.65c-.61"
        ".25-1.17.59-1.69.98l-2.49-1c-.23-.09-.49 0-.61.22l-2 3.46c-.13.22-.07.49"
        ".12.64l2.11 1.65c-.04.32-.07.65-.07.98s.03.66.07.98l-2.11 1.65c-.19.15-.24"
        ".42-.12.64l2 3.46c.12.22.39.3.61.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65"
        "c.03.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.59 1.69"
        "-.98l2.49 1c.23.09.49 0 .61-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.65z"
        "M12 15.5c-1.93 0-3.5-1.57-3.5-3.5s1.57-3.5 3.5-3.5 3.5 1.57 3.5 3.5-1.57 "
        "3.5-3.5 3.5z");

    QTransform shrink;
    shrink.translate(12.0, 12.0);
    shrink.scale(0.88, 0.88);
    shrink.translate(-12.0, -12.0);
    return shrink.map(gear);
}

constexpr std::size_t kGlyphCount = 11;

// Built once. The table is the whole icon set: an action either has a path here
// or it cannot be painted.
const std::array<QPainterPath, kGlyphCount>& videoIcons()
{
    static const std::array<QPainterPath, kGlyphCount> icons = [] {
        std::array<QPainterPath, kGlyphCount> table;
        const auto put = [&table](Glyph glyph, const char* data) {
            table[static_cast<std::size_t>(glyph)] = pathFromSvg(data);
        };

        put(Glyph::Play, "M8 5v14l11-7z");
        put(Glyph::Pause, "M6 5h4v14H6zM14 5h4v14h-4z");
        put(Glyph::Replay,
            "M12 5V1L7 6l5 5V7c3.31 0 6 2.69 6 6s-2.69 6-6 6-6-2.69-6-6H4c0 4.42 "
            "3.58 8 8 8s8-3.58 8-8-3.58-8-8-8z");
        put(Glyph::VolumeOn,
            "M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 "
            "2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71"
            "v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z");
        put(Glyph::VolumeMuted,
            "M16.5 12c0-1.77-1.02-3.29-2.5-4.03v2.21l2.45 2.45c.03-.2.05-.41.05-.63z"
            "m2.5 0c0 .94-.2 1.82-.54 2.64l1.51 1.51C20.63 14.91 21 13.5 21 12c0-4.28"
            "-2.99-7.86-7-8.77v2.06c2.89.86 5 3.54 5 6.71zM4.27 3L3 4.27 7.73 9H3v6h4"
            "l5 5v-6.73l4.25 4.25c-.67.52-1.42.93-2.25 1.18v2.06c1.38-.31 2.63-.95 "
            "3.69-1.81L19.73 21 21 19.73l-9-9L4.27 3zM12 4L9.91 6.09 12 8.18V4z");
        table[static_cast<std::size_t>(Glyph::Settings)] = settingsPath();
        put(Glyph::FullscreenEnter,
            "M7 14H5v5h5v-2H7v-3zm-2-4h2V7h3V5H5v5zm12 7h-3v2h5v-5h-2v3z"
            "M14 5v2h3v3h2V5h-5z");
        put(Glyph::FullscreenExit,
            "M5 16h3v3h2v-5H5v2zm3-8H5v2h5V5H8v3zm6 11h2v-3h3v-2h-5v5z"
            "m2-11V5h-2v5h5V8h-3z");
        put(Glyph::SkipBack, "M11 18V6l-8.5 6 8.5 6zm.5-6l8.5 6V6l-8.5 6z");
        put(Glyph::SkipForward, "M4 18l8.5-6L4 6v12zm9-12v12l8.5-6L13 6z");
        put(Glyph::Check, "M9 16.17L4.83 12l-1.42 1.41L9 19 21 7l-1.41-1.41z");
        return table;
    }();
    return icons;
}

const QPainterPath& iconFor(Glyph glyph)
{
    return videoIcons()[static_cast<std::size_t>(glyph)];
}

// Draws `glyph` centred in `target` at `iconDp`. The grid transform is the only
// place that knows about the 24 dp design grid, so a caller never re-derives an
// icon size from the target rectangle.
void paintGlyph(QPainter& painter, Glyph glyph, const QRectF& target, const QColor& ink,
                const Density& density, qreal iconDp = kIconDp)
{
    if (target.isEmpty() || ink.alpha() == 0)
        return;

    const qreal side = density.dp(iconDp);
    if (side <= 0.0)
        return;

    const QPainterPath& path = iconFor(glyph);
    if (path.isEmpty())
        return;

    const qreal ratio = painter.device() != nullptr ? painter.device()->devicePixelRatioF() : 1.0;
    const QPointF topLeft(snapToPixel(target.center().x() - side / 2.0, ratio),
                          snapToPixel(target.center().y() - side / 2.0, ratio));

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.translate(topLeft);
    painter.scale(side / kIconGridDp, side / kIconGridDp);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawPath(path);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

struct BarLayout
{
    QRectF bar;
    QRectF play;
    QRectF time;
    QRectF track;
    QRectF volume;
    QRectF settings;
    QRectF fullscreen;
    bool hasTime = false;
    std::vector<std::pair<Video::Control, QRectF>> controls; // visual order
};

// The one layout rule the painting, the hit test and the public geometry all
// share. The leading target is pinned to the leading inset; the trailing
// cluster is pinned to the trailing inset and filled right to left; the track
// gets whatever is left, and the time label is the first thing to give way.
BarLayout layoutBar(const QRectF& bar, const Density& density, qreal timeNaturalWidth)
{
    BarLayout layout;
    layout.bar = bar;
    if (bar.isEmpty())
        return layout;

    const qreal inset = density.dp(kBarInsetDp);
    const qreal side = density.dp(kTouchTargetDp);
    const qreal gap = density.dp(kControlGapDp);
    const qreal top = bar.center().y() - side / 2.0;

    if (bar.width() < inset * 2.0 + side)
        return layout;

    layout.play = QRectF(bar.left() + inset, top, side, side);
    const qreal minLeft = layout.play.right() + gap;

    qreal trailingRight = bar.right() - inset;
    const auto placeTrailing = [&](QRectF& target) {
        const QRectF candidate(trailingRight - side, top, side, side);
        if (candidate.left() < minLeft)
            return false;
        target = candidate;
        trailingRight = candidate.left() - gap;
        return true;
    };

    const bool haveFullscreen = placeTrailing(layout.fullscreen);
    const bool haveSettings = haveFullscreen && placeTrailing(layout.settings);
    const bool haveVolume = haveSettings && placeTrailing(layout.volume);

    layout.controls.emplace_back(Video::Control::PlayPause, layout.play);
    qreal leadingRight = layout.play.right();

    // The free span ends at the leading edge of whichever trailing target was
    // placed: volume is the innermost of the three.
    const qreal trailingLeft = haveVolume       ? layout.volume.left()
        : haveSettings                          ? layout.settings.left()
        : haveFullscreen                        ? layout.fullscreen.left()
                                                : bar.right() - inset;

    // The secondary transport only appears when the whole set fits. It is never
    // squeezed: at this point the settings panel carries it instead.
    const bool wide = bar.width() >= density.dp(kFullBarMinDp);
    if (wide && haveVolume) {
        const qreal reserve = density.dp(kTimeMinWidthDp) + density.dp(kTimeToTrackGapDp)
            + density.dp(kMinTrackDp) + gap;
        qreal x = layout.play.right() + gap;
        for (Video::Control control : {Video::Control::Replay, Video::Control::SkipBack,
                                       Video::Control::SkipForward}) {
            if (x + side + reserve > trailingLeft - gap)
                break;
            layout.controls.emplace_back(control, QRectF(x, top, side, side));
            x += side + gap;
            leadingRight = x - gap;
        }
    }

    if (haveVolume)
        layout.controls.emplace_back(Video::Control::Volume, layout.volume);
    if (haveSettings)
        layout.controls.emplace_back(Video::Control::Settings, layout.settings);
    if (haveFullscreen)
        layout.controls.emplace_back(Video::Control::Fullscreen, layout.fullscreen);

    const qreal regionLeft = leadingRight + gap;
    const qreal regionRight = trailingLeft - density.dp(kTimeToTrackGapDp);
    const qreal available = std::max(qreal(0.0), regionRight - regionLeft);

    // The label is measured, not assumed: the widest string it can hold is two
    // duration halves, so a real font gets the space it needs and a narrow bar
    // hands the rest to the track instead of reserving a fixed 104 dp.
    const qreal timeGap = density.dp(kTimeToTrackGapDp);
    const qreal minTime = density.dp(kTimeMinWidthDp);
    const qreal wanted = std::clamp(timeNaturalWidth, minTime, density.dp(kTimeWidthDp));
    const qreal width = std::min(wanted, available - timeGap - density.dp(kMinTrackDp));
    if (width >= minTime) {
        layout.time = QRectF(regionLeft, bar.top(), width, bar.height());
        layout.hasTime = true;
    }

    const qreal trackLeft = layout.hasTime ? layout.time.right() + timeGap : regionLeft;
    const qreal trackHeight =
        std::min(density.dp(kTrackHeightDp), std::max(qreal(0.0), bar.height() / 3.0));
    layout.track = QRectF(trackLeft, bar.center().y() - trackHeight / 2.0,
                          std::max(qreal(0.0), regionRight - trackLeft), trackHeight);

    return layout;
}

// Which glyph an action paints right now.
Glyph glyphFor(const Video& video, Video::Control control)
{
    switch (control) {
    case Video::Control::PlayPause:
        return video.state() == Video::PlaybackState::Playing ? Glyph::Pause : Glyph::Play;
    case Video::Control::Replay:
        return Glyph::Replay;
    case Video::Control::SkipBack:
        return Glyph::SkipBack;
    case Video::Control::SkipForward:
        return Glyph::SkipForward;
    case Video::Control::Volume:
        return video.isMuted() || video.volume() <= 0.0 ? Glyph::VolumeMuted : Glyph::VolumeOn;
    case Video::Control::Settings:
        return Glyph::Settings;
    case Video::Control::Fullscreen:
        return video.isFullscreen() ? Glyph::FullscreenExit : Glyph::FullscreenEnter;
    case Video::Control::None:
        break;
    }
    return Glyph::Play;
}

} // namespace

Video::Video(QWidget* parent)
    : View(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setBackgroundColour(QColor(Qt::transparent));
    setAccessibleName(tr("Video"));
    setAccessibleDescription(
        tr("Video player: play or pause, replay, seek, mute, settings and full screen"));

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

void Video::setFullscreen(bool fullscreen)
{
    if (fullscreen_ == fullscreen)
        return;

    fullscreen_ = fullscreen;
    requestUpdate();
}

bool Video::isFullscreen() const noexcept
{
    return fullscreen_;
}

bool Video::controlsVisible() const noexcept
{
    return controlsOpacity_ > 0.01;
}

bool Video::isScrubbing() const noexcept
{
    return scrubbing_;
}

Video::Control Video::hoveredControl() const noexcept
{
    return hoveredControl_;
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
    return controlRect(Control::PlayPause);
}

QRectF Video::volumeButtonRect() const
{
    return controlRect(Control::Volume);
}

QRectF Video::fullscreenButtonRect() const
{
    return controlRect(Control::Fullscreen);
}

// The natural width of the time label, measured with the same tabular font the
// paint uses. The layout never guesses from a character count.
qreal Video::timeLabelWidth() const
{
    const Theme& theme = Theme::forWidget(this);
    const QFont font = tabularFont(theme.type().labelSmall, theme.density(), this);
    const QFontMetricsF metrics(font);
    const QString sample =
        formatTime(duration_) + QStringLiteral(" / ") + formatTime(duration_);
    return metrics.horizontalAdvance(sample);
}

QRectF Video::timeLabelRect() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    const BarLayout layout = layoutBar(bar, Theme::forWidget(this).density(), timeLabelWidth());
    return layout.hasTime ? layout.time : QRectF();
}

QRectF Video::progressRect() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    return layoutBar(bar, Theme::forWidget(this).density(), timeLabelWidth()).track;
}

int Video::controlCount() const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return 0;

    const BarLayout layout = layoutBar(bar, Theme::forWidget(this).density(), timeLabelWidth());
    return static_cast<int>(layout.controls.size());
}

QRectF Video::controlRect(int index) const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    const BarLayout layout = layoutBar(bar, Theme::forWidget(this).density(), timeLabelWidth());
    if (index < 0 || index >= static_cast<int>(layout.controls.size()))
        return QRectF();

    return layout.controls[static_cast<std::size_t>(index)].second;
}

QRectF Video::controlRect(Control control) const
{
    if (control == Control::None)
        return QRectF();

    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return QRectF();

    const BarLayout layout = layoutBar(bar, Theme::forWidget(this).density(), timeLabelWidth());
    for (const auto& entry : layout.controls) {
        if (entry.first == control)
            return entry.second;
    }

    return QRectF();
}

Video::Control Video::controlAt(const QPointF& point) const
{
    const QRectF bar = controlBarRect();
    if (bar.isEmpty())
        return Control::None;

    const BarLayout layout = layoutBar(bar, Theme::forWidget(this).density(), timeLabelWidth());
    for (const auto& entry : layout.controls) {
        if (entry.second.contains(point))
            return entry.first;
    }

    return Control::None;
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
    paintOverflow(painter);
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

    const QPointF centre = content.center();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 36));
    painter.drawEllipse(centre, radius, radius);

    // The placeholder is the same play glyph as the transport, only larger: the
    // surface and the bar cannot disagree about what "play" looks like.
    const qreal glyphDp = std::min(kPlaceholderIconDp, density.pxToDp(radius * 1.6));
    paintGlyph(painter, Glyph::Play, content, kScrimInkActive, density, glyphDp);
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
    const BarLayout layout = layoutBar(bar, density, timeLabelWidth());

    const QColor restInk = onScrimInk(theme.colours().onSurfaceVariant, kScrimInk);
    const QColor activeInk = onScrimInk(theme.colours().onSurface, kScrimInkActive);
    const bool enabled = isEnabled();

    painter.save();
    painter.setOpacity(controlsOpacity_);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Every control paints its own state layer and glyph from the same
    // rectangle the hit test uses, so the painted button and the clickable
    // button can never drift apart.
    for (const auto& entry : layout.controls) {
        const Control control = entry.first;
        const QRectF& rect = entry.second;
        const bool hovered = enabled && control == hoveredControl_;
        const bool pressed = enabled && control == pressedControl_;

        // Play/pause is the primary transport and keeps the onSurface emphasis
        // at rest; the secondary actions rest at onSurfaceVariant.
        const QColor emphasis = (control == Control::PlayPause || hovered || pressed)
            ? activeInk
            : restInk;
        const QColor ink = enabled ? emphasis : withAlpha(emphasis, kDisabledAlpha);

        if (hovered || pressed) {
            const qreal layer = std::min(density.dp(kStateLayerDp), rect.width());
            const QRectF stateRect(rect.center().x() - layer / 2.0,
                                   rect.center().y() - layer / 2.0, layer, layer);
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(activeInk,
                                       pressed ? kPressedLayerAlpha : kHoverLayerAlpha));
            painter.drawEllipse(stateRect);
        }

        paintGlyph(painter, glyphFor(*this, control), rect, ink, density);
    }

    // Time label: elapsed and total, tabular figures, before the track.
    if (layout.hasTime) {
        const QFont font = tabularFont(theme.type().labelSmall, density, this);
        const QFontMetricsF metrics(font);
        const QString text =
            formatTime(position_) + QStringLiteral(" / ") + formatTime(duration_);
        const QString elided = metrics.elidedText(text, Qt::ElideRight, layout.time.width());
        painter.setFont(font);
        painter.setPen(kControlTextColour);
        painter.drawText(layout.time, static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         elided);
    }

    // The track: unplayed, buffered, played, thumb.
    if (!layout.track.isEmpty() && layout.track.width() > 0.0) {
        const qreal fraction = progressFraction();
        const qreal bufferedFraction = duration_ > 0
            ? std::clamp(static_cast<qreal>(buffered_) / static_cast<qreal>(duration_),
                         qreal(0.0), qreal(1.0))
            : 0.0;
        const qreal radius = layout.track.height() / 2.0;

        painter.setPen(Qt::NoPen);
        painter.setBrush(kTrackColour);
        painter.drawRoundedRect(layout.track, radius, radius);

        if (bufferedFraction > fraction) {
            painter.setBrush(kBufferedColour);
            painter.drawRoundedRect(
                QRectF(layout.track.left(), layout.track.top(),
                       layout.track.width() * bufferedFraction, layout.track.height()),
                radius, radius);
        }

        const qreal played = layout.track.width() * fraction;
        if (played > 0.0) {
            painter.setBrush(theme.colours().primary);
            painter.drawRoundedRect(
                QRectF(layout.track.left(), layout.track.top(), played, layout.track.height()),
                radius, radius);
        }

        const qreal thumbRadius = std::min(
            density.dp(kThumbRadiusDp), std::max(qreal(0.0), bar.height() / 4.0));
        painter.setBrush(kThumbColour);
        painter.drawEllipse(QPointF(layout.track.left() + played, layout.track.center().y()),
                            thumbRadius, thumbRadius);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// The settings panel
// ---------------------------------------------------------------------------

int Video::overflowItemCount() const
{
    return kPanelRowCount;
}

QRectF Video::overflowPanelRect() const
{
    const QRectF bar = controlBarRect();
    const QRectF settings = controlRect(Control::Settings);
    const QRectF content = contentRect();
    if (bar.isEmpty() || settings.isEmpty() || content.isEmpty())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal inset = density.dp(kBarInsetDp);
    const qreal width = std::min(density.dp(kPanelWidthDp),
                                 std::max(qreal(0.0), content.width() - 2.0 * inset));
    const qreal height = 2.0 * density.dp(kPanelPaddingDp)
        + 3.0 * density.dp(kPanelRowHeightDp) + density.dp(kPanelSeparatorHeightDp)
        + density.dp(kPanelRowHeightDp);
    if (width <= 0.0 || height <= 0.0)
        return QRectF();

    const qreal right = std::min(settings.right(), content.right() - inset);
    const qreal left = std::max(content.left() + inset, right - width);
    const qreal bottom = bar.top() - density.dp(kPanelOffsetDp);
    const qreal top = std::max(content.top() + inset, bottom - height);

    return QRectF(left, top, right - left, bottom - top);
}

QRectF Video::overflowItemRectAt(int index) const
{
    const QRectF panel = overflowPanelRect();
    if (panel.isEmpty() || index < 0 || index >= overflowItemCount())
        return QRectF();

    const Density& density = Theme::forWidget(this).density();
    const qreal pad = density.dp(kPanelPaddingDp);
    const qreal rowHeight = density.dp(kPanelRowHeightDp);

    qreal y = panel.top() + pad;
    for (int i = 0; i < index; ++i) {
        y += rowHeight;
        if (i == 2)
            y += density.dp(kPanelSeparatorHeightDp);
    }

    return QRectF(panel.left() + pad, y, panel.width() - 2.0 * pad, rowHeight);
}

QRectF Video::overflowItemRect(int index) const
{
    if (!overflowOpen_)
        return QRectF();
    return overflowItemRectAt(index);
}

int Video::overflowItemAt(const QPointF& point) const
{
    if (!overflowOpen_)
        return -1;

    for (int i = 0; i < overflowItemCount(); ++i) {
        if (overflowItemRectAt(i).contains(point))
            return i;
    }
    return -1;
}

QString Video::overflowItemText(int index) const
{
    switch (index) {
    case 0:
        return tr("Replay");
    case 1:
        return tr("Back 5 seconds");
    case 2:
        return tr("Forward 5 seconds");
    case 3:
        return tr("Loop");
    default:
        return QString();
    }
}

bool Video::overflowOpen() const noexcept
{
    return overflowOpen_;
}

void Video::toggleOverflow()
{
    if (!overflowOpen_ && overflowPanelRect().isEmpty())
        return;

    overflowOpen_ = !overflowOpen_;
    // Opening with the pointer leaves no row highlighted: the highlight is the
    // keyboard cursor and appears on the first arrow key, not on a click.
    overflowHoveredItem_ = -1;
    requestUpdate();
}

void Video::closeOverflow()
{
    if (!overflowOpen_)
        return;

    overflowOpen_ = false;
    overflowHoveredItem_ = -1;
    requestUpdate();
}

void Video::activateOverflowItem(int index)
{
    switch (index) {
    case 0:
        triggerControl(Control::Replay);
        break;
    case 1:
        triggerControl(Control::SkipBack);
        break;
    case 2:
        triggerControl(Control::SkipForward);
        break;
    case 3:
        setLoop(!loop_);
        break;
    default:
        return;
    }

    closeOverflow();
}

void Video::paintOverflow(QPainter& painter)
{
    if (!overflowOpen_)
        return;

    const QRectF panel = overflowPanelRect();
    if (panel.isEmpty())
        return;

    const Theme& theme = Theme::forWidget(this);
    const Density& density = theme.density();
    const ColorScheme& colours = theme.colours();
    const qreal radius = density.dp(kPanelRadiusDp);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
    painter.setBrush(colours.surfaceContainer);
    painter.drawRoundedRect(panel, radius, radius);

    const QFont font = Font::resolve(this, theme.type().labelMedium, density);
    painter.setFont(font);
    const qreal textInset = density.dp(12.0);

    for (int i = 0; i < overflowItemCount(); ++i) {
        const QRectF row = overflowItemRectAt(i);
        if (row.isEmpty())
            continue;

        if (i == overflowHoveredItem_) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(withAlpha(colours.onSurface, kHoverLayerAlpha));
            painter.drawRoundedRect(row.adjusted(density.dp(2.0), density.dp(1.0),
                                                 -density.dp(2.0), -density.dp(1.0)),
                                    density.dp(8.0), density.dp(8.0));
        }

        painter.setPen(colours.onSurface);
        painter.drawText(row.adjusted(textInset, 0.0, -density.dp(40.0), 0.0),
                         static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                         overflowItemText(i));

        if (i == overflowItemCount() - 1 && loop_) {
            const qreal check = density.dp(kIconDp);
            const QRectF checkRect(row.right() - textInset - check,
                                   row.center().y() - check / 2.0, check, check);
            paintGlyph(painter, Glyph::Check, checkRect, colours.onSurface, density);
        }
    }

    // The separator above the settings row.
    const QRectF beforeLast = overflowItemRectAt(2);
    if (!beforeLast.isEmpty()) {
        const qreal y = beforeLast.bottom() + density.dp(kPanelSeparatorHeightDp) / 2.0;
        painter.setPen(QPen(colours.outlineVariant, density.dp(1.0)));
        painter.drawLine(QPointF(panel.left() + density.dp(kPanelPaddingDp), y),
                         QPointF(panel.right() - density.dp(kPanelPaddingDp), y));
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Control dispatch and naming
// ---------------------------------------------------------------------------

void Video::triggerControl(Control control)
{
    switch (control) {
    case Control::PlayPause:
        togglePlayback();
        break;
    case Control::Replay:
        setPosition(0);
        play();
        break;
    case Control::SkipBack:
        setPosition(position_ - kSeekStepMs);
        break;
    case Control::SkipForward:
        setPosition(position_ + kSeekStepMs);
        break;
    case Control::Volume:
        setMuted(!muted_);
        break;
    case Control::Settings:
        toggleOverflow();
        break;
    case Control::Fullscreen:
        emit fullscreenRequested();
        break;
    case Control::None:
        break;
    }
}

QString Video::controlToolTip(Control control) const
{
    switch (control) {
    case Control::PlayPause:
        return state_ == PlaybackState::Playing ? tr("Pause (Space)") : tr("Play (Space)");
    case Control::Replay:
        return tr("Replay from the start (Home)");
    case Control::SkipBack:
        return tr("Back 5 seconds (Left)");
    case Control::SkipForward:
        return tr("Forward 5 seconds (Right)");
    case Control::Volume:
        return muted_ ? tr("Unmute (M)") : tr("Mute (M)");
    case Control::Settings:
        return tr("Playback settings");
    case Control::Fullscreen:
        return fullscreen_ ? tr("Exit full screen (F)") : tr("Full screen (F)");
    case Control::None:
        break;
    }
    return QString();
}

QString Video::controlAccessibleName(Control control) const
{
    switch (control) {
    case Control::PlayPause:
        return state_ == PlaybackState::Playing ? tr("Pause") : tr("Play");
    case Control::Replay:
        return tr("Replay from the start");
    case Control::SkipBack:
        return tr("Back 5 seconds");
    case Control::SkipForward:
        return tr("Forward 5 seconds");
    case Control::Volume:
        return muted_ ? tr("Unmute") : tr("Mute");
    case Control::Settings:
        return tr("Playback settings");
    case Control::Fullscreen:
        return fullscreen_ ? tr("Exit full screen") : tr("Full screen");
    case Control::None:
        break;
    }
    return QString();
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

void Video::updateHoveredControl(const QPointF& point)
{
    const Control next = isEnabled() ? controlAt(point) : Control::None;
    if (next == hoveredControl_)
        return;

    hoveredControl_ = next;
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
    hoveredControl_ = Control::None;
    overflowHoveredItem_ = -1;
    updateControlsTarget();
    View::leaveEvent(event);
    requestUpdate();
}

bool Video::event(QEvent* event)
{
    // The bar owns several buttons but is one widget, so the per-action tooltip
    // is resolved here rather than by QWidget's single toolTip() property.
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const Control control = controlAt(QPointF(help->pos()));
        if (control != Control::None) {
            QToolTip::showText(help->globalPos(), controlToolTip(control), this);
            return true;
        }

        QToolTip::hideText();
        return true;
    }

    return View::event(event);
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

    if (event->button() != Qt::LeftButton || !isEnabled())
        return;

    setFocus(Qt::MouseFocusReason);
    hovered_ = true;
    updateControlsTarget();

    const QPointF position = event->position();

    // The settings panel is modal for one click: a press inside it activates a
    // row, and the press that dismisses it is not also an action on the bar.
    if (overflowOpen_) {
        const int item = overflowItemAt(position);
        if (item >= 0) {
            activateOverflowItem(item);
            event->accept();
            return;
        }

        closeOverflow();
        event->accept();
        return;
    }

    const Control control = controlAt(position);
    if (control != Control::None) {
        pressedControl_ = control;
        triggerControl(control);
        requestUpdate();
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

    const QPointF position = event->position();
    updateHoveredControl(position);

    if (overflowOpen_) {
        const int item = overflowItemAt(position);
        if (item != overflowHoveredItem_) {
            overflowHoveredItem_ = item;
            requestUpdate();
        }
    }

    if (!scrubbing_)
        return;

    hovered_ = true;
    setPosition(positionForX(position.x()));
    event->accept();
}

void Video::mouseReleaseEvent(QMouseEvent* event)
{
    View::mouseReleaseEvent(event);

    if (event->button() != Qt::LeftButton)
        return;

    if (pressedControl_ != Control::None) {
        pressedControl_ = Control::None;
        requestUpdate();
    }

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
    if (overflowOpen_) {
        switch (event->key()) {
        case Qt::Key_Escape:
            closeOverflow();
            event->accept();
            return;
        case Qt::Key_Down:
        case Qt::Key_Up: {
            const int step = event->key() == Qt::Key_Down ? 1 : -1;
            const int count = overflowItemCount();
            if (count > 0) {
                if (overflowHoveredItem_ < 0)
                    overflowHoveredItem_ = step > 0 ? 0 : count - 1;
                else
                    overflowHoveredItem_ = (overflowHoveredItem_ + step + count) % count;
                requestUpdate();
            }
            event->accept();
            return;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (overflowHoveredItem_ >= 0)
                activateOverflowItem(overflowHoveredItem_);
            event->accept();
            return;
        default:
            break;
        }
    }

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
