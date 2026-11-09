// Periodicity demo — application shell.
//
// A sidebar of documentation topics, a header with a light/dark switch, and a
// stack of pages. Each page is built from the library's own tokens; the event
// page runs a real dispatch through the real dispatcher.

#include "demo_ui.h"

#include <md/event/event_dispatcher.h>
#include <md/event/event_target.h>
#include <md/event/pointer_event.h>
#include <md/schedule/task_queue.h>
#include <md/theme/theme.h>

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <memory>

namespace {

QString phaseName(md::EventPhase phase)
{
    switch (phase) {
    case md::EventPhase::Capture:
        return QStringLiteral("capture");
    case md::EventPhase::Target:
        return QStringLiteral("target");
    case md::EventPhase::Bubble:
        return QStringLiteral("bubble");
    }
    return QStringLiteral("?");
}

// A target that records each delivery, used by the event-system page.
class LogTarget : public md::EventTarget
{
public:
    LogTarget(QString name, LogTarget* parent, QStringList* log)
        : m_name(std::move(name)), m_parent(parent), m_log(log)
    {
    }

    [[nodiscard]] md::EventTarget* parentTarget() const noexcept override { return m_parent; }

    [[nodiscard]] QPointF mapPointFrom(const md::EventTarget&, const QPointF& point) const override
    {
        return point;
    }

    bool onEvent(md::Event& event) override
    {
        m_log->append(QStringLiteral("%1  %2").arg(phaseName(event.phase()), m_name));
        return false;
    }

private:
    QString m_name;
    LogTarget* m_parent = nullptr;
    QStringList* m_log = nullptr;
};

QWidget* eventDemo(const Theme& theme)
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* log = new QLabel(QStringLiteral("Press the button to dispatch a pointer event."));
    log->setFont(fontFor(theme.type().bodyMedium, theme.density()));
    log->setStyleSheet(QStringLiteral("color:%1;background:%2;border-radius:10px;padding:14px;")
                           .arg(theme.colours().onSurfaceVariant.name(),
                                theme.colours().surfaceContainer.name()));
    log->setMinimumHeight(150);
    log->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto* dispatch = new QPushButton(QStringLiteral("Dispatch pointer event"));
    dispatch->setCursor(Qt::PointingHandCursor);

    QObject::connect(dispatch, &QPushButton::clicked, log, [log]() {
        QStringList order;
        LogTarget root(QStringLiteral("root"), nullptr, &order);
        LogTarget middle(QStringLiteral("middle"), &root, &order);
        LogTarget leaf(QStringLiteral("leaf"), &middle, &order);

        md::PointerEvent event(md::EventType::PointerDown, QPointF(12, 12), QPointF(120, 120),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier,
                               std::chrono::milliseconds(0));
        md::EventDispatcher::dispatch(event, leaf);

        log->setText(QStringLiteral("Capture runs root → middle, then the target, then the "
                                    "bubble walks back:\n\n%1")
                         .arg(order.join(QLatin1Char('\n'))));
    });

    layout->addWidget(log);
    layout->addWidget(dispatch, 0, Qt::AlignLeft);
    return host;
}

