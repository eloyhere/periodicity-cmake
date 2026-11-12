// Periodicity — Video, a self-painted player surface without Qt Multimedia.
//
// Periodicity's baseline is qtbase only, so Video does not decode anything. The
// host owns the pipeline (a QMediaPlayer/QVideoSink pair, ffmpeg, a texture, …)
// and hands the component one of two things:
//
//   * setFrame()      — push the next decoded QImage; or
//   * setFrameProvider() — let the player pull a frame for a position in
//                         milliseconds every time the position changes.
//
// Everything else is local: AspectFit painting on a black letterbox, the
// placeholder shown before the first frame, the transport (play / pause /
// stop), a 16 ms ticker that advances the position, a draggable progress bar,
// time display, volume/mute and a full-screen request. The full-screen button
// only emits fullscreenRequested(); the host decides what full screen means.
//
// The controls follow the baseline media contract: a bottom scrim that fades
// from transparent to 60% black, a large play/pause on the left, the elapsed
// and total time in tabular figures, a thin progress track that draws the
// buffered and the played segment under a round thumb, and volume, settings and
// full-screen on the right. The bar is 56 dp high and hover-revealed: it fades
// in on enter and out on leave unless playback is paused, so it never covers
// the picture when the pointer is elsewhere. The ticker is suspended while the
// thumb is being dragged, because a seek in flight must not fight playback.
//
// The icons are a self-contained path set, not text glyphs and not a font: every
// action is the filled 24 dp path on the standard grid, centred in its own
// 48 dp touch target. The bar carries the transport when the width allows it;
// below that, replay and the two seeks move into the settings panel instead of
// shrinking below the 48 dp target the pointer model guarantees.
//
// The ticker advances the position even without a provider; it pulls a frame
// only when a provider is installed. There is no audio: setVolume()/setMuted()
// are state the host mirrors into its audio output.

#pragma once

#include <md/view/view.h>

#include <QImage>
#include <QMetaType>
#include <QString>
#include <Qt>

#include <functional>

class QEnterEvent;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QPointF;
class QRectF;
class QTimer;

namespace md {

class Video : public View
{
    Q_OBJECT

public:
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(PlaybackState)

    // One action in the control bar. `None` is what controlAt() answers when the
    // pointer is not on a control; it is never painted.
    enum class Control {
        None,
        PlayPause,
        Replay,
        SkipBack,
        SkipForward,
        Volume,
        Settings,
        Fullscreen,
    };
    Q_ENUM(Control)

    explicit Video(QWidget* parent = nullptr);

    // The frame the host decoded last. An invalid image clears the frame and
    // restores the placeholder.
    void setFrame(const QImage& frame);
    [[nodiscard]] QImage frame() const;
    [[nodiscard]] bool hasFrame() const noexcept;
    void clearFrame();

    // The decoder hook. The provider is called with the current position in
    // milliseconds whenever a frame is needed (seek, setFrameProvider, and each
    // playback tick). It runs on the GUI thread. A null return keeps the last
    // frame instead of flickering to the placeholder.
    void setFrameProvider(std::function<QImage(qint64)> provider);
    [[nodiscard]] bool hasFrameProvider() const noexcept;

    // The media length in milliseconds. Zero means "unknown"; the ticker then
    // keeps advancing without an end. A negative value is clamped to zero.
    void setDuration(qint64 ms);
    [[nodiscard]] qint64 duration() const noexcept;

    // Seeking. Clamped to [0, duration] when the duration is known, and emits
    // positionChanged() when the position actually changes. A provider is
    // asked for the frame at the new position immediately.
    void setPosition(qint64 ms);
    [[nodiscard]] qint64 position() const noexcept;

    // setState(Stopped) stops the ticker but keeps the position; stop() also
    // rewinds to zero. Starting playback at the end rewinds first.
    void setState(PlaybackState state);
    [[nodiscard]] PlaybackState state() const noexcept;
    void play();
    void pause();
    void stop();
    void togglePlayback();

    void setVolume(qreal volume); // 0.0 … 1.0
    [[nodiscard]] qreal volume() const noexcept;
    void setMuted(bool muted);
    [[nodiscard]] bool isMuted() const noexcept;
    void setLoop(bool loop);
    [[nodiscard]] bool loop() const noexcept;

    // Whether the surface is currently presented full screen. The component
    // never changes this itself: it only chooses the enter or exit glyph, so
    // the host must mirror its own full-screen state here. The default is the
    // "enter full screen" glyph.
    void setFullscreen(bool fullscreen);
    [[nodiscard]] bool isFullscreen() const noexcept;

    // How far ahead of position() the host has buffered, in milliseconds. The
    // progress track draws that segment behind the played one. Zero means
    // "nothing buffered"; setting a duration without ever calling this fills
    // the track, because a host that pushed a frame usually has the whole clip.
    void setBuffered(qint64 ms);
    [[nodiscard]] qint64 buffered() const noexcept;

