// Periodicity demo 鈥?an Element-Plus-style documentation window for the library.
//
// The demo is deliberately built from plain Qt widgets: the component layer of
// Periodicity does not exist yet, so the shell must not pretend otherwise. What
// it does show is real: every colour, type style, shape, elevation and motion
// sample is drawn from the library's own token and theme modules, the motion
// page is driven by the real Animator, and the event page dispatches real
// PointerEvents through the real EventDispatcher.

#include <md/animation/animator.h>
#include <md/core/density.h>
#include <md/event/event_dispatcher.h>
#include <md/event/event_target.h>
#include <md/event/pointer_event.h>
#include <md/layer/background.h>
#include <md/layer/elevation.h>
#include <md/layer/foreground.h>
#include <md/layer/ripple_layer.h>
#include <md/layer/surface_context.h>
#include <md/ripple/ripple.h>
#include <md/theme/theme.h>
#include <md/tokens/color_scheme.h>
#include <md/tokens/elevation.h>
#include <md/tokens/motion.h>
#include <md/tokens/shape.h>
#include <md/tokens/type_scale.h>

#include <QApplication>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QWidget>

#include <array>
#include <functional>
#include <utility>
#include <vector>

namespace {

using md::ColorScheme;
using md::Density;
using md::Elevation;
using md::Shape;
using md::TextStyle;
using md::Theme;
using md::TypeScale;

constexpr int kSidebarWidth = 232;

QString hexOf(const QColor& colour)
{
    return colour.name(QColor::HexRgb).toUpper();
}

QFont fontFor(const TextStyle& style, const Density& density)
{
    QFont font(QStringLiteral("Roboto"));
    font.setFamilies({QStringLiteral("Roboto"), QStringLiteral("Segoe UI"),
                      QStringLiteral("Microsoft YaHei UI")});
    font.setPixelSize(qMax(9, qRound(density.sp(style.sizeSp))));
    font.setWeight(static_cast<QFont::Weight>(style.weight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, density.sp(style.trackingSp));
    return font;
}

// A page is a scrollable column with a title, a lead paragraph and cards.
class Page : public QScrollArea
{
public:
    explicit Page(const Theme& theme, QWidget* parent = nullptr)
        : QScrollArea(parent), m_theme(theme)
    {
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        auto* body = new QWidget;
        auto* column = new QVBoxLayout(body);
        column->setContentsMargins(40, 32, 40, 48);
        column->setSpacing(20);
        setWidget(body);

        m_column = column;
        body->setStyleSheet(QStringLiteral("background:%1;").arg(theme.colours().surface.name()));
    }

    void addTitle(const QString& text)
    {
        auto* label = new QLabel(text);
        label->setFont(fontFor(m_theme.type().headlineMedium, m_theme.density()));
        label->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme.colours().onSurface.name()));
        m_column->addWidget(label);
    }

    void addLead(const QString& text)
    {
        auto* label = new QLabel(text);
        label->setWordWrap(true);
        label->setFont(fontFor(m_theme.type().bodyLarge, m_theme.density()));
        label->setStyleSheet(
            QStringLiteral("color:%1;").arg(m_theme.colours().onSurfaceVariant.name()));
        m_column->addWidget(label);
    }

    void addCard(const QString& title, const QString& description, QWidget* content)
    {
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        card->setStyleSheet(
            QStringLiteral("#card{background:%1;border:1px solid %2;border-radius:%3px;}")
                .arg(m_theme.colours().surfaceContainerLow.name(),
                     m_theme.colours().outlineVariant.name())
                .arg(qRound(m_theme.shapes().medium.radii().topStart)));

        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(24, 20, 24, 24);
        layout->setSpacing(12);

        auto* heading = new QLabel(title);
        heading->setFont(fontFor(m_theme.type().titleMedium, m_theme.density()));
        heading->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme.colours().onSurface.name()));
        layout->addWidget(heading);

        if (!description.isEmpty()) {
            auto* caption = new QLabel(description);
            caption->setWordWrap(true);
            caption->setFont(fontFor(m_theme.type().bodyMedium, m_theme.density()));
            caption->setStyleSheet(
                QStringLiteral("color:%1;").arg(m_theme.colours().onSurfaceVariant.name()));
            layout->addWidget(caption);
        }

        layout->addWidget(content);
        m_column->addWidget(card);
    }

    void addStretch() { m_column->addStretch(1); }

    [[nodiscard]] const Theme& theme() const noexcept { return m_theme; }

private:
    QVBoxLayout* m_column = nullptr;
    Theme m_theme;
};