Page* overviewPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Periodicity"));
    page->addLead(QStringLiteral(
        "A Material Design 3 component library for Qt Widgets, painted entirely with QPainter. "
        "Every sample in this demo is produced by the library's own modules — nothing here is a "
        "mock-up."));

    auto* table = new QWidget;
    auto* grid = new QGridLayout(table);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setVerticalSpacing(8);

    const std::array<std::pair<const char*, const char*>, 10> modules{{
        {"core", "L0 — units, geometry, state"},      {"tokens", "L1 — colour, type, shape, motion"},
        {"event", "L3 — dispatch, bubbling, gestures"}, {"theme", "L2 — theme bundle and scopes"},
        {"animation", "L3 — animator"},               {"drawable", "L3 — painting primitives"},
        {"layer", "L3 — background, elevation, foreground"}, {"view", "L4 — measure, arrange, paint"},
        {"layout", "L5 — containers and size classes"}, {"components", "L6 — public widgets"},
    }};

    int row = 0;
    for (const auto& [name, description] : modules) {
        const bool done = QString::fromLatin1(name) == QLatin1String("core")
            || QString::fromLatin1(name) == QLatin1String("tokens")
            || QString::fromLatin1(name) == QLatin1String("event")
            || QString::fromLatin1(name) == QLatin1String("theme")
            || QString::fromLatin1(name) == QLatin1String("animation");

        auto* badge = new QLabel(done ? QStringLiteral("done") : QStringLiteral("queued"));
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedWidth(68);
        badge->setFont(fontFor(theme.type().labelSmall, theme.density()));
        badge->setStyleSheet(
            QStringLiteral("color:%1;background:%2;border-radius:9px;padding:2px;")
                .arg(done ? theme.colours().onPrimaryContainer.name()
                          : theme.colours().onSurfaceVariant.name(),
                     done ? theme.colours().primaryContainer.name()
                          : theme.colours().surfaceContainerHigh.name()));

        auto* module = new QLabel(QString::fromUtf8(name));
        module->setFont(fontFor(theme.type().bodyMedium, theme.density()));
        module->setStyleSheet(QStringLiteral("color:%1;").arg(theme.colours().onSurface.name()));

        auto* detail = new QLabel(QString::fromUtf8(description));
        detail->setFont(fontFor(theme.type().bodySmall, theme.density()));
        detail->setStyleSheet(
            QStringLiteral("color:%1;").arg(theme.colours().onSurfaceVariant.name()));

        grid->addWidget(badge, row, 0);
        grid->addWidget(module, row, 1);
        grid->addWidget(detail, row, 2);
        grid->setColumnStretch(2, 1);
        ++row;
    }

    page->addCard(QStringLiteral("Module status"),
                  QStringLiteral("Only implemented layers are shown as live demos."), table);
    page->addStretch();
    return page;
}

Page* colourPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Colour"));
    page->addLead(QStringLiteral(
        "Material 3 colour roles for the current scheme. Each swatch is the role's actual value, "
        "generated from the token layer."));

    const ColorScheme& colours = theme.colours();
    const std::vector<std::pair<QString, QColor>> roles{
        {QStringLiteral("primary"), colours.primary},
        {QStringLiteral("onPrimary"), colours.onPrimary},
        {QStringLiteral("primaryContainer"), colours.primaryContainer},
        {QStringLiteral("onPrimaryContainer"), colours.onPrimaryContainer},
        {QStringLiteral("secondary"), colours.secondary},
        {QStringLiteral("secondaryContainer"), colours.secondaryContainer},
        {QStringLiteral("tertiary"), colours.tertiary},
        {QStringLiteral("tertiaryContainer"), colours.tertiaryContainer},
        {QStringLiteral("error"), colours.error},
        {QStringLiteral("errorContainer"), colours.errorContainer},
        {QStringLiteral("surface"), colours.surface},
        {QStringLiteral("surfaceContainer"), colours.surfaceContainer},
        {QStringLiteral("surfaceContainerHigh"), colours.surfaceContainerHigh},
        {QStringLiteral("surfaceVariant"), colours.surfaceVariant},
        {QStringLiteral("onSurface"), colours.onSurface},
        {QStringLiteral("onSurfaceVariant"), colours.onSurfaceVariant},
        {QStringLiteral("outline"), colours.outline},
        {QStringLiteral("outlineVariant"), colours.outlineVariant},
        {QStringLiteral("inverseSurface"), colours.inverseSurface},
        {QStringLiteral("scrim"), colours.scrim},
    };

    page->addCard(QStringLiteral("Roles"),
                  QStringLiteral("Switch the theme in the header to see the dark scheme."),
                  colourGrid(theme, roles));
    page->addStretch();
    return page;
}

Page* typographyPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Typography"));
    page->addLead(QStringLiteral(
        "The fifteen Material 3 type styles, rendered at the token size, line height, tracking "
        "and weight."));
    page->addCard(QStringLiteral("Type scale"), QString(), typographyColumn(theme));
    page->addStretch();
    return page;
}

Page* shapePage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Shape"));
    page->addLead(QStringLiteral("Corner radii from the shape scale, drawn with "
                                 "CornerRadii::toPath."));
    page->addCard(QStringLiteral("Corner scale"), QString(), shapeRow(theme));
    page->addStretch();
    return page;
}

Page* elevationPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Elevation"));
    page->addLead(QStringLiteral(
        "Six elevation levels. Each surface uses surfaceAtElevation, so the tonal colour and the "
        "shadow agree."));
    page->addCard(QStringLiteral("Levels"), QString(), elevationRow(theme));
    page->addStretch();
    return page;
}