    // The hover state of the controls, as a test or a host would ask it. The
    // controls fade in on enter and out on leave while playing; a paused or
    // scrubbing surface keeps them on screen.
    [[nodiscard]] bool controlsVisible() const noexcept;
    [[nodiscard]] bool isScrubbing() const noexcept;

    // The overlay geometry, in widget coordinates. Exposed so a host (and the
    // tests) can target the transport controls without duplicating the layout.
    [[nodiscard]] QRectF controlBarRect() const;
    [[nodiscard]] QRectF playButtonRect() const;
    [[nodiscard]] QRectF progressRect() const;
    [[nodiscard]] QRectF volumeButtonRect() const;
    [[nodiscard]] QRectF fullscreenButtonRect() const;
    [[nodiscard]] QRectF timeLabelRect() const;

    // Every control the current width lays out, in visual order, left to right.
    // controlRect() answers an empty rectangle for a control that the bar had to
    // drop (the secondary transport moves into the settings panel on a narrow
    // surface, it is never shrunk below its 48 dp target). controlAt() runs the
    // same hit test the pointer uses.
    [[nodiscard]] int controlCount() const;
    [[nodiscard]] QRectF controlRect(int index) const;
    [[nodiscard]] QRectF controlRect(Control control) const;
    [[nodiscard]] Control controlAt(const QPointF& point) const;
    [[nodiscard]] Control hoveredControl() const noexcept;

    // The per-action strings a button announces: the tooltip (with its
    // keyboard shortcut) and the accessible name. Both are empty for
    // Control::None.
    [[nodiscard]] QString controlToolTip(Control control) const;
    [[nodiscard]] QString controlAccessibleName(Control control) const;

    // The settings panel that holds the secondary transport on a narrow bar. It
    // is painted by the surface, not by a QMenu, so opening it never steals the
    // focus the player needs for Space and the arrow keys.
    [[nodiscard]] bool overflowOpen() const noexcept;
    [[nodiscard]] int overflowItemCount() const;
    [[nodiscard]] QRectF overflowItemRect(int index) const;
    void closeOverflow();

signals:
    void playbackStateChanged(PlaybackState state);
    void positionChanged(qint64 position);
    void durationChanged(qint64 duration);
    void volumeChanged(qreal volume);
    void mutedChanged(bool muted);
    void fullscreenRequested();
    void frameChanged();

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

    void onShown() override;
    void onHidden() override;

private:
    void startTicker();
    void stopTicker();
    void onTick();
    void applyState(PlaybackState next);
    void setPositionInternal(qint64 ms, bool pullFrameNow);
    void pullFrame();

    [[nodiscard]] qint64 clampPosition(qint64 ms) const;
    [[nodiscard]] qint64 positionForX(qreal x) const;
    [[nodiscard]] qreal progressFraction() const;
    [[nodiscard]] QRectF progressHitRect() const;
    [[nodiscard]] QRectF scrimRect() const;
    // Measured with the label's tabular font; the bar never reserves width from
    // a character count.
    [[nodiscard]] qreal timeLabelWidth() const;

    // The hover fade. updateControlsTarget() decides the destination,
    // stepControlsFade() advances one timer tick, and a reduced-motion theme
    // snaps straight to the destination because the spec's duration is zero.
    void updateControlsTarget();
    void stepControlsFade();
    [[nodiscard]] bool fadeEnabled() const;
    [[nodiscard]] int fadeDurationMs() const;

    // Control dispatch: one switch owns the behaviour of every button, so the
    // mouse, the keyboard and the settings panel cannot drift apart.
    void triggerControl(Control control);
    void updateHoveredControl(const QPointF& point);

    // The settings panel is part of the painted surface (see overflowOpen()).
    [[nodiscard]] QRectF overflowPanelRect() const;
    [[nodiscard]] QRectF overflowItemRectAt(int index) const;
    [[nodiscard]] int overflowItemAt(const QPointF& point) const;
    [[nodiscard]] QString overflowItemText(int index) const;
    void toggleOverflow();
    void activateOverflowItem(int index);
    void paintOverflow(QPainter& painter);

    void paintScrim(QPainter& painter);
    void paintPlaceholder(QPainter& painter, const QRectF& content);
    void paintControls(QPainter& painter, const QRectF& content);

    QImage frame_;
    std::function<QImage(qint64)> provider_;

    PlaybackState state_ = PlaybackState::Stopped;
    qint64 duration_ = 0;
    qint64 position_ = 0;
    qint64 buffered_ = 0;
    qreal volume_ = 1.0;
    bool muted_ = false;
    bool loop_ = false;
    bool fullscreen_ = false;
    bool scrubbing_ = false;
    bool scrubResume_ = false;
    bool hovered_ = false;
    bool overflowOpen_ = false;

    Control hoveredControl_ = Control::None;
    Control pressedControl_ = Control::None;
    int overflowHoveredItem_ = -1;

    qreal controlsOpacity_ = 1.0;
    qreal controlsTarget_ = 1.0;

    QTimer* ticker_ = nullptr;
    QTimer* fadeTimer_ = nullptr;
};

} // namespace md

Q_DECLARE_METATYPE(md::Video::PlaybackState)