// A rounded surface that forwards pointer events to a RippleController and
// paints the ripples on top of its own background.
class RippleSurface : public QWidget
{
public:
    explicit RippleSurface(const Theme& theme, QWidget* parent = nullptr)
        : QWidget(parent), m_theme(theme)
    {
        setMinimumSize(460, 220);
        setMouseTracking(true);
        m_ripple.setTheme(theme);
        m_ripple.setCornerRadius(20.0);

        // --slow-ripple stretches the motion so a still frame catches the disc
        // mid-expansion; the live demo uses the token durations.
        if (QApplication::arguments().contains(QStringLiteral("--slow-ripple"))) {
            md::ripple::RippleSpec spec = md::ripple::RippleSpec::fromTheme(theme);
            spec.expansion.duration = std::chrono::milliseconds(1600);
            spec.fade.duration = std::chrono::milliseconds(1600);
            m_ripple.setSpec(spec);
        }
        m_ripple.setBounds(rect());
        QObject::connect(&m_ripple, &md::ripple::RippleController::repaintRequested, this,
                         qOverload<>(&QWidget::update));
    }

protected:
    void resizeEvent(QResizeEvent*) override { m_ripple.setBounds(rect()); }

public:
    // Used by --demo-ripple so a capture can show the effect without a pointer.
    void demonstrate()
    {
        const QPointF centre(rect().center());
        m_ripple.pointerEntered(centre);
        m_ripple.pointerPressed(centre + QPointF(50.0, 10.0));
    }

protected:

    void enterEvent(QEnterEvent* event) override { m_ripple.pointerEntered(event->position()); }

    void leaveEvent(QEvent*) override { m_ripple.pointerLeft(); }

    void mouseMoveEvent(QMouseEvent* event) override { m_ripple.pointerMoved(event->position()); }

    void mousePressEvent(QMouseEvent* event) override { m_ripple.pointerPressed(event->position()); }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_ripple.pointerReleased(event->position());
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()), 20.0, 20.0);

        painter.fillPath(shape, m_theme.colours().surfaceContainerHigh);
        painter.setPen(QPen(m_theme.colours().outlineVariant, 1.0));
        painter.drawPath(shape);
        painter.setClipPath(shape);

        m_ripple.paint(painter);
    }

private:
    Theme m_theme;
    md::ripple::RippleController m_ripple;
};

// A surface painted by the layer stack: background, real shadow, content, then
// the state overlay and focus ring. Hovering, pressing or tabbing changes the
// state bits the layers resolve against, so the page shows the pipeline rather
// than a mock-up.
class SurfaceTile : public QWidget
{
public:
    SurfaceTile(Theme theme, md::Elevation elevation, QString caption, QWidget* parent = nullptr)
        : QWidget(parent), m_theme(std::move(theme)), m_elevation(elevation),
          m_caption(std::move(caption)), m_rippleLayer(m_theme)
    {
        setMinimumSize(250, 170);
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);

        m_shape = m_theme.shapes().large;
        m_elevationLayer.setElevation(m_elevation);
        m_elevationLayer.setShape(m_shape);

        m_rippleLayer.setCornerRadius(20.0);
        QObject::connect(&m_rippleLayer.controller(),
                         &md::ripple::RippleController::repaintRequested, this,
                         qOverload<>(&QWidget::update));
        refreshRippleBounds();
    }

protected:
    void resizeEvent(QResizeEvent*) override { refreshRippleBounds(); }

    void enterEvent(QEnterEvent* event) override
    {
        m_state |= md::ViewState::Hovered;
        m_rippleLayer.controller().pointerEntered(event->position());
        update();
    }

    void leaveEvent(QEvent*) override
    {
        m_state.setFlag(md::ViewState::Hovered, false);
        m_state.setFlag(md::ViewState::Pressed, false);
        m_rippleLayer.controller().pointerLeft();
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        m_state |= md::ViewState::Pressed;
        m_rippleLayer.controller().pointerPressed(event->position());
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_state.setFlag(md::ViewState::Pressed, false);
        m_rippleLayer.controller().pointerReleased(event->position());
        update();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        m_rippleLayer.controller().pointerMoved(event->position());
    }

    void focusInEvent(QFocusEvent*) override
    {
        m_state |= md::ViewState::Focused;
        update();
    }

    void focusOutEvent(QFocusEvent*) override
    {
        m_state.setFlag(md::ViewState::Focused, false);
        update();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QMarginsF margins = m_elevationLayer.shadowMargins();
        const QRectF content = QRectF(rect()).adjusted(margins.left(), margins.top(),
                                                      -margins.right(), -margins.bottom());
        if (content.width() <= 0.0 || content.height() <= 0.0)
            return;

        md::layer::SurfaceContext context{m_theme, m_state, m_shape, m_elevation, QColor(), QColor()};

        // Shadow first, then the surface: the shadow is drawn behind the
        // background, so its solid core is covered and only the halo remains.
        m_elevationLayer.paint(painter, content, context);
        m_background.paint(painter, content, context);

        painter.setPen(m_theme.colours().onSurface);
        painter.setFont(fontFor(m_theme.type().titleMedium, m_theme.density()));
        painter.drawText(content, Qt::AlignCenter, m_caption);

        m_overlay.paint(painter, content, context);
        m_rippleLayer.paint(painter, content, context);
        m_focus.paint(painter, content, context);
    }