Page* motionPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Motion"));
    page->addLead(QStringLiteral(
        "The dot is moved by md::animation::Animator. Each button runs one rung of the duration "
        "ladder — 0, 250, 500, 750, 1000 and 1250 ms — all on the standard curve, so pressing two "
        "buttons compares speed and nothing else."));
    page->addCard(QStringLiteral("Emphasised motion"), QString(), motionRow(theme));
    page->addStretch();
    return page;
}

// Demonstrates the priority ladder: tasks posted lower first still run after a
// later high-priority post, because the queue drains by priority, not by arrival.
QWidget* priorityDemo(const Theme& theme)
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* log = new QLabel(QStringLiteral("Nothing posted yet."));
    log->setFont(fontFor(theme.type().bodyMedium, theme.density()));
    log->setStyleSheet(QStringLiteral("color:%1;background:%2;border-radius:10px;padding:14px;")
                           .arg(theme.colours().onSurfaceVariant.name(),
                                theme.colours().surfaceContainer.name()));
    log->setMinimumHeight(160);
    log->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto lines = std::make_shared<QStringList>();
    auto refresh = [log, lines]() {
        log->setText(lines->isEmpty()
                         ? QStringLiteral("Nothing posted yet.")
                         : QStringLiteral("Execution order:\n%1").arg(lines->join(QLatin1Char('\n'))));
    };

    auto enqueue = [lines, refresh](md::schedule::Priority priority, const QString& name) {
        md::schedule::TaskQueue::main().post(priority, [lines, refresh, name]() {
            lines->append(QStringLiteral("%1  %2").arg(name, QString::number(lines->size() + 1)));
            refresh();
        });
    };

    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);

    const std::array<std::pair<md::schedule::Priority, const char*>, 5> ladder{{
        {md::schedule::Priority::UserInput, "UserInput"},
        {md::schedule::Priority::UiUpdate, "UiUpdate"},
        {md::schedule::Priority::UiEvent, "UiEvent"},
        {md::schedule::Priority::Background, "Background"},
        {md::schedule::Priority::Idle, "Idle"},
    }};

    int column = 0;
    for (const auto& [priority, label] : ladder) {
        auto* button = new QPushButton(QStringLiteral("Post %1").arg(QString::fromUtf8(label)));
        button->setCursor(Qt::PointingHandCursor);
        QObject::connect(button, &QPushButton::clicked, host, [enqueue, priority, name = QString::fromUtf8(label)]() {
            enqueue(priority, name);
        });
        grid->addWidget(button, 0, column++);
    }

    auto* postAll = new QPushButton(QStringLiteral("Post all, lowest priority first"));
    postAll->setCursor(Qt::PointingHandCursor);
    QObject::connect(postAll, &QPushButton::clicked, host, [enqueue]() {
        md::schedule::TaskQueue::main().cancel(0);
        enqueue(md::schedule::Priority::Idle, QStringLiteral("Idle"));
        enqueue(md::schedule::Priority::Background, QStringLiteral("Background"));
        enqueue(md::schedule::Priority::UiEvent, QStringLiteral("UiEvent"));
        enqueue(md::schedule::Priority::UiUpdate, QStringLiteral("UiUpdate"));
        enqueue(md::schedule::Priority::UserInput, QStringLiteral("UserInput"));
    });

    auto* clear = new QPushButton(QStringLiteral("Clear"));
    clear->setCursor(Qt::PointingHandCursor);
    QObject::connect(clear, &QPushButton::clicked, host, [lines, refresh]() {
        lines->clear();
        refresh();
    });

    layout->addWidget(log);
    layout->addLayout(grid);
    layout->addWidget(postAll, 0, Qt::AlignLeft);
    layout->addWidget(clear, 0, Qt::AlignLeft);

    // Used by --post-priority so a capture shows the ladder without clicking.
    if (QApplication::arguments().contains(QStringLiteral("--post-priority"))) {
        QTimer::singleShot(300, host, [enqueue]() {
            enqueue(md::schedule::Priority::Idle, QStringLiteral("Idle"));
            enqueue(md::schedule::Priority::Background, QStringLiteral("Background"));
            enqueue(md::schedule::Priority::UiEvent, QStringLiteral("UiEvent"));
            enqueue(md::schedule::Priority::UiUpdate, QStringLiteral("UiUpdate"));
            enqueue(md::schedule::Priority::UserInput, QStringLiteral("UserInput"));
        });
    }

    return host;
}

Page* surfacesPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Surfaces"));
    page->addLead(QStringLiteral(
        "A surface is painted in stages: background, elevation shadow, content, state overlay and "
        "focus ring. Hover, press or tab into a tile to drive the state bits the layers resolve "
        "against — the shadow comes from the real ShadowDrawable, not QGraphicsEffect."));

    auto* host = new QWidget;
    auto* row = new QHBoxLayout(host);
    row->setContentsMargins(0, 8, 0, 8);
    row->setSpacing(20);
    row->addWidget(new SurfaceTile(theme, Elevation::Level1, QStringLiteral("Level 1")));
    row->addWidget(new SurfaceTile(theme, Elevation::Level3, QStringLiteral("Level 3")));
    row->addWidget(new SurfaceTile(theme, Elevation::Level5, QStringLiteral("Level 5")));
    row->addStretch(1);

    page->addCard(QStringLiteral("Elevation and interaction"),
                  QStringLiteral("Hover, press and Tab through the tiles."), host);
    page->addStretch();
    return page;
}

Page* eventPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Events"));
    page->addLead(QStringLiteral(
        "Dispatch walks the ancestor chain twice: capture descends, the bubble ascends. The log "
        "shows the exact order."));
    page->addCard(QStringLiteral("Capture and bubble"), QString(), eventDemo(theme));
    page->addStretch();
    return page;
}

Page* ripplePage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Ripple"));
    page->addLead(QStringLiteral(
        "Move the pointer over the surface: one hover ripple plays from the pointer position. "
        "Click for a stronger press ripple. Both expand until they fill the surface, then fade "
        "out — neither follows the pointer or stays behind as a marker."));

    auto* surface = new RippleSurface(theme);
    page->addCard(QStringLiteral("Pointer feedback"),
                  QStringLiteral("Hover, then click anywhere inside the rounded surface."), surface);

    if (QApplication::arguments().contains(QStringLiteral("--demo-ripple"))) {
        QTimer::singleShot(400, surface, [surface]() { surface->demonstrate(); });
    }

    page->addStretch();
    return page;
}

Page* priorityPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Task priority"));
    page->addLead(QStringLiteral(
        "Deferred work is queued with a priority: direct user input first, then layout and paint, "
        "then UI events, then background work. Post the whole ladder lowest-first and the log "
        "still starts with UserInput."));
    page->addCard(QStringLiteral("Priority ladder"),
                  QStringLiteral("UserInput → UiUpdate → UiEvent → Background → Idle"),
                  priorityDemo(theme));
    page->addStretch();
    return page;
}

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(Theme theme, int index = 0) : m_theme(std::move(theme))
    {
        m_dark = m_theme.colours().surface.lightness() < 128;
        rebuild(index);
    }