private:
    void refreshRippleBounds()
    {
        const QMarginsF margins = m_elevationLayer.shadowMargins();
        m_rippleLayer.setBounds(QRectF(rect()).adjusted(margins.left(), margins.top(),
                                                       -margins.right(), -margins.bottom()));
    }

    Theme m_theme;
    md::Elevation m_elevation;
    md::Shape m_shape;
    QString m_caption;
    md::ViewStates m_state{md::ViewState::Enabled};

    md::layer::BackgroundLayer m_background;
    md::layer::ElevationLayer m_elevationLayer;
    md::layer::StateOverlayLayer m_overlay;
    md::layer::RippleLayer m_rippleLayer;
    md::layer::FocusRingLayer m_focus;
};

QWidget* colourGrid(const Theme& theme, const std::vector<std::pair<QString, QColor>>& roles)
{
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);

    int index = 0;
    for (const auto& [name, colour] : roles) {
        auto* swatch = new QFrame;
        swatch->setFixedHeight(72);
        swatch->setStyleSheet(QStringLiteral("background:%1;border-radius:12px;border:1px solid %2;")
                                  .arg(colour.name(), theme.colours().outlineVariant.name()));

        auto* caption = new QLabel(QStringLiteral("%1\n%2").arg(name, hexOf(colour)));
        caption->setFont(fontFor(theme.type().labelSmall, theme.density()));
        caption->setStyleSheet(
            QStringLiteral("color:%1;").arg(theme.colours().onSurfaceVariant.name()));

        const int row = (index / 4) * 2;
        const int column = index % 4;
        grid->addWidget(swatch, row, column);
        grid->addWidget(caption, row + 1, column);
        ++index;
    }
    return host;
}

QWidget* typographyColumn(const Theme& theme)
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    const TypeScale& type = theme.type();
    const std::array<std::pair<const char*, TextStyle>, 15> styles{{
        {"displayLarge", type.displayLarge},     {"displayMedium", type.displayMedium},
        {"displaySmall", type.displaySmall},     {"headlineLarge", type.headlineLarge},
        {"headlineMedium", type.headlineMedium}, {"headlineSmall", type.headlineSmall},
        {"titleLarge", type.titleLarge},         {"titleMedium", type.titleMedium},
        {"titleSmall", type.titleSmall},         {"bodyLarge", type.bodyLarge},
        {"bodyMedium", type.bodyMedium},         {"bodySmall", type.bodySmall},
        {"labelLarge", type.labelLarge},         {"labelMedium", type.labelMedium},
        {"labelSmall", type.labelSmall},
    }};

    for (const auto& [name, style] : styles) {
        auto* sample = new QLabel(QStringLiteral("Material Design 3"));
        sample->setFont(fontFor(style, theme.density()));
        sample->setStyleSheet(QStringLiteral("color:%1;").arg(theme.colours().onSurface.name()));

        auto* meta = new QLabel(QStringLiteral("%1  路  %2/%3 sp  路  tracking %4  路  weight %5")
                                    .arg(QString::fromLatin1(name))
                                    .arg(style.sizeSp)
                                    .arg(style.lineHeightSp)
                                    .arg(style.trackingSp)
                                    .arg(style.weight));
        meta->setFont(fontFor(type.labelSmall, theme.density()));
        meta->setStyleSheet(
            QStringLiteral("color:%1;").arg(theme.colours().onSurfaceVariant.name()));

        layout->addWidget(sample);
        layout->addWidget(meta);
    }
    return host;
}