protected:
    void rebuild(int index)
    {
        m_index = index;
        auto* central = new QWidget;
        auto* layout = new QHBoxLayout(central);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* sidebar = new QListWidget;
        sidebar->setFixedWidth(kSidebarWidth);
        sidebar->setFrameShape(QFrame::NoFrame);
        sidebar->setFocusPolicy(Qt::NoFocus);
        sidebar->setFont(fontFor(m_theme.type().bodyMedium, m_theme.density()));
        const QStringList topics{QStringLiteral("Overview"), QStringLiteral("Colour"),
                                 QStringLiteral("Typography"), QStringLiteral("Shape"),
                                 QStringLiteral("Elevation"), QStringLiteral("Surfaces"),
                                 QStringLiteral("Motion"), QStringLiteral("Events"),
                                 QStringLiteral("Ripple"), QStringLiteral("Priority")};
        for (const QString& topic : topics)
            sidebar->addItem(topic);
        sidebar->setStyleSheet(
            QStringLiteral("QListWidget{background:%1;color:%2;border:none;padding:12px 8px;}"
                           "QListWidget::item{padding:10px 14px;border-radius:10px;}"
                           "QListWidget::item:selected{background:%3;color:%4;}")
                .arg(m_theme.colours().surfaceContainerLow.name(),
                     m_theme.colours().onSurfaceVariant.name(),
                     m_theme.colours().secondaryContainer.name(),
                     m_theme.colours().onSecondaryContainer.name()));

        auto* right = new QWidget;
        auto* rightLayout = new QVBoxLayout(right);
        rightLayout->setContentsMargins(0, 0, 0, 0);
        rightLayout->setSpacing(0);

        auto* header = new QWidget;
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(32, 20, 32, 20);

        auto* brand = new QLabel(QStringLiteral("Periodicity"));
        brand->setFont(fontFor(m_theme.type().titleLarge, m_theme.density()));
        brand->setStyleSheet(QStringLiteral("color:%1;").arg(m_theme.colours().onSurface.name()));

        auto* subtitle = new QLabel(QStringLiteral("Material Design 3 for Qt Widgets"));
        subtitle->setFont(fontFor(m_theme.type().labelMedium, m_theme.density()));
        subtitle->setStyleSheet(
            QStringLiteral("color:%1;").arg(m_theme.colours().onSurfaceVariant.name()));

        auto* themeButton = new QPushButton(m_dark ? QStringLiteral("Light") : QStringLiteral("Dark"));
        themeButton->setCursor(Qt::PointingHandCursor);
        themeButton->setFixedHeight(38);
        themeButton->setStyleSheet(
            QStringLiteral("QPushButton{background:%1;color:%2;border:none;border-radius:19px;"
                           "padding:0 20px;}"
                           "QPushButton:hover{background:%3;}")
                .arg(m_theme.colours().primary.name(), m_theme.colours().onPrimary.name(),
                     m_theme.colours().primaryContainer.name()));
        QObject::connect(themeButton, &QPushButton::clicked, this, [this]() { toggleTheme(); });

        headerLayout->addWidget(brand);
        headerLayout->addSpacing(12);
        headerLayout->addWidget(subtitle);
        headerLayout->addStretch(1);
        headerLayout->addWidget(themeButton);
        header->setStyleSheet(QStringLiteral("background:%1;").arg(m_theme.colours().surface.name()));

        auto* stack = new QStackedWidget;
        stack->addWidget(overviewPage(m_theme));
        stack->addWidget(colourPage(m_theme));
        stack->addWidget(typographyPage(m_theme));
        stack->addWidget(shapePage(m_theme));
        stack->addWidget(elevationPage(m_theme));
        stack->addWidget(surfacesPage(m_theme));
        stack->addWidget(motionPage(m_theme));
        stack->addWidget(eventPage(m_theme));
        stack->addWidget(ripplePage(m_theme));
        stack->addWidget(priorityPage(m_theme));

        QObject::connect(sidebar, &QListWidget::currentRowChanged, this,
                         [this, stack](int row) {
                             m_index = row;
                             stack->setCurrentIndex(row);
                         });
        sidebar->setCurrentRow(qBound(0, index, stack->count() - 1));

        rightLayout->addWidget(header);
        rightLayout->addWidget(stack, 1);

        layout->addWidget(sidebar);
        layout->addWidget(right, 1);

        setCentralWidget(central);
        resize(1180, 820);
        setWindowTitle(QStringLiteral("Periodicity — Material Design 3 Gallery"));
    }

    void toggleTheme()
    {
        m_dark = !m_dark;
        m_theme = m_dark ? Theme::baselineDark() : Theme::baselineLight();
        Theme::setApplicationTheme(m_theme);
        QApplication::setPalette(m_theme.toPalette());

        // Rebuild on the next turn: the button that triggered this is a child
        // of the central widget that rebuild() replaces, and deleting a sender
        // while its signal is being delivered is undefined behaviour.
        const int index = m_index;
        QTimer::singleShot(0, this, [this, index]() { rebuild(index); });
    }

private:
    Theme m_theme;
    bool m_dark = false;
    int m_index = 0;
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    Theme theme = Theme::baselineLight();

    const QStringList startupArguments = QApplication::arguments();
    int startPage = 0;
    const int pageIndex = startupArguments.indexOf(QStringLiteral("--page"));
    if (pageIndex >= 0 && pageIndex + 1 < startupArguments.size())
        startPage = startupArguments.at(pageIndex + 1).toInt();

    if (startupArguments.contains(QStringLiteral("--dark")))
        theme = Theme::baselineDark();

    Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());
    QApplication::setFont(fontFor(theme.type().bodyMedium, theme.density()));

    MainWindow window(theme, startPage);
    window.show();

    // --screenshot <path> renders the window through QWidget::grab and exits,
    // so the demo can be captured without depending on window stacking.
    const int shotIndex = startupArguments.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 < startupArguments.size()) {
        const QString path = startupArguments.at(shotIndex + 1);

        int shotDelay = 800;
        const int delayIndex = startupArguments.indexOf(QStringLiteral("--screenshot-delay"));
        if (delayIndex >= 0 && delayIndex + 1 < startupArguments.size())
            shotDelay = startupArguments.at(delayIndex + 1).toInt();

        QTimer::singleShot(shotDelay, &window, [&window, path]() {
            const bool saved = window.grab().save(path);
            QApplication::exit(saved ? 0 : 1);
        });
    }

    return app.exec();
}