// Draws one shape token so the corner-radius scale can be compared directly.
class ShapeTile : public QWidget
{
public:
    ShapeTile(Shape shape, QColor fill, QColor outline, QString caption, QWidget* parent = nullptr)
        : QWidget(parent), m_shape(std::move(shape)), m_fill(std::move(fill)),
          m_outline(std::move(outline)), m_caption(std::move(caption))
    {
        setMinimumSize(150, 118);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF box(6, 6, width() - 12.0, height() - 34.0);
        const QPainterPath path = m_shape.path(box);
        painter.fillPath(path, m_fill);
        painter.setPen(QPen(m_outline, 1.0));
        painter.drawPath(path);

        painter.setPen(m_outline);
        painter.drawText(QRectF(0, height() - 26.0, width(), 20.0), Qt::AlignCenter, m_caption);
    }

private:
    Shape m_shape;
    QColor m_fill;
    QColor m_outline;
    QString m_caption;
};

QWidget* shapeRow(const Theme& theme)
{
    auto* host = new QWidget;
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    const std::array<std::pair<const char*, Shape>, 5> shapes{{
        {"extraSmall", theme.shapes().extraSmall},
        {"small", theme.shapes().small},
        {"medium", theme.shapes().medium},
        {"large", theme.shapes().large},
        {"extraLarge", theme.shapes().extraLarge},
    }};

    for (const auto& [name, shape] : shapes) {
        layout->addWidget(new ShapeTile(shape, theme.colours().primaryContainer,
                                        theme.colours().onPrimaryContainer,
                                        QString::fromLatin1(name)));
    }
    layout->addStretch(1);
    return host;
}

QWidget* elevationRow(const Theme& theme)
{
    auto* host = new QWidget;
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(8, 8, 8, 16);
    layout->setSpacing(14);

    const std::array<std::pair<const char*, Elevation>, 6> levels{{
        {"0 dp", Elevation::Level0}, {"1 dp", Elevation::Level1}, {"3 dp", Elevation::Level2},
        {"6 dp", Elevation::Level3}, {"8 dp", Elevation::Level4}, {"12 dp", Elevation::Level5},
    }};

    for (const auto& [name, level] : levels) {
        auto* card = new QFrame;
        card->setFixedSize(112, 84);
        card->setStyleSheet(
            QStringLiteral("background:%1;border-radius:14px;")
                .arg(theme.colours().surfaceAtElevation(level).name()));

        auto* caption = new QLabel(name, card);
        caption->setAlignment(Qt::AlignCenter);
        caption->setGeometry(0, 0, 112, 84);
        caption->setFont(fontFor(theme.type().labelMedium, theme.density()));
        caption->setStyleSheet(QStringLiteral("color:%1;").arg(theme.colours().onSurface.name()));

        if (level != Elevation::Level0) {
            auto* shadow = new QGraphicsDropShadowEffect(card);
            shadow->setBlurRadius(6.0 + md::elevationDp(level) * 2.0);
            shadow->setOffset(0.0, md::elevationDp(level) / 2.0);
            shadow->setColor(QColor(0, 0, 0, 60));
            card->setGraphicsEffect(shadow);
        }

        layout->addWidget(card);
    }
    layout->addStretch(1);
    return host;
}

// Motion sample: a dot travelling across a track, driven by the real Animator.
QWidget* motionRow(const Theme& theme)
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto* track = new QFrame;
    track->setFixedHeight(72);
    track->setStyleSheet(QStringLiteral("background:%1;border-radius:36px;")
                             .arg(theme.colours().surfaceContainerHigh.name()));

    auto* dot = new QLabel(track);
    dot->setFixedSize(48, 48);
    dot->setStyleSheet(QStringLiteral("background:%1;border-radius:24px;")
                           .arg(theme.colours().primary.name()));
    dot->move(12, 12);

    auto* animator = new md::animation::Animator(track);

    const std::array<std::pair<md::MotionLevel, const char*>, 6> levels{{
        {md::MotionLevel::Level0, "0 ms"},       {md::MotionLevel::Level1, "250 ms"},
        {md::MotionLevel::Level2, "500 ms"},     {md::MotionLevel::Level3, "750 ms"},
        {md::MotionLevel::Level4, "1000 ms"},    {md::MotionLevel::Level5, "1250 ms"},
    }};

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);

    for (const auto& [level, name] : levels) {
        auto* play = new QPushButton(QString::fromLatin1(name));
        play->setCursor(Qt::PointingHandCursor);

        QObject::connect(play, &QPushButton::clicked, track,
                         [track, dot, animator, theme, level]() {
                             const qreal travel = track->width() - dot->width() - 24.0;
                             dot->move(12, 12);
                             animator->start(theme.motion().level(level),
                                             [dot, travel](qreal progress) {
                                                 dot->move(qRound(12.0 + travel * progress), 12);
                                             });
                         });

        buttons->addWidget(play);
    }
    buttons->addStretch(1);

    layout->addWidget(track);
    layout->addLayout(buttons);
    return host;
}

} // namespace

