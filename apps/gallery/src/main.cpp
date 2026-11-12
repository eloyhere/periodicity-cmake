// Periodicity — gallery: a visual test bench for the public component API.
//
// The binary exists to be looked at and touched. Every page is a hand-written
// composition of real component instances (no screenshots, no mocks), so a
// regression in measurement, painting or interaction is visible the moment the
// page is opened. It also supports an unattended "--screenshot <dir>" mode
// that renders every page to a PNG, which makes the gallery usable as a smoke
// test on a headless build machine.
//
// The shell is laid out by the library's own md::Constraint: the brand sits at
// start 24 / top 20, the theme toggle at end 24 / top 16, and the page stack
// stretches from below the header to the bottom edge. Every page repeats the
// idea one level down: its sections are Constraint children that stretch between
// the start and end anchors, with each top accumulated from the previous
// section's height, so no layout manager places the cards.
//
// The menu components (MenuGroup, MenuItem, SubMenu) are demonstrated on the
// Overlays page under "Menu rows". Their design still follows Material's
// navigation drawer guidance: a section label above a group of destinations,
// with 16 dp of indentation per nesting level. See
// https://m3.material.io/components/navigation-drawer/specs and
// https://m2.material.io/components/navigation-drawer.

#include <md/components/affix.h>
#include <md/components/alert.h>
#include <md/components/anchor.h>
#include <md/components/avatar.h>
#include <md/components/badge.h>
#include <md/components/banner.h>
#include <md/components/bottom_sheet.h>
#include <md/components/button.h>
#include <md/components/chip.h>
#include <md/components/divider.h>
#include <md/components/fab.h>
#include <md/components/icon_button.h>
#include <md/components/navigation_bar.h>
#include <md/components/search_bar.h>
#include <md/components/switch.h>
#include <md/components/bar_chart.h>
#include <md/components/box_plot_chart.h>
#include <md/components/calendar.h>
#include <md/components/canvas.h>
#include <md/components/card.h>
#include <md/components/carousel.h>
#include <md/components/cascader.h>
#include <md/components/chart_common.h>
#include <md/components/checkbox.h>
#include <md/components/clock.h>
#include <md/components/context1d.h>
#include <md/components/context2d.h>
#include <md/components/context3d.h>
#include <md/components/date_picker.h>
#include <md/components/datetime_picker.h>
#include <md/components/drawer.h>
#include <md/components/font.h>
#include <md/components/datetime_picker.h>
#include <md/components/dialog.h>
#include <md/components/form.h>
#include <md/components/funnel_chart.h>
#include <md/components/guide.h>
#include <md/components/heatmap_chart.h>
#include <md/components/histogram_chart.h>
#include <md/components/image.h>
#include <md/components/input.h>
#include <md/components/language.h>
#include <md/components/line_chart.h>
#include <md/components/link.h>
#include <md/components/list.h>
#include <md/components/marquee.h>
#include <md/components/math_coordination.h>
#include <md/components/menu.h>
#include <md/components/menu_group.h>
#include <md/components/menu_item.h>
#include <md/components/mention.h>
#include <md/components/model.h>
#include <md/components/navigation_view.h>
#include <md/components/notification.h>
#include <md/components/page.h>
#include <md/components/pager.h>
#include <md/components/pagination.h>
#include <md/components/pie_chart.h>
#include <md/components/popup.h>
#include <md/components/progress.h>
#include <md/components/radar_chart.h>
#include <md/components/radio.h>
#include <md/components/rate.h>
#include <md/components/recycler.h>
#include <md/components/ring.h>
#include <md/components/scatter_chart.h>
#include <md/components/schedule.h>
#include <md/components/scheduler.h>
#include <md/components/scrollbar.h>
#include <md/components/seekbar.h>
#include <md/components/skeleton.h>
#include <md/components/state.h>
#include <md/components/sub_menu.h>
#include <md/components/tabbar.h>
#include <md/components/table.h>
#include <md/components/tag.h>
#include <md/components/tree_select.h>
#include <md/components/text.h>
#include <md/components/theme_switcher.h>
#include <md/components/timeline.h>
#include <md/components/time_picker.h>
#include <md/components/toolbar.h>
#include <md/components/transfer.h>
#include <md/components/typography.h>
#include <md/components/video.h>
#include <md/components/virtual_select.h>
#include <md/components/virtual_table.h>
#include <md/components/virtual_tree_select.h>
#include <md/core/measure_spec.h>
#include <md/layout/appbar.h>
#include <md/layout/collapse.h>
#include <md/layout/constraint.h>
#include <md/layout/frame.h>
#include <md/layout/linear.h>
#include <md/structure/table.h>
#include <md/structure/virtual_source.h>
#include <md/theme/theme.h>

#include "pool_demo.h"

#include <QApplication>
#include <QBuffer>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QMainWindow>
#include <QPainter>
#include <QPair>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTime>
#include <QElapsedTimer>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace {

using md::Theme;

constexpr int kSidebarWidth = 264;

// The theme page asks the window to rebuild with a new palette. A page cannot
// reach the window object, so the window installs this hook before it builds
// the pages.
std::function<void(const md::Theme&)> requestTheme;

// The pool page borrows the window-owned demo. A page builder receives only
// the theme, so the window installs this hook before it builds the pages — the
// same shape as requestTheme above.
PoolDemo* g_poolDemo = nullptr;

// Page geometry, in device-independent pixels. The column is capped and centred
// so that a wide window does not stretch a card across the whole screen.
constexpr int kSideInsetPx = 40;
constexpr int kTopInsetPx = 32;
constexpr int kBottomInsetPx = 48;
constexpr int kMaxColumnPx = 880;
constexpr int kLeadGapPx = 12;
constexpr int kCardGapPx = 20;
constexpr int kStretchExtraPx = 96;
constexpr int kCardSidePaddingPx = 24;
constexpr int kCardTopPaddingPx = 20;
constexpr int kCardBottomPaddingPx = 24;
// A View adapter that hosts one plain QWidget.
//
// md::Constraint can only arrange Views, while the gallery's demo content is
// made of ordinary Qt widgets (labels, rows, composed panels). WidgetHost gives
// such a widget a View body: it measures the content at the available width and
// paints the card surface when asked to. Stretch mode adopts the rectangle the
// constraint hands it (the page stack); the other modes keep the content's
// natural height, using heightForWidth when the content wraps.
class WidgetHost final : public md::View
{
public:
    enum class Mode {
        Plain,   // no surface, no padding: a title or a lead paragraph
        Card,    // surfaceContainerLow card with the medium radius and a contour
        Stretch, // fills whatever rectangle the parent arranges it in
    };

    explicit WidgetHost(QWidget* content, Mode mode = Mode::Stretch,
                        QWidget* parent = nullptr)
        : md::View(parent)
        , content_(content)
        , mode_(mode)
    {
        setFocusPolicy(Qt::NoFocus);
        setBackgroundColour(QColor(Qt::transparent));

        if (content_ != nullptr) {
            content_->setParent(this);
            content_->show();
        }
    }

    [[nodiscard]] QWidget* contentWidget() const noexcept { return content_; }

protected:
    void measureOverride(md::MeasureSpec widthSpec, md::MeasureSpec heightSpec) override
    {
        const qreal naturalWidth = content_ != nullptr
            ? static_cast<qreal>(content_->sizeHint().width()) + 2.0 * horizontalPadding()
            : 0.0;
        const qreal width = widthSpec.resolve(naturalWidth);

        qreal height = 0.0;
        if (mode_ == Mode::Stretch) {
            // The parent already decided the rectangle (top and bottom anchors);
            // there is no natural height to report.
            height = heightSpec.mode() == md::MeasureSpec::Mode::Unspecified
                ? 0.0
                : heightSpec.size();
        } else {
            height = contentHeight(width);
        }

        setMeasuredSize(QSizeF(width, heightSpec.resolve(height)));
    }

    void arrangeOverride(const QRectF& content) override
    {
        if (content_ == nullptr)
            return;

        const int side = static_cast<int>(qRound(horizontalPadding()));
        const int top = static_cast<int>(qRound(topPadding()));
        const int bottom = static_cast<int>(qRound(bottomPadding()));

        content_->setGeometry(content.toAlignedRect().adjusted(side, top, -side, -bottom));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        if (mode_ != Mode::Card || content.isEmpty())
            return;

        const Theme& theme = Theme::forWidget(this);
        const qreal radius = theme.shapes().medium.radii().topStart;

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(theme.colours().outlineVariant, 1.0));
        painter.setBrush(theme.colours().surfaceContainerLow);
        painter.drawRoundedRect(content.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter.restore();
    }

    void onThemeChanged() override
    {
        updateGeometry();
        requestUpdate();
    }

private:
    [[nodiscard]] qreal horizontalPadding() const
    {
        return mode_ == Mode::Card
            ? Theme::forWidget(this).density().dp(kCardSidePaddingPx)
            : 0.0;
    }

    [[nodiscard]] qreal topPadding() const
    {
        return mode_ == Mode::Card
            ? Theme::forWidget(this).density().dp(kCardTopPaddingPx)
            : 0.0;
    }

    [[nodiscard]] qreal bottomPadding() const
    {
        return mode_ == Mode::Card
            ? Theme::forWidget(this).density().dp(kCardBottomPaddingPx)
            : 0.0;
    }

    [[nodiscard]] qreal contentHeight(qreal frameWidth) const
    {
        if (content_ == nullptr)
            return 0.0;

        const int innerWidth =
            qMax(0, static_cast<int>(qRound(frameWidth - 2.0 * horizontalPadding())));

        qreal height = 0.0;
        if (innerWidth <= 0)
            height = static_cast<qreal>(content_->sizeHint().height());
        else if (content_->hasHeightForWidth())
            height = static_cast<qreal>(content_->heightForWidth(innerWidth));
        else
            height = static_cast<qreal>(content_->sizeHint().height());

        return height + topPadding() + bottomPadding();
    }

    QWidget* content_ = nullptr;
    Mode mode_ = Mode::Stretch;
};

// A card body that refreshes itself while it is on screen.
//
// The pool page is a read-out, not a form: every value changes without the
// user touching anything. A Ticker owns the 120 ms timer, starts it in
// showEvent and stops it in hideEvent, so a hidden page — or the pages the
// theme rebuild throws away — costs nothing.
class Ticker final : public QWidget
{
public:
    explicit Ticker(std::function<void()> tick, QWidget* parent = nullptr)
        : QWidget(parent)
        , tick_(std::move(tick))
    {
        timer_.setInterval(120);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] {
            if (tick_)
                tick_();
        });
    }

protected:
    void showEvent(QShowEvent* event) override
    {
        QWidget::showEvent(event);
        if (tick_)
            tick_();
        timer_.start();
    }

    void hideEvent(QHideEvent* event) override
    {
        timer_.stop();
        QWidget::hideEvent(event);
    }

private:
    QTimer timer_;
    std::function<void()> tick_;
};

// A card body plus the column inside it, so a page can compose the labels,
// bars and buttons of one ticker without repeating the layout boilerplate.
struct CardBody
{
    Ticker* body = nullptr;
    QVBoxLayout* layout = nullptr;
};

CardBody makeCardBody(std::function<void()> tick)
{
    CardBody card;
    card.body = new Ticker(std::move(tick));
    card.layout = new QVBoxLayout(card.body);
    card.layout->setContentsMargins(0, 0, 0, 0);
    card.layout->setSpacing(10);
    return card;
}

// One scrollable page: a title, a lead paragraph and a column of cards.
//
// The page body is a single md::Constraint. Every section (title, lead, card) is
// a WidgetHost inside it: the sections stretch between the start and end anchors,
// and each section's top is the previous section's bottom plus the gap. A
// Constraint anchor is a distance from the parent's edge, not a reference to a
// sibling, so the offsets are accumulated from the measured heights here — the
// column is still constraint-driven and no layout manager places the cards. The
// scroll range follows the constraint's height.
class Page : public QScrollArea
{
public:
    explicit Page(const Theme& theme, QWidget* parent = nullptr)
        : QScrollArea(parent)
        , theme_(theme)
    {
        setWidgetResizable(false);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        body_ = new QWidget;
        body_->setObjectName(QStringLiteral("galleryPageBody"));
        body_->setAttribute(Qt::WA_StyledBackground, true);
        // The rule is scoped to the body: a selector-less background would be
        // inherited by every descendant and paint a surface rectangle inside
        // each card.
        body_->setStyleSheet(QStringLiteral("#galleryPageBody{background:%1;}")
                                 .arg(theme.colours().surface.name()));

        setWidget(body_);
    }

    void addTitle(const QString& text)
    {
        auto* label = new QLabel(text);
        label->setFont(mdFont(theme_.type().headlineMedium));
        label->setStyleSheet(
            QStringLiteral("color:%1;").arg(theme_.colours().onSurface.name()));
        addSection(label, WidgetHost::Mode::Plain, 0);
    }

    void addLead(const QString& text)
    {
        auto* label = new QLabel(text);
        label->setWordWrap(true);
        label->setFont(mdFont(theme_.type().bodyLarge));
        label->setStyleSheet(
            QStringLiteral("color:%1;").arg(theme_.colours().onSurfaceVariant.name()));
        addSection(label, WidgetHost::Mode::Plain, kLeadGapPx);
    }

    // A card groups one control family: a heading, a short note, then the
    // caller-composed demo row. The card's rectangle comes from the constraint.
    void addCard(const QString& title, const QString& note, QWidget* content)
    {
        auto* box = new QWidget;
        auto* column = new QVBoxLayout(box);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(12);

        auto* heading = new QLabel(title);
        heading->setFont(mdFont(theme_.type().titleMedium));
        heading->setStyleSheet(
            QStringLiteral("color:%1;").arg(theme_.colours().onSurface.name()));
        column->addWidget(heading);

        if (!note.isEmpty()) {
            auto* caption = new QLabel(note);
            caption->setWordWrap(true);
            caption->setFont(mdFont(theme_.type().bodyMedium));
            caption->setStyleSheet(
                QStringLiteral("color:%1;").arg(theme_.colours().onSurfaceVariant.name()));
            column->addWidget(caption);
        }

        column->addWidget(content);
        WidgetHost* host = addSection(box, WidgetHost::Mode::Card, kCardGapPx);
        cards_.append(qMakePair(title, host));
    }

    [[nodiscard]] QStringList cardTitles() const
    {
        QStringList titles;
        for (const auto& card : cards_)
            titles.append(card.first);
        return titles;
    }

    // Scrolls the page so the named card is at the top of the viewport.
    void scrollToCard(const QString& title)
    {
        for (const auto& card : cards_) {
            if (card.first != title || card.second == nullptr)
                continue;
            layoutSections();
            verticalScrollBar()->setValue(qMax(0, card.second->y() - kTopInsetPx));
            return;
        }
    }

    // Trailing breathing room below the last card.
    void addStretch() { hasStretch_ = true; }

    // The scroll range depends on the constraint height, which only exists once
    // the viewport has a width; a page shown after the window layout therefore
    // lays itself out on show as well as on resize.
    void showEvent(QShowEvent* event) override
    {
        QScrollArea::showEvent(event);
        layoutSections();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QScrollArea::resizeEvent(event);
        layoutSections();
    }

private:
    struct Section
    {
        WidgetHost* host = nullptr;
        int gapAbove = 0;
    };

    WidgetHost* addSection(QWidget* content, WidgetHost::Mode mode, int gapAbove)
    {
        // The page is a plain scrolling column: each section is a widget with
        // its own geometry inside the fixed-size body. A Constraint here would
        // clamp a section's height to the remaining viewport and cut the tail of
        // a long page, which is exactly what blocked scrolling.
        auto* host = new WidgetHost(content, mode, body_);
        sections_.push_back(Section{host, gapAbove});
        layoutSections();
        return host;
    }

    void layoutSections()
    {
        const int viewportWidth = viewport()->width();
        if (viewportWidth <= 0 || sections_.empty())
            return;

        const int sideInset = qMax(kSideInsetPx, (viewportWidth - kMaxColumnPx) / 2);
        const int columnWidth = viewportWidth - 2 * sideInset;
        if (columnWidth <= 0)
            return;

        int y = kTopInsetPx;
        for (Section& section : sections_) {
            if (section.host == nullptr)
                continue;

            y += section.gapAbove;
            section.host->measure(md::MeasureSpec::exactly(columnWidth),
                                  md::MeasureSpec::unspecified());
            const int height =
                static_cast<int>(std::ceil(section.host->measuredSize().height()));

            section.host->arrange(QRect(sideInset, y, columnWidth, height));

            y += height;
        }

        y += hasStretch_ ? kBottomInsetPx + kStretchExtraPx : kBottomInsetPx;

        const int contentHeight = qMax(y, viewport()->height());
        body_->setFixedSize(viewportWidth, contentHeight);
    }

    QFont mdFont(const md::TextStyle& style) const
    {
        QFont font(QStringLiteral("Roboto"));
        font.setFamilies({QStringLiteral("Roboto"), QStringLiteral("Segoe UI"),
                          QStringLiteral("Microsoft YaHei UI")});
        font.setPixelSize(qMax(9, qRound(theme_.density().sp(style.sizeSp))));
        font.setWeight(static_cast<QFont::Weight>(style.weight));
        font.setLetterSpacing(QFont::AbsoluteSpacing,
                              theme_.density().sp(style.trackingSp));
        return font;
    }

    Theme theme_;
    QWidget* body_ = nullptr;
    md::Constraint* content_ = nullptr;
    std::vector<Section> sections_;
    QVector<QPair<QString, WidgetHost*>> cards_;
    bool hasStretch_ = false;
};

// A horizontal row that keeps the components at their natural size.
QWidget* row(std::initializer_list<QWidget*> widgets, int spacing = 16)
{
    auto* host = new QWidget;
    auto* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
    for (QWidget* widget : widgets)
        layout->addWidget(widget, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    return host;
}

// A vertical stack used for radio groups and other multi-row samples.
QWidget* column(std::initializer_list<QWidget*> widgets, int spacing = 10)
{
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
    for (QWidget* widget : widgets)
        layout->addWidget(widget, 0, Qt::AlignLeft);
    return host;
}

QWidget* basicPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Basic components"));
    page->addLead(QStringLiteral(
        "The primitives every other page builds on: buttons, selection controls, "
        "scores, progress and sliders. Every widget here is a md::View, painted with "
        "QPainter through the shared state layer and ripple pipeline."));

    auto* filled = new md::Button(QStringLiteral("Filled"));
    auto* tonal = new md::Button(QStringLiteral("Tonal"));
    tonal->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* elevated = new md::Button(QStringLiteral("Elevated"));
    elevated->setVariant(md::Button::ButtonVariant::Elevated);
    auto* outlined = new md::Button(QStringLiteral("Outlined"));
    outlined->setVariant(md::Button::ButtonVariant::Outlined);
    auto* text = new md::Button(QStringLiteral("Text"));
    text->setVariant(md::Button::ButtonVariant::Text);
    auto* disabled = new md::Button(QStringLiteral("Disabled"));
    disabled->setEnabled(false);

    auto* counter = new md::Text(QStringLiteral("Clicked 0 times"));
    counter->setRole(md::Text::TextRole::Label);
    QObject::connect(filled, &md::Button::clicked, counter,
                     [counter] { counter->setText(QStringLiteral("Clicked — filled")); });
    QObject::connect(outlined, &md::Button::clicked, counter,
                     [counter] { counter->setText(QStringLiteral("Clicked — outlined")); });

    auto* buttonRow = row({filled, tonal, elevated, outlined, text, disabled});
    auto* buttonColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(buttonColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(buttonRow);
        layout->addWidget(counter, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Button"),
                  QStringLiteral("Five variants. Hover, press and focus are drawn by the "
                                 "View pipeline; the label is the labelLarge token."),
                  buttonColumn);

    auto* unchecked = new md::CheckBox;
    auto* checked = new md::CheckBox;
    checked->setChecked(true);
    auto* tristate = new md::CheckBox;
    tristate->setTristate(true);
    tristate->setCheckState(Qt::PartiallyChecked);
    auto* checkboxNote = new md::Text(QStringLiteral("Checked state mirrors Qt::CheckState"));
    checkboxNote->setRole(md::Text::TextRole::Caption);

    auto* checkboxColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(checkboxColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(row({unchecked, checked, tristate}));
        layout->addWidget(checkboxNote, 0, Qt::AlignLeft);
    }
    QObject::connect(checked, &md::CheckBox::toggled, checkboxNote,
                     [checkboxNote](bool on) {
                         checkboxNote->setText(on ? QStringLiteral("on")
                                                  : QStringLiteral("off"));
                     });
    page->addCard(QStringLiteral("CheckBox"),
                  QStringLiteral("Two-state and tri-state. Space toggles the focused box."),
                  checkboxColumn);

    auto* first = new md::Radio;
    auto* second = new md::Radio;
    first->setGroupId(7);
    second->setGroupId(7);
    second->setChecked(true);
    auto* third = new md::Radio;
    third->setGroupId(7);
    page->addCard(QStringLiteral("Radio"),
                  QStringLiteral("Members of one group id are mutually exclusive; the group "
                                 "ignores clicks on an already-checked member."),
                  row({first, second, third}));

    auto* rate = new md::Rate;
    rate->setMax(5);
    rate->setStep(0.5);
    rate->setValue(3.5);
    auto* rateNote = new md::Text(QStringLiteral("value 3.5 / 5, step 0.5"));
    rateNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(rate, &md::Rate::valueChanged, rateNote, [rateNote](qreal value) {
        rateNote->setText(QStringLiteral("value %1 / 5").arg(value));
    });

    auto* rateColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(rateColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(row({rate}));
        layout->addWidget(rateNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Rate"),
                  QStringLiteral("Click or drag along the stars; Left/Right step the value."),
                  rateColumn);

    auto* linear = new md::Progress;
    linear->setValue(0.42);
    linear->setBuffer(0.65);

    auto* indeterminateLinear = new md::Progress;
    indeterminateLinear->setIndeterminate(true);

    auto* ring = new md::Ring;
    ring->setValue(0.65);

    auto* indeterminateRing = new md::Ring;
    indeterminateRing->setIndeterminate(true);

    page->addCard(QStringLiteral("Progress and Ring"),
                  QStringLiteral("Determinate, buffered and indeterminate. The ring is the "
                                 "Android-style determinate arc plus rotation animation."),
                  row({linear, ring, indeterminateLinear, indeterminateRing}, 28));

    auto* seek = new md::SeekBar;
    seek->setRange(0.0, 100.0);
    seek->setValue(35.0);
    seek->setTicksVisible(true);
    auto* seekNote = new md::Text(QStringLiteral("35"));
    seekNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(seek, &md::SeekBar::valueChanged, seekNote, [seekNote](qreal value) {
        seekNote->setText(QString::number(value, 'g', 3));
    });
    QObject::connect(seek, &md::SeekBar::valueChanged, seek,
                     [seek](qreal) { seek->update(); });

    auto* seekColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(seekColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(row({seek}));
        layout->addWidget(seekNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("SeekBar"),
                  QStringLiteral("Continuous drag with optional ticks; the value label is a "
                                 "Text component driven by valueChanged."),
                  seekColumn);

    page->addStretch();
    return page;
}

// A view that asks for its natural size, so a demo row does not stretch it.
template <class T>
T* natural(T* view)
{
    view->setFixedSize(view->sizeHint());
    return view;
}

// A container with an explicit rectangle: a ViewGroup only has a useful shape
// once something has told it how much room it owns.
template <class T>
T* bounded(T* view, int width, int height)
{
    view->measure(md::MeasureSpec::exactly(qreal(width)),
                  md::MeasureSpec::exactly(qreal(height)));
    view->setFixedSize(width, height);
    return view;
}

QWidget* containersPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Layout containers"));
    page->addLead(QStringLiteral(
        "Containers own child views and place them through the measure/arrange "
        "contract. Every child below is an ordinary component (Button, Text, Badge), "
        "so the page also proves that the containers compose with the public API."));

    // Linear — weights share the extra space of the main axis.
    auto* linear = new md::Linear(md::Linear::Orientation::Horizontal);
    linear->setSpacing(8);
    auto* one = new md::Button(QStringLiteral("weight 1"));
    auto* two = new md::Button(QStringLiteral("weight 2"));
    auto* three = new md::Button(QStringLiteral("weight 1"));
    linear->addView(one);
    linear->addView(two);
    linear->addView(three);
    linear->setWeight(one, 1.0);
    linear->setWeight(two, 2.0);
    linear->setWeight(three, 1.0);
    bounded(linear, 560, 56);
    page->addCard(QStringLiteral("Linear"),
                  QStringLiteral("Horizontal with weights 1/2/1 and 8 dp spacing. The "
                                 "vertical variant is used by Form and the page column."),
                  linear);

    // Frame — children stack; gravity places them inside the frame.
    auto* frame = new md::Frame;
    frame->setMargin(8);
    auto* corner = new md::Text(QStringLiteral("top start"));
    corner->setRole(md::Text::TextRole::Caption);
    auto* centred = new md::Button(QStringLiteral("centred"));
    auto* bottom = new md::Text(QStringLiteral("bottom end"));
    bottom->setRole(md::Text::TextRole::Caption);
    frame->addView(corner);
    frame->addView(centred);
    frame->addView(bottom);
    frame->setGravity(corner, Qt::AlignTop | Qt::AlignLeft);
    frame->setGravity(centred, Qt::AlignCenter);
    frame->setGravity(bottom, Qt::AlignBottom | Qt::AlignRight);
    bounded(frame, 420, 120);
    page->addCard(QStringLiteral("Frame"),
                  QStringLiteral("Later children paint above earlier ones; gravity aligns "
                                 "each child inside the shared rectangle."),
                  frame);

    // Constraint — parent anchors, stretching and centring.
    auto* constraint = new md::Constraint;
    auto* pinned = new md::Button(QStringLiteral("start/top 16"));
    auto* stretched = new md::Text(QStringLiteral("stretched between start and end"));
    stretched->setRole(md::Text::TextRole::Caption);
    auto* centredText = new md::Text(QStringLiteral("centred"));
    constraint->addView(pinned);
    constraint->addView(stretched);
    constraint->addView(centredText);

    md::Constraint::Anchors pinnedAnchors;
    pinnedAnchors.start = 16.0;
    pinnedAnchors.top = 12.0;
    constraint->setConstraints(pinned, pinnedAnchors);

    md::Constraint::Anchors stretchedAnchors;
    stretchedAnchors.start = 16.0;
    stretchedAnchors.end = 16.0;
    stretchedAnchors.top = 64.0;
    constraint->setConstraints(stretched, stretchedAnchors);

    md::Constraint::Anchors centredAnchors;
    centredAnchors.centerHorizontal = true;
    centredAnchors.centerVertical = true;
    constraint->setConstraints(centredText, centredAnchors);
    bounded(constraint, 560, 140);
    page->addCard(QStringLiteral("Constraint"),
                  QStringLiteral("Two opposing anchors stretch; one anchor pins; centring "
                                 "applies only to an axis without an anchor."),
                  constraint);

    // Collapse — a single expandable section with an animated height.
    auto* collapse = new md::Collapse;
    collapse->setTitle(QStringLiteral("Expandable section"));
    auto* body = new md::Text(QStringLiteral(
        "The content section animates its height with the theme's medium motion "
        "token. Click the header to toggle it."));
    body->setWordWrap(true);
    collapse->setContentWidget(body);
    collapse->setExpanded(true);
    collapse->setFixedWidth(560);
    collapse->setFixedHeight(static_cast<int>(collapse->preferredHeight()));
    QObject::connect(collapse, &md::Collapse::animationFinished, collapse, [collapse] {
        collapse->setFixedHeight(static_cast<int>(collapse->preferredHeight()));
    });
    page->addCard(QStringLiteral("Collapse"),
                  QStringLiteral("Header row plus one content area; expansion is a 0..1 "
                                 "value driven by QVariantAnimation."),
                  collapse);

    // AppBar — the bar collapses as the scroll offset grows.
    auto* appBar = new md::AppBar;
    appBar->setTitle(QStringLiteral("Collapsing app bar"));
    auto* nav = new md::Button(QStringLiteral("Nav"));
    nav->setVariant(md::Button::ButtonVariant::Text);
    auto* action = new md::Button(QStringLiteral("Action"));
    action->setVariant(md::Button::ButtonVariant::FilledTonal);
    appBar->setNavigationButton(nav);
    appBar->addAction(action);
    appBar->setScrollRange(96.0);
    bounded(appBar, 560, 112);

    auto* scroll = new md::SeekBar;
    scroll->setRange(0.0, 96.0);
    scroll->setValue(0.0);
    QObject::connect(scroll, &md::SeekBar::valueChanged, appBar,
                     [appBar](qreal value) { appBar->setScrollOffset(value); });

    auto* appBarColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(appBarColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(appBar);
        layout->addWidget(scroll);
    }
    page->addCard(QStringLiteral("AppBar"),
                  QStringLiteral("Expanded 112 dp collapses to 64 dp; the elevation rises "
                                 "from level 0 to level 2. Drag the slider to scroll."),
                  appBarColumn);

    // Coordinator — a behaviour receives the nested scroll instead of the bar.
    auto* coordinator = new md::Coordinator;
    auto* coordinatedBar = new md::AppBar;
    coordinatedBar->setTitle(QStringLiteral("Coordinator"));
    coordinatedBar->setScrollRange(80.0);
    coordinator->addView(coordinatedBar);
    coordinator->addBehaviour(
        coordinatedBar, std::make_unique<md::AppBarBehavior>(coordinatedBar));
    bounded(coordinator, 560, 160);

    auto* nested = new md::SeekBar;
    nested->setRange(0.0, 80.0);
    QObject::connect(nested, &md::SeekBar::valueChanged, coordinator,
                     [coordinator](qreal value) {
                         coordinator->startNestedScroll(Qt::Vertical);
                         QPointF consumed;
                         coordinator->dispatchNestedPreScroll(QPointF(0.0, value),
                                                              &consumed);
                     });

    auto* coordinatorColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(coordinatorColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(coordinator);
        layout->addWidget(nested);
    }
    page->addCard(QStringLiteral("Coordinator"),
                  QStringLiteral("The bar's AppBarBehavior intercepts a nested scroll, so "
                                 "the coordinator collapses the bar without either widget "
                                 "knowing the other."),
                  coordinatorColumn);

    page->addStretch();
    return page;
}

QWidget* datesPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Dates, cascades and mentions"));
    page->addLead(QStringLiteral(
        "Structured input: a six-week calendar, a date-time field composed with it, a "
        "hierarchical selector and a text editor in which an @mention is an atomic node."));

    auto* date = new md::DatePicker;
    natural(date);
    auto* dateNote = new md::Text(QStringLiteral("no date selected"));
    dateNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(date, &md::DatePicker::dateActivated, dateNote,
                     [dateNote](QDate value) {
                         dateNote->setText(value.toString(Qt::ISODate));
                     });

    auto* dateColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(dateColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(date, 0, Qt::AlignLeft);
        layout->addWidget(dateNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("DatePicker"),
                  QStringLiteral("Six-week grid, Monday first. Arrows change month, the "
                                 "title cycles days → months → years, and the arrow keys "
                                 "move the selection."),
                  dateColumn);

    auto* dateTime = new md::DateTimePicker;
    dateTime->setSecondsVisible(false);
    natural(dateTime);
    auto* dateTimeNote = new md::Text(QStringLiteral("left/right switch fields, up/down step"));
    dateTimeNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(dateTime, &md::DateTimePicker::dateTimeChanged, dateTimeNote,
                     [dateTimeNote](const QDateTime& value) {
                         dateTimeNote->setText(value.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                     });

    auto* dateTimeColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(dateTimeColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(dateTime, 0, Qt::AlignLeft);
        layout->addWidget(dateTimeNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("DateTimePicker"),
                  QStringLiteral("A calendar plus painted hour and minute fields; the "
                                 "value is clamped to the configured range."),
                  dateTimeColumn);

    auto* cascader = new md::Cascader;
    const QList<md::CascaderNode> levels{
        md::CascaderNode(QStringLiteral("Zhejiang"), QStringLiteral("zj"),
                         {md::CascaderNode(QStringLiteral("Hangzhou"), QStringLiteral("hz"),
                                           {md::CascaderNode(QStringLiteral("Xihu"),
                                                             QStringLiteral("xh")),
                                            md::CascaderNode(QStringLiteral("Binjiang"),
                                                             QStringLiteral("bj"))}),
                          md::CascaderNode(QStringLiteral("Ningbo"), QStringLiteral("nb"),
                                           {md::CascaderNode(QStringLiteral("Haishu"),
                                                             QStringLiteral("hs"))})}),
        md::CascaderNode(QStringLiteral("Jiangsu"), QStringLiteral("js"),
                         {md::CascaderNode(QStringLiteral("Nanjing"), QStringLiteral("nj"),
                                           {md::CascaderNode(QStringLiteral("Xuanwu"),
                                                             QStringLiteral("xw"))})})};
    cascader->setLevels(levels);
    cascader->setFixedSize(560, 190);
    auto* cascaderNote = new md::Text(QStringLiteral("path: —"));
    cascaderNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(cascader, &md::Cascader::pathChanged, cascaderNote,
                     [cascaderNote](const QStringList& path) {
                         cascaderNote->setText(QStringLiteral("path: %1")
                                                   .arg(path.join(QStringLiteral(" / "))));
                     });

    auto* cascaderColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(cascaderColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(cascader);
        layout->addWidget(cascaderNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Cascader"),
                  QStringLiteral("Click a parent to expand the next column; choosing a "
                                 "different parent drops the deeper selection."),
                  cascaderColumn);

    auto* mention = new md::Mention;
    mention->setCandidates({QStringLiteral("ada"), QStringLiteral("grace"),
                            QStringLiteral("linus"), QStringLiteral("margaret")});
    mention->insertMention(QStringLiteral("ada"), QStringLiteral("user-1"));
    mention->setFixedSize(560, 120);
    auto* mentionNote = new md::Text(QStringLiteral("type @ to mention"));
    mentionNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(mention, &md::Mention::textChanged, mentionNote,
                     [mention, mentionNote] {
                         mentionNote->setText(QStringLiteral("plain text: \"%1\"")
                                                  .arg(mention->plainText().trimmed()));
                     });

    auto* mentionColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(mentionColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(mention);
        layout->addWidget(mentionNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Mention"),
                  QStringLiteral("An inserted name is one atomic node: Backspace removes "
                                 "the whole mention and the caret cannot enter it."),
                  mentionColumn);

    page->addStretch();
    return page;
}

QWidget* feedbackPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Bars, badges and states"));
    page->addLead(QStringLiteral(
        "The chrome around content: a top app bar, a count badge that can follow any "
        "widget, and a layout that swaps between content, loading, empty, error and "
        "offline states."));

    auto* toolbar = new md::ToolBar;
    toolbar->setTitle(QStringLiteral("Inbox"));
    auto* navButton = new md::Button(QStringLiteral("Back"));
    navButton->setVariant(md::Button::ButtonVariant::Text);
    auto* sync = new md::Button(QStringLiteral("Sync"));
    sync->setVariant(md::Button::ButtonVariant::FilledTonal);
    toolbar->setNavigationIcon(navButton);
    toolbar->addAction(sync);
    bounded(toolbar, 560, 64);
    page->addCard(QStringLiteral("ToolBar"),
                  QStringLiteral("Small top app bar: a leading navigation view, a title and "
                                 "trailing actions aligned logically so RTL mirrors."),
                  toolbar);

    // A badge attached to a target widget rather than laid out next to it.
    auto* badgeHost = new QWidget;
    badgeHost->setFixedSize(260, 72);
    auto* target = new md::Button(QStringLiteral("Target"), badgeHost);
    target->resize(target->sizeHint());
    target->move(20, 12);
    auto* attached = new md::Badge(badgeHost);
    attached->setCount(120);
    attached->setMaxCount(99);
    attached->setTarget(target);

    auto* independent = new md::Badge;
    independent->setCount(7);
    natural(independent);

    auto* dot = new md::Badge;
    dot->setDot(true);
    natural(dot);

    auto* badgeRow = new QWidget;
    {
        auto* layout = new QHBoxLayout(badgeRow);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(24);
        layout->addWidget(badgeHost, 0, Qt::AlignVCenter);
        layout->addWidget(independent, 0, Qt::AlignVCenter);
        layout->addWidget(dot, 0, Qt::AlignVCenter);
        layout->addStretch(1);
    }
    page->addCard(QStringLiteral("Badge"),
                  QStringLiteral("Counts cap at \"99+\", zero hides the badge, and a dot "
                                 "variant marks presence. The left badge follows its target."),
                  badgeRow);

    // StateLayout swaps whole child views for a status.
    auto* states = new md::StateLayout;
    states->setFixedSize(560, 200);

    auto* content = new md::Text(QStringLiteral("Content is ready. Use the buttons to switch state."));
    content->setWordWrap(true);
    content->setAlignment(Qt::AlignCenter);
    states->setContentWidget(content);
    states->setPlaceholderText(md::StateLayout::State::Empty,
                               QStringLiteral("Nothing here yet"));
    states->setState(md::StateLayout::State::Content);

    auto* buttons = new QWidget;
    {
        auto* layout = new QHBoxLayout(buttons);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        const QList<QPair<QString, md::StateLayout::State>> cases{
            {QStringLiteral("Content"), md::StateLayout::State::Content},
            {QStringLiteral("Loading"), md::StateLayout::State::Loading},
            {QStringLiteral("Empty"), md::StateLayout::State::Empty},
            {QStringLiteral("Error"), md::StateLayout::State::Error},
            {QStringLiteral("Offline"), md::StateLayout::State::Offline}};
        for (const auto& entry : cases) {
            auto* button = new md::Button(entry.first);
            button->setVariant(md::Button::ButtonVariant::Outlined);
            QObject::connect(button, &md::Button::clicked, states,
                             [states, state = entry.second] { states->setState(state); });
            layout->addWidget(button);
        }
        layout->addStretch(1);
    }

    auto* stateColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(stateColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        layout->addWidget(states);
        layout->addWidget(buttons);
    }
    page->addCard(QStringLiteral("StateLayout"),
                  QStringLiteral("One visible role at a time. A role without a widget falls "
                                 "back to a painted placeholder."),
                  stateColumn);

    page->addStretch();
    return page;
}

QWidget* inputsPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Text inputs"));
    page->addLead(QStringLiteral(
        "Material's filled text field: a label that floats, a focus underline, and a "
        "supporting line that switches to the error colour. The editor itself is a "
        "QLineEdit hosted inside the painted field, so IME and selection keep working."));

    auto* plain = new md::TextInput;
    plain->setLabel(QStringLiteral("Full name"));
    plain->setHelperText(QStringLiteral("As it appears on your ID"));
    plain->setFixedSize(320, plain->sizeHint().height());

    auto* password = new md::TextInput;
    password->setLabel(QStringLiteral("Password"));
    password->setPassword(true);
    password->setClearButtonEnabled(true);
    password->setFixedSize(320, password->sizeHint().height());

    auto* email = new md::EmailInput;
    email->setLabel(QStringLiteral("Email"));
    email->setText(QStringLiteral("not-an-email"));
    email->setFixedSize(320, email->sizeHint().height());

    auto* phone = new md::TelephoneInput;
    phone->setLabel(QStringLiteral("Telephone"));
    phone->setFixedSize(320, phone->sizeHint().height());

    auto* regex = new md::RegexInput;
    regex->setLabel(QStringLiteral("Postcode"));
    regex->setPattern(QRegularExpression(QStringLiteral("^[A-Z]{2}\\d{4}$")));
    regex->setHelperText(QStringLiteral("Two letters, four digits"));
    regex->setFixedSize(320, regex->sizeHint().height());

    auto* number = new md::NumberInput;
    number->setLabel(QStringLiteral("Quantity"));
    number->setRange(0.0, 10.0);
    number->setStep(1.0);
    number->setValue(3.0);
    number->setFixedSize(320, number->sizeHint().height());
    auto* numberNote = new md::Text(QStringLiteral("value 3"));
    numberNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(number, &md::NumberInput::valueChanged, numberNote,
                     [numberNote](double value) {
                         numberNote->setText(QStringLiteral("value %1").arg(value));
                     });

    page->addCard(QStringLiteral("TextInput and derivatives"),
                  QStringLiteral("Email, telephone and regex fields show their validation "
                                 "in the error line; NumberInput also steps with Up/Down."),
                  column({row({plain, password}), row({email, phone}), row({regex, number}),
                          numberNote}));

    auto* complete = new md::Complete;
    complete->setLabel(QStringLiteral("Fruit"));
    complete->setSuggestions({QStringLiteral("Apple"), QStringLiteral("Apricot"),
                              QStringLiteral("Banana"), QStringLiteral("Blueberry"),
                              QStringLiteral("Cherry")});
    complete->setFixedSize(320, complete->sizeHint().height());
    auto* completeNote = new md::Text(QStringLiteral("type to filter"));
    completeNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(complete, &md::Complete::suggestionActivated, completeNote,
                     [completeNote](int, const QVariant& value) {
                         completeNote->setText(QStringLiteral("picked %1").arg(value.toString()));
                     });

    auto* completeColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(completeColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(complete, 0, Qt::AlignLeft);
        layout->addWidget(completeNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Complete"),
                  QStringLiteral("A text field plus a filtered suggestion list backed by an "
                                 "Adapter; Up/Down and Enter pick without leaving the field."),
                  completeColumn);

    // Form ties labels, editors and rules together.
    auto* form = new md::Form;
    auto* nameItem = new md::FormItem(QStringLiteral("Name"), new md::TextInput);
    auto* emailItem = new md::FormItem(QStringLiteral("Email"), new md::EmailInput);
    nameItem->setRules({md::FormRule::required()});
    emailItem->setRules({md::FormRule::required(),
                         md::FormRule::matches(
                             QRegularExpression(QStringLiteral("^[^\\s@]+@[^\\s@]+\\.[^\\s@]{2,}$")),
                             QStringLiteral("Enter a valid address"))});
    form->addItem(nameItem);
    form->addItem(emailItem);
    form->setFixedSize(360, 220);

    auto* result = new md::Text(QStringLiteral("not validated"));
    result->setRole(md::Text::TextRole::Caption);
    auto* validate = new md::Button(QStringLiteral("Validate"));
    QObject::connect(validate, &md::Button::clicked, form, [form, result] {
        result->setText(form->validate() ? QStringLiteral("valid")
                                         : QStringLiteral("invalid"));
    });

    auto* formColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(formColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        layout->addWidget(form, 0, Qt::AlignLeft);
        layout->addWidget(row({validate, result}));
    }
    page->addCard(QStringLiteral("Form"),
                  QStringLiteral("FormItem owns the label, the editor and the rules; "
                                 "validate() walks them and paints the first error inline."),
                  formColumn);

    page->addStretch();
    return page;
}

QWidget* pickersPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Selection and pickers"));
    page->addLead(QStringLiteral(
        "Choosing a value from a collection: a dropdown, a single-select field, a colour "
        "picker with real HSV editing, and two cyclic wheel pickers driven by adapters."));

    // DropDown over an adapter, so labels differ from the stored values.
    auto* model = new md::Model;
    model->setValues({QVariant(1), QVariant(2), QVariant(3)},
                     {QStringLiteral("One"), QStringLiteral("Two"),
                      QStringLiteral("Three")});
    auto* dropdown = new md::DropDown;
    dropdown->setSourceModel(model);
    dropdown->setCurrentIndex(1);
    dropdown->setFixedSize(240, 56);

    auto* select = new md::Select;
    select->addItems({QStringLiteral("Small"), QStringLiteral("Medium"),
                      QStringLiteral("Large")});
    select->setCurrentIndex(2);
    select->setFixedSize(240, 56);

    auto* choiceNote = new md::Text(QStringLiteral("dropdown: Two"));
    choiceNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(dropdown, &md::DropDown::currentValueChanged, choiceNote,
                     [choiceNote, dropdown](const QVariant&) {
                         choiceNote->setText(
                             QStringLiteral("dropdown: %1").arg(dropdown->currentText()));
                     });

    page->addCard(QStringLiteral("DropDown and Select"),
                  QStringLiteral("The menu is a top-level popup: Up/Down, Home/End, Enter, "
                                 "Esc and a click outside all dismiss it."),
                  row({dropdown, select}));

    auto* colour = new md::ColourPicker;
    colour->setColour(QColor(103, 80, 164));
    colour->setFixedSize(320, 300);
    auto* colourNote = new md::Text(QStringLiteral("#6750A4"));
    colourNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(colour, &md::ColourPicker::colourChanged, colourNote,
                     [colourNote](const QColor& value) {
                         colourNote->setText(value.name(QColor::HexArgb));
                     });

    auto* colourColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(colourColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(colour, 0, Qt::AlignLeft);
        layout->addWidget(colourNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("ColourPicker"),
                  QStringLiteral("Saturation/value panel, hue strip, alpha strip on a "
                                 "checkerboard, and a hex field; the hue survives greys."),
                  colourColumn);

    auto* numbers = new md::NumberPicker;
    numbers->setRange(0.0, 24.0);
    numbers->setStep(1.0);
    numbers->setCurrentValue(QVariant(8.0));
    numbers->setCyclic(true);
    numbers->setFixedSize(110, 220);
    auto* numberNote = new md::Text(QStringLiteral("8"));
    numberNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(numbers, &md::NumberPicker::valueChanged, numberNote,
                     [numberNote](double value) {
                         numberNote->setText(QStringLiteral("%1").arg(value));
                     });

    auto* months = new md::RingPicker;
    months->setValues({QVariant(QStringLiteral("January")), QVariant(QStringLiteral("February")),
                       QVariant(QStringLiteral("March")), QVariant(QStringLiteral("April")),
                       QVariant(QStringLiteral("May"))});
    months->setCyclic(true);
    months->setCurrentIndex(2);
    months->setFixedSize(190, 220);
    auto* monthNote = new md::Text(QStringLiteral("March"));
    monthNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(months, &md::PickerBase::currentValueChanged, monthNote,
                     [months, monthNote](const QVariant&) {
                         monthNote->setText(months->currentText());
                     });

    auto* pickerRow = row({numbers, months}, 28);
    auto* pickerColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(pickerColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(pickerRow);
        layout->addWidget(row({numberNote, monthNote}, 120));
    }
    page->addCard(QStringLiteral("NumberPicker and RingPicker"),
                  QStringLiteral("Both wrap cyclically and take wheel, drag and keyboard "
                                 "input. NumberPicker walks a numeric set; RingPicker takes "
                                 "any QVariant, so the month wheel is strings while the left "
                                 "wheel is numbers."),
                  pickerColumn);

    page->addStretch();
    return page;
}

QWidget* displayPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Display and navigation"));
    page->addLead(QStringLiteral(
        "Content presentation: the full M3 type scale, two model-backed lists (one "
        "virtualised), a tab bar, a banner, a marquee, an anchored popup and a guided tour."));

    auto* typeColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(typeColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        const QList<QPair<QString, md::Typography::TypeRole>> rows{
            {QStringLiteral("Display small — 36 sp"), md::Typography::TypeRole::DisplaySmall},
            {QStringLiteral("Headline small — 24 sp"), md::Typography::TypeRole::HeadlineSmall},
            {QStringLiteral("Title medium — 16 sp"), md::Typography::TypeRole::TitleMedium},
            {QStringLiteral("Body medium — 14 sp"), md::Typography::TypeRole::BodyMedium},
            {QStringLiteral("Label small — 11 sp"), md::Typography::TypeRole::LabelSmall}};
        for (const auto& entry : rows) {
            auto* sample = new md::Typography(entry.first, entry.second);
            layout->addWidget(natural(sample), 0, Qt::AlignLeft);
        }
    }
    page->addCard(QStringLiteral("Typography"),
                  QStringLiteral("Fifteen roles resolved from the theme's type scale; the "
                                 "same roles Text exposes through its smaller enum."),
                  typeColumn);

    auto* names = new QStandardItemModel;
    for (int index = 1; index <= 40; ++index) {
        names->appendRow(new QStandardItem(
            QStringLiteral("Item %1 — a row that lives in the model").arg(index)));
    }

    auto* list = new md::List;
    list->setModel(names);
    list->setRowHeight(44.0);
    list->setFixedSize(260, 240);

    auto* recycler = new md::Recycler;
    recycler->setModel(names);
    recycler->setRowHeight(44.0);
    recycler->setFixedSize(260, 240);

    auto* listNote = new md::Text(QStringLiteral("40 rows, 260 × 240"));
    listNote->setRole(md::Text::TextRole::Caption);

    auto* listColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(listColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(row({list, recycler}, 24));
        layout->addWidget(listNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("List and Recycler"),
                  QStringLiteral("Both take any QAbstractItemModel. List paints the rows it "
                                 "owns; Recycler only realises the visible rows plus a "
                                 "prefetch margin, so a long model costs the same."),
                  listColumn);

    auto* tabs = new md::TabBar;
    auto home = md::Tab(QStringLiteral("Home"));
    auto search = md::Tab(QStringLiteral("Search"));
    auto profile = md::Tab(QStringLiteral("Profile"));
    profile.badge = QStringLiteral("3");
    tabs->setTabs({home, search, profile});
    tabs->setFixedSize(560, 56);
    auto* tabNote = new md::Text(QStringLiteral("selected: Home"));
    tabNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(tabs, &md::TabBar::currentChanged, tabNote, [tabNote, tabs](int index) {
        tabNote->setText(QStringLiteral("selected: %1").arg(tabs->tabAt(index).text));
    });

    auto* tabColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(tabColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(tabs);
        layout->addWidget(tabNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("TabBar"),
                  QStringLiteral("Fixed mode shares the width evenly; the indicator slides "
                                 "to the selection over the medium1 motion token."),
                  tabColumn);

    auto* banner = new md::Banner;
    banner->setTitle(QStringLiteral("Storage almost full"));
    banner->setDescription(QStringLiteral("Free up space to keep syncing new attachments."));
    banner->setPrimaryAction(QStringLiteral("Manage"));
    banner->setSecondaryAction(QStringLiteral("Dismiss"));
    banner->setFixedSize(560, 132);
    auto* bannerNote = new md::Text(QStringLiteral("no action yet"));
    bannerNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(banner, &md::Banner::actionClicked, bannerNote,
                     [bannerNote](md::Banner::Action action) {
                         bannerNote->setText(action == md::Banner::Action::Primary
                                                 ? QStringLiteral("primary action")
                                                 : QStringLiteral("secondary action"));
                     });
    QObject::connect(banner, &md::Banner::closed, bannerNote,
                     [bannerNote] { bannerNote->setText(QStringLiteral("closed")); });

    auto* bannerColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(bannerColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(banner);
        layout->addWidget(bannerNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Banner"),
                  QStringLiteral("Icon, title, description, two text actions and a close "
                                 "button; show and hide are animated."),
                  bannerColumn);

    auto* marquee = new md::Marquee(QStringLiteral(
        "Marquee scrolls only when the text is wider than the viewport — this "
        "sentence is deliberately long enough to overflow."));
    marquee->setFixedSize(560, 40);

    auto* popupHost = new QWidget;
    popupHost->setFixedSize(560, 190);
    auto* popupAnchor = new md::Button(QStringLiteral("Open popup"), popupHost);
    popupAnchor->resize(popupAnchor->sizeHint());
    popupAnchor->move(16, 16);
    auto* popupBody = new md::Text(QStringLiteral(
        "An anchored popup: it flips when it would leave the screen, closes on Escape "
        "or an outside click."));
    popupBody->setWordWrap(true);
    auto* popup = new md::PopUp(popupHost);
    popup->setAnchor(popupAnchor);
    popup->setPlacement(md::PopUp::Placement::Below);
    popup->setContent(popupBody);
    QObject::connect(popupAnchor, &md::Button::clicked, popup, [popup] { popup->open(); });

    auto* popupColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(popupColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(marquee);
        layout->addWidget(popupHost);
    }
    page->addCard(QStringLiteral("Marquee and PopUp"),
                  QStringLiteral("The marquee pauses on hover. The popup is a child "
                                 "surface positioned from its anchor, not a separate window."),
                  popupColumn);

    auto* guideHost = new QWidget;
    guideHost->setFixedSize(560, 240);
    auto* stepOne = new md::Button(QStringLiteral("Step 1 target"), guideHost);
    stepOne->resize(stepOne->sizeHint());
    stepOne->move(48, 48);
    auto* stepTwo = new md::Button(QStringLiteral("Step 2 target"), guideHost);
    stepTwo->resize(stepTwo->sizeHint());
    stepTwo->move(300, 150);

    auto* guide = new md::GuideView(guideHost);
    guide->setGeometry(guideHost->rect());

    md::GuideStep first;
    first.target = stepOne;
    first.title = QStringLiteral("Step one");
    first.description = QStringLiteral("The target is cut out of the scrim.");
    first.highlight = md::GuideStep::Highlight::Circle;

    md::GuideStep second;
    second.target = stepTwo;
    second.title = QStringLiteral("Step two");
    second.description = QStringLiteral("Next, previous and skip buttons drive the tour.");
    second.highlight = md::GuideStep::Highlight::RoundedRect;

    guide->setSteps({first, second});
    guide->setSkipVisible(true);
    guide->start();

    auto* guideNote = new md::Text(QStringLiteral("tour running"));
    guideNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(guide, &md::GuideView::finished, guideNote,
                     [guideNote] { guideNote->setText(QStringLiteral("finished")); });
    QObject::connect(guide, &md::GuideView::skipped, guideNote,
                     [guideNote] { guideNote->setText(QStringLiteral("skipped")); });

    auto* guideColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(guideColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(guideHost);
        layout->addWidget(guideNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Guide"),
                  QStringLiteral("A scrim with a punched-through target, a bubble that "
                                 "follows the target, and keyboard-reachable buttons."),
                  guideColumn);

    // Link ---------------------------------------------------------------
    auto* linkNote = new md::Text(QStringLiteral("activate a link"));
    linkNote->setRole(md::Text::TextRole::Caption);

    const auto makeLink = [linkNote](const QString& text, const QString& href) {
        auto* link = new md::Link(text, href);
        QObject::connect(link, &md::Link::clicked, linkNote,
                         [linkNote](const QString& target) {
                             linkNote->setText(QStringLiteral("clicked: %1").arg(target));
                         });
        return link;
    };

    auto* bodyLink = makeLink(QStringLiteral("Periodicity documentation"),
                              QStringLiteral("https://example.com/docs"));
    auto* labelLink = makeLink(QStringLiteral("Label role link"),
                               QStringLiteral("https://example.com/label"));
    labelLink->setRole(md::Text::TextRole::Label);
    auto* titleLink = makeLink(QStringLiteral("Title role link"),
                               QStringLiteral("https://example.com/title"));
    titleLink->setRole(md::Text::TextRole::Title);
    auto* visitedLink = makeLink(QStringLiteral("Visited and underlined"),
                                 QStringLiteral("https://example.com/visited"));
    visitedLink->setVisited(true);
    visitedLink->setUnderline(true);
    auto* longLink = makeLink(
        QStringLiteral("A deliberately long link label that has to elide inside the "
                       "column instead of widening the card"),
        QStringLiteral("https://example.com/long"));
    longLink->setFixedWidth(320);

    auto* linkColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(linkColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(titleLink, 0, Qt::AlignLeft);
        layout->addWidget(bodyLink, 0, Qt::AlignLeft);
        layout->addWidget(labelLink, 0, Qt::AlignLeft);
        layout->addWidget(visitedLink, 0, Qt::AlignLeft);
        layout->addWidget(longLink, 0, Qt::AlignLeft);
        layout->addWidget(linkNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Link"),
                  QStringLiteral("A focusable text link: primary until visited, tertiary "
                                 "afterwards, hover deepens the role colour, Enter or Space "
                                 "reports the href and a long label elides."),
                  linkColumn);

    // Scrollbar ----------------------------------------------------------
    auto* scrollHost = new QWidget;
    scrollHost->setFixedSize(320, 184);

    auto* verticalBar = new md::Scrollbar(md::Scrollbar::Orientation::Vertical, scrollHost);
    verticalBar->setGeometry(304, 0, 14, 160);
    verticalBar->setRange(0, 600);
    verticalBar->setPageStep(80);
    verticalBar->setViewportExtent(160);
    verticalBar->setValue(180);

    auto* horizontalBar =
        new md::Scrollbar(md::Scrollbar::Orientation::Horizontal, scrollHost);
    horizontalBar->setGeometry(0, 166, 296, 14);
    horizontalBar->setRange(0, 400);
    horizontalBar->setPageStep(60);
    horizontalBar->setViewportExtent(296);
    horizontalBar->setValue(120);

    auto* scrollNote = new md::Text;
    scrollNote->setRole(md::Text::TextRole::Caption);
    const auto syncScrollNote = [verticalBar, horizontalBar, scrollNote](int) {
        scrollNote->setText(QStringLiteral("vertical %1, horizontal %2")
                                .arg(verticalBar->value())
                                .arg(horizontalBar->value()));
    };
    scrollNote->setText(QStringLiteral("vertical %1, horizontal %2")
                            .arg(verticalBar->value())
                            .arg(horizontalBar->value()));
    QObject::connect(verticalBar, &md::Scrollbar::valueChanged, scrollNote, syncScrollNote);
    QObject::connect(horizontalBar, &md::Scrollbar::valueChanged, scrollNote, syncScrollNote);

    auto* scrollColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(scrollColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(scrollHost, 0, Qt::AlignLeft);
        layout->addWidget(scrollNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Scrollbar"),
                  QStringLiteral("A standalone Material 3 scrollbar: an 8 dp rounded track, "
                                 "a thumb that widens on hover and turns primary while "
                                 "dragged, drag, track paging and the wheel."),
                  scrollColumn);

    page->addStretch();
    return page;
}

QWidget* themePage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Theme and colour attributes"));
    page->addLead(QStringLiteral(
        "Android applications configure their palette with colourPrimary, "
        "colourSecondary and colourAccent. This page shows that attribute layer on "
        "top of the Material 3 scheme, and lets a preset replace the whole palette "
        "for the gallery."));

    struct Attribute
    {
        QString name;
        QColor colour;
    };

    const std::vector<Attribute> attributes{
        {QStringLiteral("colorPrimary"), theme.colour(QStringLiteral("colorPrimary"))},
        {QStringLiteral("colorPrimaryDark"), theme.colour(QStringLiteral("colorPrimaryDark"))},
        {QStringLiteral("colorSecondary"), theme.colour(QStringLiteral("colorSecondary"))},
        {QStringLiteral("colorSecondaryVariant"), theme.colour(QStringLiteral("colorSecondaryVariant"))},
        {QStringLiteral("colorAccent"), theme.colour(QStringLiteral("colorAccent"))},
        {QStringLiteral("colorOnPrimary"), theme.colour(QStringLiteral("colorOnPrimary"))},
        {QStringLiteral("colorOnSecondary"), theme.colour(QStringLiteral("colorOnSecondary"))},
        {QStringLiteral("colorOnAccent"), theme.colour(QStringLiteral("colorOnAccent"))},
        {QStringLiteral("colorBackground"), theme.colour(QStringLiteral("colorBackground"))},
        {QStringLiteral("colorSurface"), theme.colour(QStringLiteral("colorSurface"))},
        {QStringLiteral("colorSurfaceVariant"), theme.colour(QStringLiteral("colorSurfaceVariant"))},
        {QStringLiteral("colorError"), theme.colour(QStringLiteral("colorError"))},
        {QStringLiteral("colorOnError"), theme.colour(QStringLiteral("colorOnError"))},
        {QStringLiteral("colorControlHighlight"), theme.colour(QStringLiteral("colorControlHighlight"))},
        {QStringLiteral("colorControlNormal"), theme.colour(QStringLiteral("colorControlNormal"))},
    };

    auto* table = new QWidget;
    {
        auto* layout = new QVBoxLayout(table);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        for (const Attribute& attribute : attributes) {
            auto* rowWidget = new QWidget;
            auto* rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(12);

            auto* swatch = new QFrame;
            swatch->setFixedSize(28, 28);
            swatch->setStyleSheet(
                QStringLiteral("background:%1;border:1px solid %2;border-radius:8px;")
                    .arg(attribute.colour.name(QColor::HexArgb),
                         theme.colours().outlineVariant.name()));

            auto* label = new QLabel(attribute.name);
            label->setStyleSheet(
                QStringLiteral("color:%1;font-family:Consolas,monospace;")
                    .arg(theme.colours().onSurface.name()));

            auto* value = new QLabel(attribute.colour.name(QColor::HexRgb).toUpper());
            value->setStyleSheet(
                QStringLiteral("color:%1;font-family:Consolas,monospace;")
                    .arg(theme.colours().onSurfaceVariant.name()));

            rowLayout->addWidget(swatch);
            rowLayout->addWidget(label);
            rowLayout->addStretch(1);
            rowLayout->addWidget(value);
            layout->addWidget(rowWidget);
        }
    }
    page->addCard(QStringLiteral("Attributes"),
                  QStringLiteral("Read from the live theme through Theme::colour(). "
                                 "Both spellings resolve: colourPrimary and colorPrimary, "
                                 "colourSecond and colorSecondary, colourAcent and colorAccent."),
                  table);

    auto* switcher = new md::ThemeSwitcher;
    switcher->addPreset(QStringLiteral("Baseline light"),
                        Theme::baselineLight());
    switcher->addPreset(QStringLiteral("Teal and slate"), QColor(0x00, 0x6A, 0x5B),
                        QColor(0x4A, 0x63, 0x6B), QColor(0x5B, 0x5D, 0x91));
    switcher->addPreset(QStringLiteral("Burnt orange"), QColor(0x8B, 0x50, 0x00),
                        QColor(0x77, 0x57, 0x4E), QColor(0x6C, 0x5D, 0x2F));
    switcher->addPreset(QStringLiteral("Indigo"), QColor(0x4F, 0x5B, 0x92),
                        QColor(0x5D, 0x5F, 0x71), QColor(0x7B, 0x4E, 0x77));
    switcher->addPreset(QStringLiteral("Baseline dark"),
                        Theme::baselineDark());
    switcher->setFixedSize(560, switcher->sizeHint().height());

    QObject::connect(switcher, &md::ThemeSwitcher::themeApplied, switcher,
                     [](const md::Theme& applied) {
                         if (requestTheme)
                             requestTheme(applied);
                     });

    page->addCard(QStringLiteral("ThemeSwitcher"),
                  QStringLiteral("Each row is a preset built from three seed colours. "
                                 "Clicking a row applies it to the application theme and "
                                 "rebuilds the gallery, so every page repaints from the new "
                                 "colourPrimary, colourSecondary and colourAccent."),
                  switcher);

    page->addStretch();
    return page;
}

// A small gradient used as a stand-in for real artwork in the media page.
QImage artwork(const QColor& from, const QColor& to, int width = 240, int height = 140)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    QLinearGradient gradient(0.0, 0.0, width, height);
    gradient.setColorAt(0.0, from);
    gradient.setColorAt(1.0, to);
    painter.fillRect(image.rect(), gradient);
    painter.end();
    return image;
}

QString base64Of(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}

QWidget* mediaPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Media and content"));
    page->addLead(QStringLiteral(
        "Pictures and the containers built around them: avatars from four different "
        "sources, a fitted image, cards with media and actions, chips, a skeleton "
        "placeholder and a two-pane transfer list."));

    // Avatar: initials, generated image, base64 data, icon, square variant.
    auto* initials = new md::Avatar(QStringLiteral("Ada Lovelace"));
    initials->setDiameter(56.0);
    auto* picture = new md::Avatar;
    picture->setDiameter(56.0);
    picture->setImage(artwork(QColor(0x67, 0x50, 0xA4), QColor(0xD0, 0xBC, 0xFF), 112, 112));
    auto* encoded = new md::Avatar;
    encoded->setDiameter(56.0);
    encoded->setBase64(base64Of(artwork(QColor(0x00, 0x6A, 0x5B),
                                       QColor(0x4F, 0xDA, 0xC4), 112, 112)));
    QPixmap iconPixmap(64, 64);
    iconPixmap.fill(Qt::transparent);
    {
        QPainter painter(&iconPixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.colours().onSurface);
        painter.drawEllipse(QPointF(32.0, 24.0), 12.0, 12.0);
        painter.drawRoundedRect(QRectF(12.0, 38.0, 40.0, 22.0), 11.0, 11.0);
    }
    auto* iconAvatar = new md::Avatar;
    iconAvatar->setDiameter(56.0);
    iconAvatar->setIcon(QIcon(iconPixmap));
    auto* square = new md::Avatar(QStringLiteral("AL"));
    square->setDiameter(56.0);
    square->setShapeMode(md::Avatar::ShapeMode::Square);
    square->setBorderWidth(2.0);
    square->setBorderColour(theme.colours().primary);

    page->addCard(QStringLiteral("Avatar"),
                  QStringLiteral("Initials, an image, a base64 payload and an icon all "
                                 "resolve to the same circular surface; the last one is "
                                 "square with a ring."),
                  row({initials, picture, encoded, iconAvatar, square}, 18));

    auto* cover = new md::Image;
    cover->setImage(artwork(QColor(0x00, 0x6A, 0x5B), QColor(0x4F, 0xDA, 0xC4)));
    cover->setFit(md::Image::FitMode::Cover);
    cover->setCornerRadius(16.0);
    cover->setFixedSize(240, 140);

    auto* contain = new md::Image;
    contain->setImage(artwork(QColor(0x8B, 0x50, 0x00), QColor(0xFF, 0xB9, 0x5C), 120, 160));
    contain->setFit(md::Image::FitMode::Contain);
    contain->setPlaceholderColour(theme.colours().surfaceContainerHighest);
    contain->setFixedSize(180, 140);

    page->addCard(QStringLiteral("Image"),
                  QStringLiteral("Cover crops to fill and clips to the 16 dp corner; Contain "
                                 "letterboxes the whole picture inside the placeholder."),
                  row({cover, contain}, 18));

    auto* media = new md::Image;
    media->setImage(artwork(QColor(0x67, 0x50, 0xA4), QColor(0xEF, 0xB8, 0xC8), 320, 140));
    media->setFit(md::Image::FitMode::Cover);

    auto* card = new md::Card;
    card->setVariant(md::Card::CardVariant::Elevated);
    card->setTitle(QStringLiteral("Elevated card"));
    card->setSubtitle(QStringLiteral("with media and an action"));
    card->setBody(QStringLiteral("Material cards group related content on a raised "
                                 "container. The media area is an Image child."));
    card->setMedia(media);
    auto* action = new md::Button(QStringLiteral("Open"));
    action->setVariant(md::Button::ButtonVariant::FilledTonal);
    card->addAction(action);
    card->setFixedWidth(340);
    card->setMinimumHeight(card->sizeHint().height());

    auto* outlinedCard = new md::Card;
    outlinedCard->setVariant(md::Card::CardVariant::Outlined);
    outlinedCard->setTitle(QStringLiteral("Outlined card"));
    outlinedCard->setBody(QStringLiteral("A 1 dp contour instead of a shadow, for dense "
                                         "layouts where elevation would be noise."));
    outlinedCard->setFixedWidth(280);
    outlinedCard->setMinimumHeight(outlinedCard->sizeHint().height());

    page->addCard(QStringLiteral("Card"),
                  QStringLiteral("Elevated and outlined variants; both are real containers, "
                                 "so the media and the action keep their own behaviour."),
                  row({card, outlinedCard}, 18));

    auto* filledTag = new md::Tag(QStringLiteral("Filled"));
    auto* tonalTag = new md::Tag(QStringLiteral("Tonal"));
    tonalTag->setVariant(md::Tag::TagVariant::Tonal);
    auto* outlinedTag = new md::Tag(QStringLiteral("Outlined"));
    outlinedTag->setVariant(md::Tag::TagVariant::Outlined);
    auto* selectableTag = new md::Tag(QStringLiteral("Selectable"));
    selectableTag->setSelected(true);
    auto* closableTag = new md::Tag(QStringLiteral("Closable"));
    closableTag->setClosable(true);
    auto* dottedTag = new md::Tag(QStringLiteral("Online"));
    dottedTag->setColourDot(true);
    dottedTag->setDotColour(theme.colours().primary);

    page->addCard(QStringLiteral("Tag"),
                  QStringLiteral("Filled, tonal and outlined chips; one is selected, one "
                                 "closes (its x is a separate hit target), one carries a dot."),
                  row({filledTag, tonalTag, outlinedTag, selectableTag, closableTag, dottedTag},
                      14));

    auto* line = new md::Skeleton(md::Skeleton::SkeletonShape::Text);
    line->setSkeletonWidth(320.0);
    auto* lineShort = new md::Skeleton(md::Skeleton::SkeletonShape::Text);
    lineShort->setSkeletonWidth(220.0);
    auto* circle = new md::Skeleton(md::Skeleton::SkeletonShape::Circle);
    circle->setSkeletonWidth(48.0);
    circle->setSkeletonHeight(48.0);

    auto* skeletonColumn = new QWidget;
    {
        auto* layout = new QHBoxLayout(skeletonColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(12);
        layout->addWidget(circle, 0, Qt::AlignVCenter);
        auto* lines = new QWidget;
        auto* lineLayout = new QVBoxLayout(lines);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        lineLayout->setSpacing(8);
        lineLayout->addWidget(line);
        lineLayout->addWidget(lineShort);
        layout->addWidget(lines, 0, Qt::AlignVCenter);
        layout->addStretch(1);
    }
    page->addCard(QStringLiteral("Skeleton"),
                  QStringLiteral("A shimmering placeholder while content loads; the timer "
                                 "only runs while the skeleton is visible and animating."),
                  skeletonColumn);

    auto* transfer = new md::Transfer;
    transfer->setAvailableLabel(QStringLiteral("Available"));
    transfer->setSelectedLabel(QStringLiteral("Chosen"));
    transfer->setItems({QStringLiteral("Alpha"), QStringLiteral("Bravo"),
                        QStringLiteral("Charlie"), QStringLiteral("Delta"),
                        QStringLiteral("Echo"), QStringLiteral("Foxtrot")});
    transfer->setFixedSize(560, 260);
    auto* transferNote = new md::Text(QStringLiteral("6 available / 0 chosen"));
    transferNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(transfer, &md::Transfer::itemsChanged, transferNote,
                     [transferNote](const QStringList& available, const QStringList& selected) {
                         transferNote->setText(QStringLiteral("%1 available / %2 chosen")
                                                   .arg(available.size())
                                                   .arg(selected.size()));
                     });

    auto* transferColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(transferColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(transfer);
        layout->addWidget(transferNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Transfer"),
                  QStringLiteral("Two panes and four buttons; Ctrl extends a selection, "
                                 "Shift extends a range, and double-click moves a row."),
                  transferColumn);

    page->addStretch();
    return page;
}

QWidget* timeNavPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Time and navigation"));
    page->addLead(QStringLiteral(
        "Picking a time, browsing a month and moving through content: a 24-hour and a "
        "12-hour wheel picker, a marked calendar, a carousel, page numbers, a timeline, "
        "an affix bar and an anchor list."));

    auto* clock24 = new md::TimePicker;
    clock24->setTime(QTime(8, 45, 30));
    clock24->setSecondVisible(true);
    clock24->setFixedSize(220, 220);

    auto* clock12 = new md::TimePicker;
    clock12->setHourFormat(md::TimePicker::HourFormat::Hour12);
    clock12->setTime(QTime(20, 15));
    clock12->setFixedSize(220, 220);

    auto* clockNote = new md::Text(QStringLiteral("08:45:30 and 20:15"));
    clockNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(clock24, &md::TimePicker::timeChanged, clockNote,
                     [clockNote](const QTime& value) {
                         clockNote->setText(value.toString(QStringLiteral("HH:mm:ss")));
                     });

    auto* clockColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(clockColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(row({clock24, clock12}, 24));
        layout->addWidget(clockNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("TimePicker"),
                  QStringLiteral("Cyclic columns with a selection band: wheel, drag and "
                                 "snap, arrow keys, and a 12-hour mode with a meridian."),
                  clockColumn);

    auto* calendar = new md::Calendar;
    const QDate today = QDate::currentDate();
    calendar->setSelectedDate(today);
    QList<QDate> marked;
    for (int offset = 1; offset <= 4; ++offset)
        marked.append(today.addDays(offset * 3));
    calendar->setMarkedDates(marked);
    calendar->setFixedSize(360, 400);

    auto* calendarNote = new md::Text(today.toString(Qt::ISODate));
    calendarNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(calendar, &md::Calendar::dateSelected, calendarNote,
                     [calendarNote](QDate date) {
                         calendarNote->setText(date.toString(Qt::ISODate));
                     });

    auto* calendarColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(calendarColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(calendar, 0, Qt::AlignLeft);
        layout->addWidget(calendarNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Calendar"),
                  QStringLiteral("Six weeks, Monday first; marked dates carry a dot and the "
                                 "header cycles days → months → years."),
                  calendarColumn);

    auto* carousel = new md::Carousel;
    const QList<QColor> slideColours{theme.colours().primaryContainer,
                                     theme.colours().secondaryContainer,
                                     theme.colours().tertiaryContainer};
    for (int index = 0; index < slideColours.size(); ++index) {
        auto* slide = new md::Text(QStringLiteral("Slide %1").arg(index + 1));
        slide->setAlignment(Qt::AlignCenter);
        slide->setBackgroundColour(slideColours.at(index));
        carousel->addSlide(slide);
    }
    carousel->setLoop(true);
    carousel->setFixedSize(480, 180);

    auto* carouselNote = new md::Text(QStringLiteral("slide 1 of 3"));
    carouselNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(carousel, &md::Carousel::currentChanged, carouselNote,
                     [carouselNote](int index) {
                         carouselNote->setText(QStringLiteral("slide %1").arg(index + 1));
                     });

    auto* carouselColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(carouselColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(carousel);
        layout->addWidget(carouselNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Carousel"),
                  QStringLiteral("Loops through its slides with a sliding transition; the "
                                 "indicator dots are clickable and autoplay is off here."),
                  carouselColumn);

    auto* pagination = new md::Pagination;
    pagination->setPageCount(12);
    pagination->setCurrentPage(5);
    pagination->setFixedSize(520, 56);

    auto* paginationNote = new md::Text(QStringLiteral("page 5"));
    paginationNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(pagination, &md::Pagination::pageChanged, paginationNote,
                     [paginationNote](int page) {
                         paginationNote->setText(QStringLiteral("page %1").arg(page));
                     });

    auto* paginationColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(paginationColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(pagination);
        layout->addWidget(paginationNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Pagination"),
                  QStringLiteral("First, previous, a compressed page window, next and last; "
                                 "the ellipsis keeps the row a fixed width."),
                  paginationColumn);

    auto* timeline = new md::Timeline;
    {
        struct Step
        {
            const char* time;
            const char* title;
            const char* description;
        };
        const Step steps[] = {
            {"09:00", "Standup", "What shipped yesterday, what blocks today."},
            {"11:30", "Design review", "Walk through the theme page."},
            {"15:00", "Implementation", "Land the Android colour attributes."},
            {"18:00", "Wrap up", "Summarise and queue the next batch."},
        };
        for (const Step& step : steps) {
            md::TimelineItem item;
            item.time = QString::fromUtf8(step.time);
            item.title = QString::fromUtf8(step.title);
            item.description = QString::fromUtf8(step.description);
            timeline->addItem(item);
        }
    }
    timeline->setFixedSize(380, 260);
    page->addCard(QStringLiteral("Timeline"),
                  QStringLiteral("A rail from the first marker to the last, each item with "
                                 "a time, a title and a description; scrolls when it overflows."),
                  timeline);

    auto* affixContent = new md::Text(QStringLiteral("Pinned header"));
    affixContent->setAlignment(Qt::AlignCenter);
    affixContent->setBackgroundColour(theme.colours().surfaceContainerLow);
    auto* affix = new md::Affix;
    affix->setContent(affixContent);
    affix->setThreshold(40.0);
    affix->setFixedSize(380, 64);

    auto* affixSlider = new md::SeekBar;
    affixSlider->setRange(0.0, 240.0);
    auto* affixNote = new md::Text(QStringLiteral("not stuck"));
    affixNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(affixSlider, &md::SeekBar::valueChanged, affix,
                     [affix](qreal offset) { affix->setScrollOffset(offset); });
    QObject::connect(affix, &md::Affix::stuckChanged, affixNote, [affixNote](bool stuck) {
        affixNote->setText(stuck ? QStringLiteral("stuck") : QStringLiteral("not stuck"));
    });

    auto* affixColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(affixColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(affix);
        layout->addWidget(affixSlider);
        layout->addWidget(affixNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Affix"),
                  QStringLiteral("Past its threshold the bar stops moving, raises its "
                                 "elevation and reports stuckChanged. Drag to scroll."),
                  affixColumn);

    auto* anchor = new md::Anchor;
    anchor->addAnchor(QStringLiteral("overview"), QStringLiteral("Overview"), 0.0);
    anchor->addAnchor(QStringLiteral("colour"), QStringLiteral("Colour"), 300.0);
    anchor->addAnchor(QStringLiteral("motion"), QStringLiteral("Motion"), 620.0);
    anchor->addAnchor(QStringLiteral("faq"), QStringLiteral("FAQ"), 900.0);
    anchor->setActiveAnchor(QStringLiteral("overview"));
    anchor->setFixedSize(210, 220);

    auto* anchorSlider = new md::SeekBar;
    anchorSlider->setRange(0.0, 1000.0);
    auto* anchorNote = new md::Text(QStringLiteral("active: overview"));
    anchorNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(anchorSlider, &md::SeekBar::valueChanged, anchor,
                     [anchor](qreal offset) { anchor->setScrollOffset(offset); });
    QObject::connect(anchor, &md::Anchor::activeAnchorChanged, anchorNote,
                     [anchorNote](const QString& key) {
                         anchorNote->setText(QStringLiteral("active: %1").arg(key));
                     });

    auto* anchorColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(anchorColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(anchor, 0, Qt::AlignLeft);
        layout->addWidget(anchorSlider);
        layout->addWidget(anchorNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Anchor"),
                  QStringLiteral("The highlighted anchor follows the scroll offset; clicking "
                                 "one emits its key and its offset for the host to scroll to."),
                  anchorColumn);

    page->addStretch();
    return page;
}

QWidget* overlaysPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Feedback and overlays"));
    page->addLead(QStringLiteral(
        "Menus, banners, messages and dialogs are window-internal surfaces painted by "
        "the framework, not native windows: they stay inside the page, follow its "
        "resize and close on Escape or an outside click."));

    // Menu ---------------------------------------------------------------
    auto* menuHost = new QWidget;
    menuHost->setFixedHeight(120);
    auto* menuButton = new md::Button(QStringLiteral("Open menu"), menuHost);
    menuButton->resize(menuButton->sizeHint());
    menuButton->move(16, 16);

    auto* menu = new md::Menu(menuHost);
    menu->setAnchor(menuButton);
    md::Menu::MenuItem openItem;
    openItem.text = QStringLiteral("Open");
    openItem.shortcut = QStringLiteral("Ctrl+O");
    menu->addItem(openItem);

    md::Menu::MenuItem recentItem;
    recentItem.text = QStringLiteral("Open recent");
    recentItem.submenu.push_back(md::Menu::MenuItem{QStringLiteral("theme.json")});
    recentItem.submenu.push_back(md::Menu::MenuItem{QStringLiteral("layout.json")});
    menu->addItem(recentItem);

    md::Menu::MenuItem wrapItem;
    wrapItem.text = QStringLiteral("Word wrap");
    wrapItem.checkable = true;
    wrapItem.checked = true;
    menu->addItem(wrapItem);
    menu->addSeparator();
    md::Menu::MenuItem disabledItem;
    disabledItem.text = QStringLiteral("Unavailable");
    disabledItem.enabled = false;
    menu->addItem(disabledItem);

    auto* menuNote = new md::Text(QStringLiteral("no item chosen"));
    menuNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(menuButton, &md::Button::clicked, menu, [menu] { menu->open(); });
    QObject::connect(menu, &md::Menu::itemTriggered, menuNote,
                     [menuNote](int index, const QString& text) {
                         menuNote->setText(QStringLiteral("%1: %2").arg(index).arg(text));
                     });

    page->addCard(QStringLiteral("Menu"),
                  QStringLiteral("Anchored panel with a submenu, a checkable item and a "
                                 "separator; Up/Down, Right/Left, Enter and Escape all work."),
                  menuHost);

    // Alert --------------------------------------------------------------
    auto* alerts = new QWidget;
    {
        auto* layout = new QVBoxLayout(alerts);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);

        struct Entry
        {
            md::Alert::Severity severity;
            const char* title;
            const char* text;
        };
        const Entry entries[] = {
            {md::Alert::Severity::Info, "Information", "The palette was rebuilt from three seeds."},
            {md::Alert::Severity::Success, "Saved", "Your changes are on disk."},
            {md::Alert::Severity::Warning, "Careful", "This action cannot be undone."},
            {md::Alert::Severity::Error, "Upload failed", "The connection was reset."},
        };
        for (const Entry& entry : entries) {
            auto* alert = new md::Alert;
            alert->setSeverity(entry.severity);
            alert->setTitle(QString::fromUtf8(entry.title));
            alert->setDescription(QString::fromUtf8(entry.text));
            alert->setClosable(true);
            if (entry.severity == md::Alert::Severity::Warning)
                alert->setActionText(QStringLiteral("Undo"));
            layout->addWidget(alert);
        }
    }
    page->addCard(QStringLiteral("Alert"),
                  QStringLiteral("Four inline severities; each one uses its container role "
                                 "and keeps its close cross as a separate hit target."),
                  alerts);

    // Notification -------------------------------------------------------
    auto* notificationHost = new QWidget;
    notificationHost->setFixedHeight(120);
    auto* notifyButton = new md::Button(QStringLiteral("Show message"), notificationHost);
    notifyButton->resize(notifyButton->sizeHint());
    notifyButton->move(16, 16);
    auto* notification = new md::Notification(notificationHost);
    notification->setAnchor(notifyButton);
    notification->setClosable(true);
    QObject::connect(notifyButton, &md::Button::clicked, notification, [notification] {
        notification->showMessage(QStringLiteral("Theme applied to every page"),
                                  QStringLiteral("Undo"));
    });
    auto* notifyNote = new md::Text(QStringLiteral("idle"));
    notifyNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(notification, &md::Notification::actionTriggered, notifyNote, [notifyNote] {
        notifyNote->setText(QStringLiteral("action triggered"));
    });
    QObject::connect(notification, &md::Notification::closed, notifyNote, [notifyNote] {
        notifyNote->setText(QStringLiteral("closed"));
    });

    auto* notificationColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(notificationColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(notificationHost);
        layout->addWidget(notifyNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Notification"),
                  QStringLiteral("A snackbar that queues messages, closes on its own after "
                                 "four seconds and reports its action."),
                  notificationColumn);

    // Dialog family ------------------------------------------------------
    auto* dialogHost = new QWidget;
    dialogHost->setFixedHeight(96);
    auto* dialogRow = new QHBoxLayout(dialogHost);
    dialogRow->setContentsMargins(0, 0, 0, 0);
    dialogRow->setSpacing(8);

    auto* dialogNote = new md::Text(QStringLiteral("no dialog opened"));
    dialogNote->setRole(md::Text::TextRole::Caption);

    auto addDialogButton = [dialogHost, dialogRow, page, dialogNote](const QString& label,
                                                                    auto* dialog) {
        auto* button = new md::Button(label);
        button->setVariant(md::Button::ButtonVariant::Outlined);
        button->resize(button->sizeHint());
        dialogRow->addWidget(button);
        dialog->setParent(page);
        QObject::connect(button, &md::Button::clicked, dialog, [dialog] { dialog->open(); });
        QObject::connect(dialog, &md::Dialog::accepted, dialogNote, [dialogNote, label] {
            dialogNote->setText(QStringLiteral("%1 accepted").arg(label));
        });
        QObject::connect(dialog, &md::Dialog::rejected, dialogNote, [dialogNote, label] {
            dialogNote->setText(QStringLiteral("%1 rejected").arg(label));
        });
    };

    auto* confirm = new md::ConfirmDialog(QStringLiteral("Delete preset?"),
                                          QStringLiteral("The palette will fall back to the "
                                                         "baseline colours."));
    addDialogButton(QStringLiteral("Confirm"), confirm);

    auto* message = new md::MessageDialog;
    message->setTitle(QStringLiteral("About this page"));
    message->setText(QStringLiteral("Dialogs are ViewGroups: the scrim, the surface and the "
                                    "action row are painted, the buttons are real children."));
    addDialogButton(QStringLiteral("Message"), message);

    auto* single = new md::SingleChoiceDialog(
        QStringLiteral("Density"), QStringList{QStringLiteral("Compact"),
                                               QStringLiteral("Comfortable"),
                                               QStringLiteral("Spacious")});
    addDialogButton(QStringLiteral("Single choice"), single);

    auto* multiple = new md::MultipleChoiceDialog(
        QStringLiteral("Show columns"), QStringList{QStringLiteral("Colour"),
                                                    QStringLiteral("Density"),
                                                    QStringLiteral("Motion")});
    addDialogButton(QStringLiteral("Multiple choice"), multiple);

    auto* custom = new md::CustomDialog;
    custom->setTitle(QStringLiteral("Custom content"));
    custom->setContent(new md::Text(QStringLiteral(
        "A CustomDialog takes any View as its body and any number of actions.")));
    custom->addAction(QStringLiteral("Details"), md::Dialog::ActionRole::Custom);
    custom->addAction(QStringLiteral("Close"), md::Dialog::ActionRole::Reject);
    addDialogButton(QStringLiteral("Custom"), custom);
    dialogRow->addStretch(1);

    auto* dialogColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(dialogColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(dialogHost);
        layout->addWidget(dialogNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Dialog"),
                  QStringLiteral("Confirm, Message, SingleChoice, MultipleChoice and Custom "
                                 "share one scrim and one surface; Escape rejects."),
                  dialogColumn);

    // Language -----------------------------------------------------------
    auto* language = new md::Language;
    language->setCurrentLanguage(QStringLiteral("zh-Hans"));
    auto* languageNote = new md::Text(QStringLiteral("zh-Hans"));
    languageNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(language, &md::Language::languageChanged, languageNote,
                     [languageNote](const QString& code, const QLocale&) {
                         languageNote->setText(code);
                     });

    auto* languageRow = new QWidget;
    {
        auto* layout = new QHBoxLayout(languageRow);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(16);
        layout->addWidget(language);
        layout->addWidget(languageNote, 0, Qt::AlignVCenter);
        layout->addStretch(1);
    }
    page->addCard(QStringLiteral("Language"),
                  QStringLiteral("Ten common languages; picking one updates the code and "
                                 "hands it to the host installer, which owns the .qm files."),
                  languageRow);

    // Menu rows ----------------------------------------------------------
    auto* menuRowsGroup = new md::MenuGroup;
    menuRowsGroup->setTitle(QStringLiteral("Navigation"));
    menuRowsGroup->setCollapsible(true);

    auto* menuRowLeaf = new md::MenuItem(QStringLiteral("MenuItem"));
    menuRowLeaf->setBadge(QStringLiteral("3"));
    menuRowLeaf->setShortcut(QStringLiteral("Ctrl+M"));
    menuRowsGroup->addItem(menuRowLeaf);

    auto* menuRowSubMenu = new md::SubMenu(QStringLiteral("SubMenu"));
    menuRowSubMenu->addItem(new md::MenuItem(QStringLiteral("Child row A")));
    menuRowSubMenu->addItem(new md::MenuItem(QStringLiteral("Child row B")));
    menuRowSubMenu->setExpanded(true);
    menuRowSubMenu->finishAnimation();
    menuRowsGroup->addItem(menuRowSubMenu);

    auto* menuRowSelected = new md::MenuItem(QStringLiteral("Selected row"));
    menuRowSelected->setTrailingText(QStringLiteral("on"));
    menuRowsGroup->addItem(menuRowSelected);
    menuRowsGroup->setSelectedIndex(2);
    menuRowsGroup->setFixedWidth(320);

    auto* menuRowsNote = new md::Text(QStringLiteral("hover, click or use Up/Down + Enter"));
    menuRowsNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(menuRowsGroup, &md::MenuGroup::itemActivated, menuRowsNote,
                     [menuRowsNote](int index, const QString& label) {
                         menuRowsNote->setText(
                             QStringLiteral("activated %1: %2").arg(index).arg(label));
                     });
    QObject::connect(menuRowsGroup, &md::MenuGroup::childItemActivated, menuRowsNote,
                     [menuRowsNote](int, int childIndex, const QString& label) {
                         menuRowsNote->setText(
                             QStringLiteral("child %1: %2").arg(childIndex).arg(label));
                     });

    auto* menuRowsColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(menuRowsColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(menuRowsGroup, 0, Qt::AlignLeft);
        layout->addWidget(menuRowsNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Menu rows"),
                  QStringLiteral("The navigation building blocks: a MenuGroup with a section "
                                 "title and one selection, a MenuItem with a badge and a "
                                 "shortcut, and a SubMenu that expands into indented child "
                                 "rows with an animated height."),
                  menuRowsColumn);

    page->addStretch();
    return page;
}

// A generated table with no backing storage: the virtual table only asks for
// the cells it is about to paint.
class GeneratedTableSource final : public md::structure::VirtualTableSource
{
public:
    [[nodiscard]] int rowCount() const override { return 10000; }
    [[nodiscard]] int columnCount() const override { return 4; }

    [[nodiscard]] QVariant cell(int row, int column) const override
    {
        switch (column) {
        case 0:
            return row + 1;
        case 1:
            return QStringLiteral("Row %1").arg(row + 1);
        case 2:
            return (row * 7) % 100;
        default:
            return row % 5 == 0 ? QStringLiteral("flagged") : QStringLiteral("ok");
        }
    }

    [[nodiscard]] QString header(int column) const override
    {
        switch (column) {
        case 0:
            return QStringLiteral("#");
        case 1:
            return QStringLiteral("Name");
        case 2:
            return QStringLiteral("Score");
        default:
            return QStringLiteral("Status");
        }
    }
};

QWidget* dataPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Tables and video"));
    page->addLead(QStringLiteral(
        "Two ways to show rows: a Table over a Qt item model and a VirtualTable over "
        "a generated source that is never materialised. Video is the player surface "
        "for frames a host decoder pushes or draws on demand."));

    auto* table = new md::Table;
    auto* model = new QStandardItemModel(table);
    model->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Role"),
                                      QStringLiteral("Score"), QStringLiteral("Status")});
    for (int row = 0; row < 12; ++row) {
        const QList<QStandardItem*> cells{
            new QStandardItem(QStringLiteral("Person %1").arg(row + 1)),
            new QStandardItem(row % 2 == 0 ? QStringLiteral("Designer")
                                           : QStringLiteral("Engineer")),
            new QStandardItem(QString::number(60 + (row * 7) % 40)),
            new QStandardItem(row % 4 == 0 ? QStringLiteral("away")
                                           : QStringLiteral("online")),
        };
        model->appendRow(cells);
    }
    table->setModel(model);
    table->setDefaultColumnWidth(130.0);
    table->setSelectionMode(md::Table::SelectionMode::SingleRow);
    table->setFixedSize(560, 260);

    auto* tableNote = new md::Text(QStringLiteral("cell 0, 0"));
    tableNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(table, &md::Table::currentCellChanged, tableNote,
                     [tableNote](int row, int column) {
                         tableNote->setText(QStringLiteral("cell %1, %2").arg(row).arg(column));
                     });

    auto* tableColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(tableColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(table);
        layout->addWidget(tableNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Table"),
                  QStringLiteral("Headers from the model, row separators, hover and "
                                 "selection, wheel and keyboard scrolling; only the visible "
                                 "rows are painted."),
                  tableColumn);

    auto* virtualTable = new md::VirtualTable;
    static GeneratedTableSource generated;
    virtualTable->setSource(&generated);
    virtualTable->setFrozenColumns(1);
    virtualTable->setFixedSize(560, 220);

    auto* virtualNote = new md::Text(QStringLiteral("10000 generated rows"));
    virtualNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(virtualTable, &md::VirtualTable::currentCellChanged, virtualNote,
                     [virtualNote](int row, int column) {
                         virtualNote->setText(QStringLiteral("generated cell %1, %2")
                                                  .arg(row)
                                                  .arg(column));
                     });

    auto* virtualColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(virtualColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(virtualTable);
        layout->addWidget(virtualNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("VirtualTable"),
                  QStringLiteral("Ten thousand rows, four columns, one frozen column. The "
                                 "source is asked only for the cells inside the viewport, so "
                                 "the cost does not grow with the row count."),
                  virtualColumn);

    auto* video = new md::Video;
    video->setDuration(8000);
    video->setFrameProvider([theme](qint64 position) {
        QImage image(560, 315, QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        QLinearGradient gradient(0.0, 0.0, 560.0, 315.0);
        gradient.setColorAt(0.0, theme.colours().primary);
        gradient.setColorAt(1.0, theme.colours().tertiary);
        painter.fillRect(image.rect(), gradient);

        const qreal phase = qreal(position % 4000) / 4000.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.colours().onPrimary);
        painter.drawEllipse(QPointF(80.0 + phase * 400.0, 157.0), 42.0, 42.0);

        painter.setPen(theme.colours().onPrimary);
        QFont font = painter.font();
        font.setPixelSize(44);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter,
                         QStringLiteral("%1 s").arg(position / 1000.0, 0, 'f', 1));
        painter.end();
        return image;
    });
    video->setFixedSize(560, 315);

    auto* playButton = new md::Button(QStringLiteral("Play"));
    playButton->setVariant(md::Button::ButtonVariant::FilledTonal);
    playButton->resize(playButton->sizeHint());
    QObject::connect(playButton, &md::Button::clicked, video, [video] { video->togglePlayback(); });
    QObject::connect(video, &md::Video::playbackStateChanged, playButton,
                     [playButton](md::Video::PlaybackState state) {
                         playButton->setText(state == md::Video::PlaybackState::Playing
                                                 ? QStringLiteral("Pause")
                                                 : QStringLiteral("Play"));
                     });

    auto* videoColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(videoColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(video);
        layout->addWidget(playButton, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Video"),
                  QStringLiteral("The component owns transport and painting, not decoding: "
                                 "this frame provider draws each requested position. Play, "
                                 "seek with the progress bar, or use Space and the arrows."),
                  videoColumn);

    page->addStretch();
    return page;
}

// A small virtual tree: three chapters, each with two sections. The selector
// is expected to ask only for the rows it can see.
class DemoTreeSource final : public md::structure::VirtualTreeSource
{
public:
    [[nodiscard]] int rootCount() const override { return 3; }
    [[nodiscard]] NodeId rootAt(int index) const override { return NodeId(index + 1); }

    [[nodiscard]] int childCount(NodeId parent) const override
    {
        return parent >= 1 && parent <= 3 ? 2 : 0;
    }

    [[nodiscard]] NodeId childAt(NodeId parent, int index) const override
    {
        return parent * 100 + NodeId(index + 1);
    }

    [[nodiscard]] QVariant value(NodeId node) const override
    {
        return QVariant::fromValue<qulonglong>(node);
    }

    [[nodiscard]] QString label(NodeId node) const override
    {
        if (node <= 3)
            return QStringLiteral("Chapter %1").arg(node);
        return QStringLiteral("Section %1.%2").arg(node / 100).arg(node % 100);
    }
};

QWidget* selectsPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Tree and virtual selects"));
    page->addLead(QStringLiteral(
        "A tree selector never selects a branch: a node with children only expands. "
        "The virtual variants read through the structure module's data sources, so the "
        "popup asks for the visible rows instead of materialising the data set."));

    auto* tree = new md::TreeSelect;
    auto* treeModel = new QStandardItemModel(tree);
    for (int chapter = 1; chapter <= 3; ++chapter) {
        auto* chapterItem = new QStandardItem(QStringLiteral("Chapter %1").arg(chapter));
        for (int section = 1; section <= 3; ++section) {
            chapterItem->appendRow(new QStandardItem(
                QStringLiteral("Section %1.%2").arg(chapter).arg(section)));
        }
        treeModel->appendRow(chapterItem);
    }
    tree->setModel(treeModel);
    tree->setPlaceholderText(QStringLiteral("Pick a section"));
    tree->setMaxVisibleRows(6);
    tree->setFixedSize(280, 56);

    auto* treeNote = new md::Text(QStringLiteral("no selection"));
    treeNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(tree, &md::TreeSelect::selected, treeNote,
                     [tree, treeNote](const QVariant&) {
                         treeNote->setText(QStringLiteral("selected: %1")
                                               .arg(tree->currentText()));
                     });

    auto* treeColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(treeColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(tree);
        layout->addWidget(treeNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("TreeSelect"),
                  QStringLiteral("Branches expand, leaves select; the field shows the label "
                                 "path of the chosen leaf. Driven by a QStandardItemModel "
                                 "through an Adapter."),
                  treeColumn);

    auto* virtualTree = new md::VirtualTreeSelect;
    static DemoTreeSource demoTree;
    virtualTree->setSource(&demoTree);
    virtualTree->setPlaceholderText(QStringLiteral("Pick a section"));
    virtualTree->setFixedSize(280, 56);

    auto* virtualTreeNote = new md::Text(QStringLiteral("no selection"));
    virtualTreeNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(virtualTree, &md::VirtualTreeSelect::selected, virtualTreeNote,
                     [virtualTreeNote](const QVariant& value) {
                         virtualTreeNote->setText(
                             QStringLiteral("node %1").arg(value.toString()));
                     });

    auto* virtualTreeColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(virtualTreeColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(virtualTree);
        layout->addWidget(virtualTreeNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("VirtualTreeSelect"),
                  QStringLiteral("The same branch rule over a VirtualTreeSource: node ids "
                                 "are opaque handles and children are queried lazily."),
                  virtualTreeColumn);

    auto* virtualSelect = new md::VirtualSelect;
    static md::structure::ListSource listSource;
    QVariantList values;
    values.reserve(1000);
    for (int index = 0; index < 1000; ++index)
        values.append(QStringLiteral("Item %1").arg(index + 1, 4, 10, QLatin1Char('0')));
    listSource.setValues(values);
    virtualSelect->setSource(&listSource);
    virtualSelect->setFilterable(true);
    virtualSelect->setPlaceholderText(QStringLiteral("Pick an item"));
    virtualSelect->setFixedSize(280, 56);

    auto* virtualSelectNote = new md::Text(QStringLiteral("no selection"));
    virtualSelectNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(virtualSelect, &md::VirtualSelect::currentChanged, virtualSelectNote,
                     [virtualSelectNote](int index) {
                         virtualSelectNote->setText(QStringLiteral("index %1").arg(index));
                     });

    auto* virtualSelectColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(virtualSelectColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(virtualSelect);
        layout->addWidget(virtualSelectNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("VirtualSelect"),
                  QStringLiteral("One thousand items behind a VirtualSource; the popup "
                                 "materialises only the visible window, and typing filters by "
                                 "label without walking the whole list."),
                  virtualSelectColumn);

    page->addStretch();
    return page;
}

QWidget* navigationPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Navigation and pages"));
    page->addLead(QStringLiteral(
        "A page scaffold with a header, a scrollable body and a footer; a pager that "
        "moves between child pages; and a modal navigation drawer that slides over the "
        "window with a scrim."));

    // Page scaffold ------------------------------------------------------
    auto* scaffold = new md::Page;
    scaffold->setTitle(QStringLiteral("Inbox"));
    scaffold->setSubtitle(QStringLiteral("3 unread messages"));
    scaffold->setBackVisible(true);
    auto* archive = new md::Button(QStringLiteral("Archive"));
    archive->setVariant(md::Button::ButtonVariant::Text);
    scaffold->addHeaderAction(archive);
    auto* body = new md::Text(QStringLiteral(
        "The body is a view supplied by the host. The page arranges it inside the "
        "scrollable viewport, clips it to the body rectangle and leaves the widget "
        "ownership with the caller."));
    body->setWordWrap(true);
    scaffold->setContent(body);
    auto* footer = new md::Text(QStringLiteral("Last synced just now"));
    footer->setRole(md::Text::TextRole::Caption);
    scaffold->setFooter(footer);
    scaffold->setFixedSize(560, 300);

    auto* pageNote = new md::Text(QStringLiteral("back: not requested"));
    pageNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(scaffold, &md::Page::backRequested, pageNote, [pageNote] {
        pageNote->setText(QStringLiteral("back: requested"));
    });

    auto* pageColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(pageColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(scaffold);
        layout->addWidget(pageNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Page"),
                  QStringLiteral("Header with a back affordance and an action, a scrollable "
                                 "body and a footer; the title elides and the body keeps its "
                                 "own geometry."),
                  pageColumn);

    // Pager ---------------------------------------------------------------
    auto* pager = new md::Pager;
    const QList<QColor> slideColours{theme.colours().primaryContainer,
                                     theme.colours().secondaryContainer,
                                     theme.colours().tertiaryContainer};
    for (int index = 0; index < slideColours.size(); ++index) {
        auto* slide = new md::Text(QStringLiteral("Page %1").arg(index + 1));
        slide->setAlignment(Qt::AlignCenter);
        slide->setBackgroundColour(slideColours.at(index));
        pager->addPage(slide);
    }
    pager->setLoop(true);
    pager->setFixedSize(560, 220);

    auto* previous = new md::Button(QStringLiteral("Previous"));
    previous->setVariant(md::Button::ButtonVariant::Outlined);
    auto* next = new md::Button(QStringLiteral("Next"));
    next->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* pagerNote = new md::Text(QStringLiteral("page 1 of 3"));
    pagerNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(previous, &md::Button::clicked, pager, [pager] { pager->previous(); });
    QObject::connect(next, &md::Button::clicked, pager, [pager] { pager->next(); });
    QObject::connect(pager, &md::Pager::pageChanged, pagerNote, [pagerNote](int index) {
        pagerNote->setText(QStringLiteral("page %1 of 3").arg(index + 1));
    });

    auto* pagerButtons = new QWidget;
    {
        auto* layout = new QHBoxLayout(pagerButtons);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addWidget(previous);
        layout->addWidget(next);
        layout->addWidget(pagerNote, 0, Qt::AlignVCenter);
        layout->addStretch(1);
    }

    auto* pagerColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(pagerColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(pager);
        layout->addWidget(pagerButtons, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Pager"),
                  QStringLiteral("Drag the page or use the buttons; the indicator dots "
                                 "switch pages and the transition honours reduced motion."),
                  pagerColumn);

    // Drawer --------------------------------------------------------------
    auto* drawerHost = new QWidget;
    drawerHost->setFixedHeight(120);
    auto* drawerButton = new md::Button(QStringLiteral("Open drawer"), drawerHost);
    drawerButton->resize(drawerButton->sizeHint());
    drawerButton->move(16, 16);

    auto* drawer = new md::Drawer(page);
    drawer->setHeader(QStringLiteral("Periodicity"), QStringLiteral("Material 3 components"));
    drawer->addSection(QStringLiteral("Browse"));
    drawer->addItem(QStringLiteral("Inbox"));
    drawer->addItem(QStringLiteral("Starred"));
    drawer->addItem(QStringLiteral("Drafts"), QIcon(), QStringLiteral("3"));
    drawer->addSection(QStringLiteral("Settings"));
    drawer->addItem(QStringLiteral("Preferences"));
    drawer->addItem(QStringLiteral("About"));
    QObject::connect(drawerButton, &md::Button::clicked, drawer, [drawer] { drawer->open(); });

    auto* drawerNote = new md::Text(QStringLiteral("drawer: closed"));
    drawerNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(drawer, &md::Drawer::opened, drawerNote, [drawerNote] {
        drawerNote->setText(QStringLiteral("drawer: open"));
    });
    QObject::connect(drawer, &md::Drawer::closed, drawerNote, [drawerNote] {
        drawerNote->setText(QStringLiteral("drawer: closed"));
    });
    QObject::connect(drawer, &md::Drawer::itemActivated, drawerNote,
                     [drawerNote](int, const QString& label) {
                         drawerNote->setText(QStringLiteral("chosen: %1").arg(label));
                     });

    auto* drawerColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(drawerColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(drawerHost);
        layout->addWidget(drawerNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Drawer"),
                  QStringLiteral("A modal navigation drawer with sections, badges and a "
                                 "scrim; Escape or a scrim click closes it and the selection "
                                 "is reported through itemActivated."),
                  drawerColumn);

    // NavigationView ------------------------------------------------------
    auto* navigation = new md::NavigationView;
    navigation->setHeaderTitle(QStringLiteral("Ada Lovelace"));
    navigation->setHeaderSubtitle(QStringLiteral("ada@example.com"));
    navigation->addSection(QStringLiteral("Browse"));
    navigation->addItem(QStringLiteral("Inbox"), QIcon(), QStringLiteral("3"));
    navigation->addItem(QStringLiteral("Starred"));
    navigation->addItem(QStringLiteral("Drafts"));
    navigation->addDivider();
    navigation->addSection(QStringLiteral("Settings"));
    navigation->addItem(QStringLiteral("Preferences"));
    navigation->addItem(QStringLiteral("About"));
    navigation->setSelectedIndex(1);
    navigation->setFixedSize(320, 380);

    auto* navigationNote = new md::Text(QStringLiteral("selected: Inbox"));
    navigationNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(navigation, &md::NavigationView::itemActivated, navigationNote,
                     [navigationNote](int, const QString& label) {
                         navigationNote->setText(QStringLiteral("activated: %1").arg(label));
                     });
    QObject::connect(navigation, &md::NavigationView::selectionChanged, navigationNote,
                     [navigation, navigationNote](int) {
                         navigationNote->setText(
                             QStringLiteral("selected: %1").arg(navigation->selectedLabel()));
                     });

    auto* navigationColumn = new QWidget;
    {
        auto* layout = new QVBoxLayout(navigationColumn);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(navigation, 0, Qt::AlignLeft);
        layout->addWidget(navigationNote, 0, Qt::AlignLeft);
    }
    page->addCard(QStringLiteral("Navigation"),
                  QStringLiteral("The navigation panel on its own: an account header, "
                                 "sections, dividers, icons and badges. Put it in a Drawer "
                                 "or a rail; it owns the selection and reports activation."),
                  navigationColumn);

    page->addStretch();
    return page;
}


// Foundations page ------------------------------------------------------
class FoundationTile final : public md::View
{
public:
    FoundationTile(const Theme& theme, const QString& label, const QColor& fill,
                   qreal radius, md::Elevation elevation = md::Elevation::Level0,
                   bool outline = true, QWidget* parent = nullptr)
        : md::View(parent)
        , theme_(theme)
        , label_(label)
        , fill_(fill)
        , radius_(radius)
        , outline_(outline)
    {
        setElevation(elevation);
        setFixedSize(180, 96);
    }

protected:
    void measureOverride(md::MeasureSpec, md::MeasureSpec) override
    {
        setMeasuredSize(QSizeF(width(), height()));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = content.adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setBrush(fill_);
        painter.setPen(outline_ ? QPen(theme_.colours().primary, 1.0) : Qt::NoPen);
        painter.drawRoundedRect(box, radius_, radius_);

        painter.setFont(md::Font::resolve(this, theme_.type().bodyMedium, theme_.density()));
        painter.setPen(theme_.colours().onSurface);
        painter.drawText(box, static_cast<int>(Qt::AlignCenter), label_);
        painter.restore();
    }

private:
    Theme theme_;
    QString label_;
    QColor fill_;
    qreal radius_ = 0.0;
    bool outline_ = true;
};

class FoundationDot final : public md::View
{
public:
    explicit FoundationDot(const Theme& theme, QWidget* parent = nullptr)
        : md::View(parent)
        , theme_(theme)
    {
        setFixedSize(56, 56);
    }

protected:
    void measureOverride(md::MeasureSpec, md::MeasureSpec) override
    {
        setMeasuredSize(QSizeF(width(), height()));
    }

    void paintContent(QPainter& painter, const QRectF& content) override
    {
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme_.colours().primary);
        painter.drawEllipse(content);
        painter.restore();
    }

private:
    Theme theme_;
};

QWidget* foundationsPage(const Theme& theme)
{
    auto* page = new Page(theme);
    page->addTitle(QStringLiteral("Foundations"));
    page->addLead(QStringLiteral(
        "Shape, elevation, surfaces and motion: the tokens every other page "
        "resolves against, shown on their own."));

    // Shape -------------------------------------------------------------
    auto* shapeRow = new QWidget;
    auto* shapeLayout = new QHBoxLayout(shapeRow);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    shapeLayout->setSpacing(14);

    struct ShapeEntry
    {
        const char* name;
        md::Shape shape;
    };
    const ShapeEntry shapes[] = {
        {"extraSmall", theme.shapes().extraSmall},
        {"small", theme.shapes().small},
        {"medium", theme.shapes().medium},
        {"large", theme.shapes().large},
        {"extraLarge", theme.shapes().extraLarge},
    };

    for (const ShapeEntry& entry : shapes) {
        auto* column = new QWidget;
        auto* stack = new QVBoxLayout(column);
        stack->setContentsMargins(0, 0, 0, 0);
        stack->setSpacing(8);

        auto* tile = new FoundationTile(theme, QString(), theme.colours().primaryContainer,
                                        entry.shape.radii().topStart);
        auto* label = new md::Text(QString::fromLatin1(entry.name), column);
        label->setRole(md::Text::TextRole::Caption);
        label->setAlignment(Qt::AlignHCenter);

        stack->addWidget(tile);
        stack->addWidget(label);
        shapeLayout->addWidget(column);
    }
    shapeLayout->addStretch(1);

    page->addCard(QStringLiteral("Shape"),
                  QStringLiteral("Corner radii from the shape scale, drawn with "
                                 "CornerRadii::toPath."),
                  shapeRow);

    // Elevation ---------------------------------------------------------
    auto* elevationRow = new QWidget;
    auto* elevationLayout = new QHBoxLayout(elevationRow);
    elevationLayout->setContentsMargins(0, 0, 0, 0);
    elevationLayout->setSpacing(12);

    struct ElevationEntry
    {
        const char* label;
        md::Elevation level;
        QColor colour;
    };
    const ElevationEntry elevations[] = {
        {"0 dp", md::Elevation::Level0, theme.colours().surfaceContainerLowest},
        {"1 dp", md::Elevation::Level1, theme.colours().surfaceContainerLowest},
        {"3 dp", md::Elevation::Level2, theme.colours().surfaceContainerLow},
        {"6 dp", md::Elevation::Level3, theme.colours().surfaceContainer},
        {"8 dp", md::Elevation::Level4, theme.colours().surfaceContainerHigh},
        {"12 dp", md::Elevation::Level5, theme.colours().surfaceContainerHighest},
    };

    for (const ElevationEntry& entry : elevations) {
        auto* tile = new FoundationTile(theme, QString::fromLatin1(entry.label),
                                        entry.colour, theme.shapes().small.radii().topStart,
                                        entry.level, false);
        tile->setFixedSize(160, 110);
        elevationLayout->addWidget(tile);
    }
    elevationLayout->addStretch(1);

    page->addCard(QStringLiteral("Elevation"),
                  QStringLiteral("Six elevation levels. Each surface uses "
                                 "surfaceAtElevation, so the tonal colour and the "
                                 "shadow agree."),
                  elevationRow);

    // Surfaces ----------------------------------------------------------
    auto* surfacesRow = new QWidget;
    auto* surfacesLayout = new QHBoxLayout(surfacesRow);
    surfacesLayout->setContentsMargins(0, 0, 0, 0);
    surfacesLayout->setSpacing(18);
    for (const auto& entry : {std::pair<const char*, md::Elevation>{"Level 1", md::Elevation::Level1},
                              std::pair<const char*, md::Elevation>{"Level 3", md::Elevation::Level3},
                              std::pair<const char*, md::Elevation>{"Level 5", md::Elevation::Level5}}) {
        auto* tile = new FoundationTile(theme, QString::fromLatin1(entry.first),
                                        theme.colours().surfaceContainerLow,
                                        theme.shapes().medium.radii().topStart,
                                        entry.second, true);
        tile->setFixedSize(260, 180);
        surfacesLayout->addWidget(tile);
    }
    surfacesLayout->addStretch(1);

    page->addCard(QStringLiteral("Surfaces"),
                  QStringLiteral("A surface is painted in stages: background, "
                                 "elevation shadow, content, state overlay and focus "
                                 "ring. Hover a tile to drive the state bits."),
                  surfacesRow);

    // Motion ------------------------------------------------------------
    auto* motionColumn = new QWidget;
    auto* motionStack = new QVBoxLayout(motionColumn);
    motionStack->setContentsMargins(0, 0, 0, 0);
    motionStack->setSpacing(12);

    auto* track = new QWidget;
    track->setFixedHeight(96);
    track->setStyleSheet(QStringLiteral("background:%1;border-radius:48px;")
                             .arg(theme.colours().surfaceContainerHigh.name()));

    auto* dot = new FoundationDot(theme, track);
    dot->move(20, 20);

    auto* buttons = new QWidget;
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);

    for (const int duration : {0, 250, 500, 750, 1000, 1250}) {
        auto* button = new md::Button(QStringLiteral("%1 ms").arg(duration));
        button->setVariant(md::Button::ButtonVariant::Outlined);
        QObject::connect(button, &md::Button::clicked, track,
                         [track, dot, duration] {
                             auto* animation = new QPropertyAnimation(dot, "pos", track);
                             animation->setDuration(duration);
                             animation->setEasingCurve(QEasingCurve::OutCubic);
                             animation->setStartValue(QPoint(20, 20));
                             animation->setEndValue(
                                 QPoint(track->width() - dot->width() - 20, 20));
                             animation->start(QAbstractAnimation::DeleteWhenStopped);
                         });
        buttonLayout->addWidget(button);
    }
    buttonLayout->addStretch(1);

    motionStack->addWidget(track);
    motionStack->addWidget(buttons);

    page->addCard(QStringLiteral("Motion"),
                  QStringLiteral("The dot is moved by a property animation. Each "
                                 "button runs one rung of the duration ladder — 0, "
                                 "250, 500, 750, 1000 and 1250 ms — on the standard "
                                 "curve."),
                  motionColumn);

    page->addStretch();
    return page;
}

// The pool page: one live card per facility. Nothing is composed at build
// time — a card's Ticker reads the running objects every 120 ms, so the page
// shows the pool's queue depth, the ring's fill level and the child process's
// round-trip time as they actually are.
QWidget* poolPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Pool"));
    page->addLead(QStringLiteral(
        "The pool layer, live. A four-worker pool recycles its task nodes from "
        "md::pool::ObjectPool; a lock-free SPSC ring and a closeable channel move "
        "values between threads; the IO thread owns a deadline queue; and a child "
        "process talks over two pipes and a shared-memory ring. Every figure is read "
        "from the running objects, not from a mock."));

    PoolDemo* demo = g_poolDemo;
    if (demo == nullptr) {
        page->addCard(QStringLiteral("Pool"),
                      QStringLiteral("The window did not create a PoolDemo."),
                      new QWidget);
        return page;
    }

    // ---- Thread pool ------------------------------------------------------
    auto* poolValue = new md::Text(QStringLiteral("workers —"));
    poolValue->setRole(md::Text::TextRole::Label);
    auto* poolBar = new md::Progress;
    auto* poolNote = new md::Text(QStringLiteral("drain —"));
    poolNote->setRole(md::Text::TextRole::Caption);

    auto* submit64 = new md::Button(QStringLiteral("64 jobs"));
    submit64->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* submit512 = new md::Button(QStringLiteral("512 jobs"));
    submit512->setVariant(md::Button::ButtonVariant::Outlined);
    auto* pauseButton = new md::Button(QStringLiteral("Pause"));
    pauseButton->setVariant(md::Button::ButtonVariant::Text);
    auto* resumeButton = new md::Button(QStringLiteral("Resume"));
    resumeButton->setVariant(md::Button::ButtonVariant::Text);
    auto* drainButton = new md::Button(QStringLiteral("Drain"));
    drainButton->setVariant(md::Button::ButtonVariant::Text);
    auto* restartButton = new md::Button(QStringLiteral("Restart"));
    restartButton->setVariant(md::Button::ButtonVariant::Text);

    QObject::connect(submit64, &md::Button::clicked, [demo] { demo->submitJobs(64); });
    QObject::connect(submit512, &md::Button::clicked, [demo] { demo->submitJobs(512); });
    QObject::connect(pauseButton, &md::Button::clicked, [demo] { demo->setPaused(true); });
    QObject::connect(resumeButton, &md::Button::clicked, [demo] { demo->setPaused(false); });
    QObject::connect(restartButton, &md::Button::clicked, [demo] { demo->restartPool(); });
    QObject::connect(drainButton, &md::Button::clicked, [demo, poolNote] {
        poolNote->setText(demo->drainPool() ? QStringLiteral("queue drained")
                                            : QStringLiteral("drain timed out"));
    });

    CardBody poolCard = makeCardBody([demo, poolValue, poolBar] {
        demo->poll();
        const PoolDemo::Snapshot snapshot = demo->snapshot();
        const md::pool::ThreadPool::Stats& stats = snapshot.pool;

        poolValue->setText(
            QStringLiteral("workers %1 · active %2 · queued %3 · completed %4 · rejected %5")
                .arg(static_cast<qulonglong>(stats.workers))
                .arg(static_cast<qulonglong>(stats.active))
                .arg(static_cast<qulonglong>(stats.queued))
                .arg(static_cast<qulonglong>(stats.completed))
                .arg(static_cast<qulonglong>(stats.rejected)));

        const double load = stats.workers == 0
            ? 0.0
            : static_cast<double>(stats.active) / static_cast<double>(stats.workers);
        poolBar->setValue(std::clamp(load, 0.0, 1.0));
    });
    poolCard.layout->addWidget(poolValue);
    poolCard.layout->addWidget(poolBar);
    poolCard.layout->addWidget(poolNote);
    poolCard.layout->addWidget(row({submit64, submit512, pauseButton, resumeButton,
                                    drainButton, restartButton}));
    page->addCard(QStringLiteral("Thread pool"),
                  QStringLiteral("Tasks run on four named workers. Pause keeps the queue and "
                                 "stops execution; Drain waits for it to empty; Restart stops "
                                 "and rebuilds the worker set. The bar is active/workers."),
                  poolCard.body);

    // ---- Object pool ------------------------------------------------------
    auto* taskNodes = new md::Text(QStringLiteral("task nodes —"));
    taskNodes->setRole(md::Text::TextRole::Label);
    auto* pagePool = new md::Text(QStringLiteral("page pool —"));
    pagePool->setRole(md::Text::TextRole::Label);

    auto* churnButton = new md::Button(QStringLiteral("Churn 4096 handles"));
    churnButton->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* resetPoolButton = new md::Button(QStringLiteral("Release chunks"));
    resetPoolButton->setVariant(md::Button::ButtonVariant::Outlined);

    QObject::connect(churnButton, &md::Button::clicked, [demo] { demo->churnObjectPool(4096); });
    QObject::connect(resetPoolButton, &md::Button::clicked, [demo] { demo->resetObjectPool(); });

    CardBody objectPoolCard = makeCardBody([demo, taskNodes, pagePool] {
        demo->poll();
        const PoolDemo::Snapshot snapshot = demo->snapshot();

        taskNodes->setText(
            QStringLiteral("task nodes — capacity %1 · live %2 · high-water %3 · chunks %4")
                .arg(static_cast<qulonglong>(snapshot.pool.tasks.capacity))
                .arg(static_cast<qulonglong>(snapshot.pool.tasks.live))
                .arg(static_cast<qulonglong>(snapshot.pool.tasks.highWaterMark))
                .arg(static_cast<qulonglong>(snapshot.pool.tasks.chunks)));

        pagePool->setText(
            QStringLiteral("page pool — capacity %1 · live %2 · high-water %3")
                .arg(static_cast<qulonglong>(snapshot.demoPoolCapacity))
                .arg(static_cast<qulonglong>(snapshot.demoPoolLive))
                .arg(static_cast<qulonglong>(snapshot.demoPoolHighWaterMark)));
    });
    objectPoolCard.layout->addWidget(taskNodes);
    objectPoolCard.layout->addWidget(pagePool);
    objectPoolCard.layout->addWidget(row({churnButton, resetPoolButton}));
    page->addCard(QStringLiteral("Object pool"),
                  QStringLiteral("md::pool::ObjectPool: the thread pool "
                                 "now recycles its task nodes through it. Churn creates and "
                                 "destroys 4096 handles in a loop: capacity stops growing "
                                 "because the storage is reused, not reallocated."),
                  objectPoolCard.body);

    // ---- Ring buffer ------------------------------------------------------
    auto* ringValue = new md::Text(QStringLiteral("ring —"));
    ringValue->setRole(md::Text::TextRole::Label);
    auto* ringBar = new md::Progress;

    auto* ringStart = new md::Button(QStringLiteral("Start producer/consumer"));
    ringStart->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* ringStop = new md::Button(QStringLiteral("Stop"));
    ringStop->setVariant(md::Button::ButtonVariant::Outlined);

    QObject::connect(ringStart, &md::Button::clicked, [demo] { demo->startRingStress(); });
    QObject::connect(ringStop, &md::Button::clicked, [demo] { demo->stopRingStress(); });

    CardBody ringCard = makeCardBody([demo, ringValue, ringBar] {
        demo->poll();
        const PoolDemo::Snapshot snapshot = demo->snapshot();

        ringValue->setText(QStringLiteral("%1 / %2 slots · produced %3 · consumed %4 · "
                                          "dropped %5 · %6/s")
                               .arg(static_cast<qulonglong>(snapshot.ringUsed))
                               .arg(static_cast<qulonglong>(snapshot.ringCapacity))
                               .arg(static_cast<qulonglong>(snapshot.ringProduced))
                               .arg(static_cast<qulonglong>(snapshot.ringConsumed))
                               .arg(static_cast<qulonglong>(snapshot.ringDropped))
                               .arg(snapshot.ringPerSecond, 0, 'f', 0));

        const double fill = snapshot.ringCapacity == 0
            ? 0.0
            : static_cast<double>(snapshot.ringUsed) / static_cast<double>(snapshot.ringCapacity);
        ringBar->setValue(std::clamp(fill, 0.0, 1.0));
    });
    ringCard.layout->addWidget(ringValue);
    ringCard.layout->addWidget(ringBar);
    ringCard.layout->addWidget(row({ringStart, ringStop}));
    page->addCard(QStringLiteral("Ring buffer"),
                  QStringLiteral("One producer and one consumer share an 8192-slot "
                                 "SpscRingBuffer: no mutex, one cache line per cursor. The "
                                 "producer throttles after every 64 pushes so the page stays "
                                 "responsive; a full ring rejects and counts, never overwrites."),
                  ringCard.body);

    // ---- IO thread --------------------------------------------------------
    auto* ioValue = new md::Text(QStringLiteral("io —"));
    ioValue->setRole(md::Text::TextRole::Label);
    auto* ioStatus = new md::Text(QStringLiteral("idle"));
    ioStatus->setRole(md::Text::TextRole::Caption);

    auto* ioPost = new md::Button(QStringLiteral("Post task"));
    ioPost->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* ioDelay = new md::Button(QStringLiteral("Post after 250 ms"));
    ioDelay->setVariant(md::Button::ButtonVariant::Outlined);
    auto* ioRead = new md::Button(QStringLiteral("Read CMakeLists.txt"));
    ioRead->setVariant(md::Button::ButtonVariant::Outlined);
    auto* ioCancel = new md::Button(QStringLiteral("Cancel timer"));
    ioCancel->setVariant(md::Button::ButtonVariant::Text);

    QObject::connect(ioPost, &md::Button::clicked, [demo] { demo->postIoTask(); });
    QObject::connect(ioDelay, &md::Button::clicked, [demo] { demo->postIoDelayed(); });
    QObject::connect(ioRead, &md::Button::clicked, [demo] { demo->readProjectFile(); });
    QObject::connect(ioCancel, &md::Button::clicked, [demo] { demo->cancelIoTimers(); });

    CardBody ioCard = makeCardBody([demo, ioValue, ioStatus] {
        demo->poll();
        const PoolDemo::Snapshot snapshot = demo->snapshot();

        ioValue->setText(QStringLiteral("executed %1 · pending %2 · avg %3 µs · max %4 µs")
                             .arg(static_cast<qulonglong>(snapshot.io.executed))
                             .arg(static_cast<qulonglong>(snapshot.io.pending))
                             .arg(double(snapshot.io.avgLatencyUs), 0, 'f', 1)
                             .arg(double(snapshot.io.maxLatencyUs), 0, 'f', 1));
        ioStatus->setText(snapshot.ioFileStatus.isEmpty() ? QStringLiteral("idle")
                                                          : snapshot.ioFileStatus);
    });
    ioCard.layout->addWidget(ioValue);
    ioCard.layout->addWidget(ioStatus);
    ioCard.layout->addWidget(row({ioPost, ioDelay, ioRead, ioCancel}));
    page->addCard(QStringLiteral("IO thread"),
                  QStringLiteral("One thread whose clock is deadlines, not frames. A delayed "
                                 "task is cancelled before it fires; the file read runs here "
                                 "and reports its size through the mailbox."),
                  ioCard.body);

    // ---- Thread communication --------------------------------------------
    auto* mailboxValue = new md::Text(QStringLiteral("mailbox —"));
    mailboxValue->setRole(md::Text::TextRole::Label);
    auto* channelValue = new md::Text(QStringLiteral("channel —"));
    channelValue->setRole(md::Text::TextRole::Label);

    auto* pingOnce = new md::Button(QStringLiteral("Ping worker"));
    pingOnce->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* pingBurst = new md::Button(QStringLiteral("Ping ×64"));
    pingBurst->setVariant(md::Button::ButtonVariant::Outlined);
    auto* channelStart = new md::Button(QStringLiteral("Start channel"));
    channelStart->setVariant(md::Button::ButtonVariant::Outlined);
    auto* channelStop = new md::Button(QStringLiteral("Stop channel"));
    channelStop->setVariant(md::Button::ButtonVariant::Text);

    QObject::connect(pingOnce, &md::Button::clicked, [demo] { demo->pingWorker(1); });
    QObject::connect(pingBurst, &md::Button::clicked, [demo] { demo->pingWorker(64); });
    QObject::connect(channelStart, &md::Button::clicked, [demo] { demo->startChannelDemo(); });
    QObject::connect(channelStop, &md::Button::clicked, [demo] { demo->stopChannelDemo(); });

    CardBody commCard = makeCardBody([demo, mailboxValue, channelValue] {
        demo->poll();
        const PoolDemo::Snapshot snapshot = demo->snapshot();

        mailboxValue->setText(
            QStringLiteral("mailbox — delivered %1 · pending %2 · replies %3/%4 · last RTT %5 µs")
                .arg(static_cast<qulonglong>(snapshot.mailboxDelivered))
                .arg(static_cast<qulonglong>(snapshot.mailboxPending))
                .arg(static_cast<qulonglong>(snapshot.pingReplies))
                .arg(static_cast<qulonglong>(snapshot.pingRequests))
                .arg(snapshot.lastWorkerRttUs, 0, 'f', 1));

        channelValue->setText(QStringLiteral("channel — produced %1 · consumed %2 · %3/s")
                                  .arg(static_cast<qulonglong>(snapshot.channelProduced))
                                  .arg(static_cast<qulonglong>(snapshot.channelConsumed))
                                  .arg(snapshot.channelPerSecond, 0, 'f', 0));
    });
    commCard.layout->addWidget(mailboxValue);
    commCard.layout->addWidget(channelValue);
    commCard.layout->addWidget(row({pingOnce, pingBurst, channelStart, channelStop}));
    page->addCard(QStringLiteral("Thread communication"),
                  QStringLiteral("A worker computes, then posts one closure to the UI-side "
                                 "Mailbox: the whole burst costs one event-loop wake-up and "
                                 "every closure runs in post order. The channel is the "
                                 "multi-producer counterpart, closed with one call."),
                  commCard.body);

    // ---- Process communication -------------------------------------------
    auto* childValue = new md::Text(QStringLiteral("child —"));
    childValue->setRole(md::Text::TextRole::Label);
    auto* childRingValue = new md::Text(QStringLiteral("shared ring —"));
    childRingValue->setRole(md::Text::TextRole::Label);
    auto* childLog = new QLabel(QStringLiteral("(no child yet)"));
    childLog->setWordWrap(true);
    childLog->setFont(QFont(QStringLiteral("Consolas"), 9));
    childLog->setStyleSheet(
        QStringLiteral("color:%1;").arg(theme.colours().onSurfaceVariant.name()));

    auto* childStart = new md::Button(QStringLiteral("Start child"));
    childStart->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* childPing = new md::Button(QStringLiteral("Send ping"));
    childPing->setVariant(md::Button::ButtonVariant::Outlined);
    auto* childStop = new md::Button(QStringLiteral("Stop child"));
    childStop->setVariant(md::Button::ButtonVariant::Text);

    QObject::connect(childStart, &md::Button::clicked, [demo] { demo->startChild(); });
    QObject::connect(childPing, &md::Button::clicked, [demo] { demo->sendPing(); });
    QObject::connect(childStop, &md::Button::clicked, [demo] { demo->stopChild(); });

    CardBody ipcCard = makeCardBody([demo, childValue, childRingValue, childLog] {
        demo->poll();
        const PoolDemo::Snapshot snapshot = demo->snapshot();

        childValue->setText(
            QStringLiteral("child — %1 · pid %2 · tx %3 · rx %4 · last RTT %5 µs")
                .arg(snapshot.childRunning ? QStringLiteral("running")
                                           : QStringLiteral("stopped"))
                .arg(snapshot.childPid)
                .arg(static_cast<qulonglong>(snapshot.childTx))
                .arg(static_cast<qulonglong>(snapshot.childRx))
                .arg(snapshot.childRttUs, 0, 'f', 1));

        childRingValue->setText(
            QStringLiteral("shared ring — %1 frames · %2/s · capacity 1 MiB")
                .arg(static_cast<qulonglong>(snapshot.childShmFrames))
                .arg(snapshot.childShmPerSecond, 0, 'f', 0));

        childLog->setText(snapshot.log.isEmpty() ? QStringLiteral("(no child yet)")
                                                 : snapshot.log.join(QChar('\n')));
    });
    ipcCard.layout->addWidget(childValue);
    ipcCard.layout->addWidget(childRingValue);
    ipcCard.layout->addWidget(row({childStart, childPing, childStop}));
    ipcCard.layout->addWidget(childLog);
    page->addCard(QStringLiteral("Process communication"),
                  QStringLiteral("periodicity_ipc_echo is spawned with only its two pipe ends "
                                 "inherited. PING/PONG crosses the pipe; the child also streams "
                                 "TICK frames into a named 1 MiB shared ring, which the page "
                                 "drains without a single copy through the kernel."),
                  ipcCard.body);

    page->addStretch();
    return page;
}

// The first wave from docs/component-gaps.md: the controls a real product
// screen needs before the catalogue is usable end to end, in one place so the
// visual regression baseline has a single page to review.
QWidget* additionsPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Additions"));
    page->addLead(QStringLiteral(
        "The P0 gaps from the component report: Switch and Divider for settings, "
        "IconButton and FAB for actions, the four chip variants, SearchBar, a "
        "bottom NavigationBar and a modal BottomSheet. Everything is keyboard "
        "reachable and painted through the same View pipeline as the rest."));

    const auto standardIcon = [page](QStyle::StandardPixmap pixmap) {
        return page->style()->standardIcon(pixmap);
    };

    // ---- Switch and divider ---------------------------------------------
    auto* wifi = new md::Switch;
    wifi->setChecked(true);
    auto* bluetooth = new md::Switch;
    auto* bluetoothNote = new md::Text(QStringLiteral("Bluetooth: off"));
    bluetoothNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(bluetooth, &md::Switch::toggled, bluetoothNote, [bluetoothNote](bool on) {
        bluetoothNote->setText(on ? QStringLiteral("Bluetooth: on")
                                  : QStringLiteral("Bluetooth: off"));
    });

    auto* settingsColumn = new QWidget;
    auto* settingsLayout = new QVBoxLayout(settingsColumn);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);
    settingsLayout->addWidget(row({new md::Text(QStringLiteral("Wi-Fi")), wifi}));
    settingsLayout->addWidget(new md::Divider);
    settingsLayout->addWidget(row({new md::Text(QStringLiteral("Bluetooth")), bluetooth}));
    settingsLayout->addWidget(new md::Divider);
    settingsLayout->addWidget(bluetoothNote);
    page->addCard(QStringLiteral("Switch and divider"),
                  QStringLiteral("A 52x48 dp switch whose thumb slides over the short4 "
                                 "motion token (and snaps under reduced motion), above a "
                                 "1 dp outlineVariant divider. Space toggles the switch."),
                  settingsColumn);

    // ---- Actions: icon buttons and FABs ----------------------------------
    auto* standardButton = new md::IconButton(standardIcon(QStyle::SP_FileIcon));
    auto* filledButton = new md::IconButton(standardIcon(QStyle::SP_DialogSaveButton));
    filledButton->setVariant(md::IconButton::Variant::Filled);
    auto* tonalButton = new md::IconButton(standardIcon(QStyle::SP_MediaPlay));
    tonalButton->setVariant(md::IconButton::Variant::Tonal);
    auto* outlinedButton = new md::IconButton(standardIcon(QStyle::SP_TrashIcon));
    outlinedButton->setVariant(md::IconButton::Variant::Outlined);
    auto* toggleButton = new md::IconButton(standardIcon(QStyle::SP_BrowserReload));
    toggleButton->setCheckable(true);

    auto* smallFab = new md::Fab(standardIcon(QStyle::SP_FileIcon));
    smallFab->setSize(md::Fab::Size::Small);
    auto* regularFab = new md::Fab(standardIcon(QStyle::SP_DialogSaveButton));
    auto* largeFab = new md::Fab(standardIcon(QStyle::SP_MediaPlay));
    largeFab->setSize(md::Fab::Size::Large);
    auto* extendedFab = new md::Fab(standardIcon(QStyle::SP_FileIcon));
    extendedFab->setExtended(true);
    extendedFab->setText(QStringLiteral("Compose"));

    auto* actionNote = new md::Text(QStringLiteral("last action: none"));
    actionNote->setRole(md::Text::TextRole::Caption);

    const auto report = [actionNote](const QString& what) {
        actionNote->setText(QStringLiteral("last action: %1").arg(what));
    };
    QObject::connect(standardButton, &md::IconButton::clicked, actionNote,
                     [report] { report(QStringLiteral("standard icon button")); });
    QObject::connect(filledButton, &md::IconButton::clicked, actionNote,
                     [report] { report(QStringLiteral("filled icon button")); });
    QObject::connect(tonalButton, &md::IconButton::clicked, actionNote,
                     [report] { report(QStringLiteral("tonal icon button")); });
    QObject::connect(outlinedButton, &md::IconButton::clicked, actionNote,
                     [report] { report(QStringLiteral("outlined icon button")); });
    QObject::connect(toggleButton, &md::IconButton::toggled, actionNote,
                     [report](bool on) { report(on ? QStringLiteral("toggle on")
                                                   : QStringLiteral("toggle off")); });
    for (md::Fab* fab : {smallFab, regularFab, largeFab, extendedFab}) {
        QObject::connect(fab, &md::Fab::clicked, actionNote,
                         [report, fab] { report(fab->isExtended() ? QStringLiteral("extended FAB")
                                                                  : QStringLiteral("FAB")); });
    }

    auto* actionColumn = new QWidget;
    auto* actionLayout = new QVBoxLayout(actionColumn);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(12);
    actionLayout->addWidget(row({standardButton, filledButton, tonalButton,
                                 outlinedButton, toggleButton}));
    actionLayout->addWidget(row({smallFab, regularFab, largeFab, extendedFab}));
    actionLayout->addWidget(actionNote);
    page->addCard(QStringLiteral("Actions"),
                  QStringLiteral("IconButton in all four variants, one checkable, and the "
                                 "FAB at 40, 56 and 96 dp plus the extended pill."),
                  actionColumn);

    // ---- Chips -----------------------------------------------------------
    auto* assistChip = new md::Chip(QStringLiteral("Assist"));
    assistChip->setVariant(md::Chip::Variant::Assist);
    assistChip->setLeadingIcon(standardIcon(QStyle::SP_DialogOpenButton));
    auto* filterChip = new md::Chip(QStringLiteral("Filter"));
    filterChip->setVariant(md::Chip::Variant::Filter);
    filterChip->setSelected(true);
    auto* inputChip = new md::Chip(QStringLiteral("Input"));
    inputChip->setVariant(md::Chip::Variant::Input);
    inputChip->setClosable(true);
    auto* suggestionChip = new md::Chip(QStringLiteral("Suggestion"));
    suggestionChip->setVariant(md::Chip::Variant::Suggestion);

    auto* chipNote = new md::Text(QStringLiteral("Filter is selected; Input is closable"));
    chipNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(filterChip, &md::Chip::selectedChanged, chipNote, [chipNote](bool selected) {
        chipNote->setText(selected ? QStringLiteral("Filter selected")
                                   : QStringLiteral("Filter cleared"));
    });
    QObject::connect(inputChip, &md::Chip::closed, chipNote,
                     [chipNote] { chipNote->setText(QStringLiteral("Input chip closed")); });

    auto* chipColumn = new QWidget;
    auto* chipLayout = new QVBoxLayout(chipColumn);
    chipLayout->setContentsMargins(0, 0, 0, 0);
    chipLayout->setSpacing(12);
    chipLayout->addWidget(row({assistChip, filterChip, inputChip, suggestionChip}));
    chipLayout->addWidget(chipNote);
    page->addCard(QStringLiteral("Chips"),
                  QStringLiteral("Assist, filter (selected, leading check), input (24 dp "
                                 "close target) and suggestion. Assist and suggestion are "
                                 "stateless by design."),
                  chipColumn);

    // ---- Search ----------------------------------------------------------
    auto* search = new md::SearchBar;
    search->setPlaceholder(QStringLiteral("Search components"));
    search->setSuggestions({QStringLiteral("Button"), QStringLiteral("Card"),
                            QStringLiteral("Chip"), QStringLiteral("Dialog"),
                            QStringLiteral("Divider"), QStringLiteral("FAB"),
                            QStringLiteral("Switch")});
    auto* searchNote = new md::Text(QStringLiteral("type to filter, Return to submit"));
    searchNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(search, &md::SearchBar::submitted, searchNote, [searchNote](const QString& text) {
        searchNote->setText(QStringLiteral("submitted: %1").arg(text));
    });
    QObject::connect(search, &md::SearchBar::suggestionActivated, searchNote,
                     [searchNote](const QString& text) {
                         searchNote->setText(QStringLiteral("suggestion: %1").arg(text));
                     });

    auto* searchColumn = new QWidget;
    auto* searchLayout = new QVBoxLayout(searchColumn);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(12);
    searchLayout->addWidget(search);
    searchLayout->addWidget(searchNote);
    page->addCard(QStringLiteral("Search"),
                  QStringLiteral("A 56 dp pill with a Qt-backed line edit, a 48 dp clear "
                                 "target and an anchored suggestion list (Up/Down + Return)."),
                  searchColumn);

    // ---- Navigation bar --------------------------------------------------
    auto* navigation = new md::NavigationBar;
    navigation->addDestination({standardIcon(QStyle::SP_FileIcon), QIcon(),
                                QStringLiteral("Home"), QString()});
    navigation->addDestination({standardIcon(QStyle::SP_DirIcon), QIcon(),
                                QStringLiteral("Files"), QStringLiteral("3")});
    navigation->addDestination({standardIcon(QStyle::SP_MediaPlay), QIcon(),
                                QStringLiteral("Media"), QString()});
    navigation->addDestination({standardIcon(QStyle::SP_DialogSaveButton), QIcon(),
                                QStringLiteral("Saved"), QString()});
    auto* navigationNote = new md::Text(QStringLiteral("Home"));
    navigationNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(navigation, &md::NavigationBar::destinationActivated, navigationNote,
                     [navigation, navigationNote](int index) {
                         navigationNote->setText(
                             QStringLiteral("%1 activated").arg(navigation->destinationAt(index).label));
                     });

    auto* navigationColumn = new QWidget;
    auto* navigationLayout = new QVBoxLayout(navigationColumn);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(12);
    navigationLayout->addWidget(navigation);
    navigationLayout->addWidget(navigationNote);
    page->addCard(QStringLiteral("Navigation bar"),
                  QStringLiteral("80 dp bar with an active 32 dp indicator pill, a badge and "
                                 "arrow keys plus Home/End; RTL mirrors the order."),
                  navigationColumn);

    // ---- Bottom sheet ----------------------------------------------------
    auto* openSheet = new md::Button(QStringLiteral("Open bottom sheet"));
    openSheet->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* sheetNote = new md::Text(QStringLiteral("the sheet opens over a 32% scrim"));
    sheetNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(openSheet, &md::Button::clicked, page, [page, sheetNote] {
        auto* sheet = new md::BottomSheet(page->window());
        sheet->setTitle(QStringLiteral("Filters"));

        auto* content = new QWidget;
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(12);
        auto* text = new QLabel(
            QStringLiteral("A modal sheet: the scrim, the focus trap and the Escape key "
                           "come from the Modal layer. Drag the handle down past a quarter "
                           "of the height (or press Escape) to dismiss."));
        text->setWordWrap(true);
        contentLayout->addWidget(text);
        auto* close = new md::Button(QStringLiteral("Done"));
        close->setVariant(md::Button::ButtonVariant::Filled);
        contentLayout->addWidget(close, 0, Qt::AlignLeft);

        sheet->setContent(content);
        QObject::connect(close, &md::Button::clicked, sheet, [sheet] { sheet->close(); });
        QObject::connect(sheet, &md::BottomSheet::closed, sheet, &QObject::deleteLater);
        QObject::connect(sheet, &md::BottomSheet::closed, sheetNote, [sheetNote] {
            sheetNote->setText(QStringLiteral("sheet closed"));
        });

        sheet->open();
        sheetNote->setText(QStringLiteral("sheet open"));
    });

    auto* sheetColumn = new QWidget;
    auto* sheetLayout = new QVBoxLayout(sheetColumn);
    sheetLayout->setContentsMargins(0, 0, 0, 0);
    sheetLayout->setSpacing(12);
    sheetLayout->addWidget(openSheet, 0, Qt::AlignLeft);
    sheetLayout->addWidget(sheetNote);
    page->addCard(QStringLiteral("Bottom sheet"),
                  QStringLiteral("A bottom-anchored surface with a drag handle, a 28 dp top "
                                 "corner and a scrollable body, registered with the Modal "
                                 "layer just below dialogs."),
                  sheetColumn);

    page->addStretch();
    return page;
}

// The charts page: one card for the Canvas, one for the coordinate transform,
// one per chart, then the Clock, Schedule and Scheduler. Every sample is a
// table literal, so the page paints the same image on every run; the Canvas
// keeps one scene per context and the Scheduler owns a queue instead of
// borrowing TaskQueue::main().
QWidget* chartsPage(const Theme& theme)
{
    auto* page = new Page(theme);

    page->addTitle(QStringLiteral("Charts"));
    page->addLead(QStringLiteral(
        "The visualisation stack: a Canvas that switches between the three drawing "
        "contexts, the shared MathematicsCoordination transform, and one card per chart — "
        "line, scatter, pie, bar, histogram, box, heatmap, radar and funnel. The Clock, "
        "Schedule and Scheduler close the page with the new time components."));

    const QSize chartSize(qRound(theme.density().dp(320.0)),
                          qRound(theme.density().dp(200.0)));
    const auto fit = [chartSize](QWidget* widget) { widget->setFixedSize(chartSize); };

    // The cards share two data builders: `values` turns a table of samples into
    // x = 1..n points and `series` names them. Nothing here is random, so two
    // runs produce the same charts.
    const auto values = [](std::initializer_list<qreal> samples) {
        QVector<QPointF> points;
        qreal x = 1.0;
        for (const qreal sample : samples)
            points.append(QPointF(x++, sample));
        return points;
    };
    const auto series = [](const QString& name, const QVector<QPointF>& points) {
        md::ChartSeries result;
        result.name = name;
        result.points = points;
        return result;
    };

    // ---- Canvas ----------------------------------------------------------
    auto* canvas = new md::Canvas;
    canvas->setFixedSize(chartSize);
    canvas->setBackground(theme.colours().surfaceContainerLowest);

    canvas->context1d()->setRange(0.0, 100.0);
    canvas->context1d()->setValues(
        {18.0, 34.0, 29.0, 47.0, 42.0, 63.0, 58.0, 74.0});
    canvas->context1d()->setTicks(4);

    md::Context2D* two = canvas->context2d();
    two->setTransform(theme.density().dp(1.0), theme.density().dp(1.0));
    two->setPen(theme.colours().primary);
    two->setBrush(theme.colours().primaryContainer);
    two->drawRoundedRect(QRectF(20.0, 20.0, 128.0, 72.0), 12.0, 12.0);
    two->setBrush(theme.colours().secondaryContainer);
    two->drawEllipse(QPointF(96.0, 144.0), 52.0, 32.0);
    two->setBrush(theme.colours().tertiaryContainer);
    two->drawRect(QRectF(190.0, 20.0, 44.0, 44.0));
    two->setPen(theme.colours().secondary);
    two->drawLine(QPointF(184.0, 112.0), QPointF(184.0, 176.0));
    two->drawLine(QPointF(184.0, 176.0), QPointF(300.0, 176.0));
    two->setPen(theme.colours().tertiary);
    two->drawArc(QRectF(206.0, 92.0, 82.0, 82.0), 30 * 16, 240 * 16);
    two->setPen(theme.colours().onSurfaceVariant);
    two->drawText(QRectF(188.0, 70.0, 120.0, 18.0),
                  static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter),
                  QStringLiteral("context2d()"));

    md::Context3D* three = canvas->context3d();
    // The gallery always shows the software path: no OpenGL backend is
    // requested, so the page also works on a machine without a GL context.
    three->setBackend(md::Context3D::Backend::Software);
    three->setCamera(QVector3D(2.6f, 2.0f, 4.4f), QVector3D(0.0f, 0.0f, 0.0f));
    three->perspective(45.0, 0.1, 100.0);
    three->setLineWidth(1.5);
    const QVector3D cube[8] = {
        QVector3D(-1.0f, -1.0f, -1.0f), QVector3D(1.0f, -1.0f, -1.0f),
        QVector3D(1.0f, 1.0f, -1.0f),   QVector3D(-1.0f, 1.0f, -1.0f),
        QVector3D(-1.0f, -1.0f, 1.0f),  QVector3D(1.0f, -1.0f, 1.0f),
        QVector3D(1.0f, 1.0f, 1.0f),    QVector3D(-1.0f, 1.0f, 1.0f),
    };
    const int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    for (const auto& edge : edges)
        three->line3D(cube[edge[0]], cube[edge[1]], theme.colours().primary);
    three->points3D(QVector<QVector3D>(cube, cube + 8), theme.colours().tertiary);
    canvas->requestDraw();

    auto* mode = new md::DropDown;
    mode->addItems({QStringLiteral("OneD"), QStringLiteral("TwoD"),
                    QStringLiteral("ThreeD")});
    mode->setCurrentIndex(0);
    mode->setFixedSize(qRound(theme.density().dp(160.0)),
                       qRound(theme.density().dp(56.0)));

    auto* canvasNote = new md::Text(
        QStringLiteral("OneD: setValues() — the scalar strip"));
    canvasNote->setRole(md::Text::TextRole::Caption);
    QObject::connect(mode, &md::DropDown::currentIndexChanged, canvas,
                     [canvas, canvasNote](int index) {
                         switch (index) {
                         case 1:
                             canvas->setMode(md::Canvas::Mode::TwoD);
                             canvasNote->setText(QStringLiteral(
                                 "TwoD: retained rectangles, ellipses, lines, an arc and text"));
                             break;
                         case 2:
                             canvas->setMode(md::Canvas::Mode::ThreeD);
                             canvasNote->setText(QStringLiteral(
                                 "ThreeD: a software-projected cube — no OpenGL requested"));
                             break;
                         default:
                             canvas->setMode(md::Canvas::Mode::OneD);
                             canvasNote->setText(QStringLiteral(
                                 "OneD: setValues() — the scalar strip"));
                             break;
                         }
                     });

    auto* canvasColumn = new QWidget;
    auto* canvasLayout = new QVBoxLayout(canvasColumn);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(12);
    canvasLayout->addWidget(mode, 0, Qt::AlignLeft);
    canvasLayout->addWidget(canvasNote, 0, Qt::AlignLeft);
    canvasLayout->addWidget(canvas, 0, Qt::AlignLeft);
    page->addCard(QStringLiteral("Canvas"),
                  QStringLiteral("One View, three retained contexts: the menu flips OneD, "
                                 "TwoD and ThreeD, and switching modes preserves each scene."),
                  canvasColumn);

    // ---- Coordinate fit --------------------------------------------------
    auto* coordination = new md::MathematicsCoordination;
    coordination->setDataBounds(QPointF(0.0, 0.0), QPointF(12.0, 80.0));
    coordination->setXAxisLabel(QStringLiteral("sample"));
    coordination->setYAxisLabel(QStringLiteral("value"));
    coordination->setTickCount(6);
    fit(coordination);

    auto* zoom = new md::Button(QStringLiteral("Zoom 1.25x"));
    zoom->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* pan = new md::Button(QStringLiteral("Pan +24 px"));
    pan->setVariant(md::Button::ButtonVariant::Outlined);
    QObject::connect(zoom, &md::Button::clicked, coordination, [coordination] {
        coordination->zoomAt(coordination->plotArea().center(), 1.25);
    });
    QObject::connect(pan, &md::Button::clicked, coordination, [coordination] {
        coordination->pan(QPointF(24.0, 0.0));
    });

    auto* gestureNote = new md::Text(QStringLiteral(
        "Gesture: pinch maps to zoomAt(centre, 1.25), drag maps to pan(+24 px); both stop "
        "auto-fit and move the visible bounds."));
    gestureNote->setRole(md::Text::TextRole::Caption);
    gestureNote->setWordWrap(true);

    auto* coordinationColumn = new QWidget;
    auto* coordinationLayout = new QVBoxLayout(coordinationColumn);
    coordinationLayout->setContentsMargins(0, 0, 0, 0);
    coordinationLayout->setSpacing(12);
    coordinationLayout->addWidget(row({zoom, pan}));
    coordinationLayout->addWidget(coordination, 0, Qt::AlignLeft);
    coordinationLayout->addWidget(gestureNote);
    page->addCard(QStringLiteral("Coordinate fit"),
                  QStringLiteral("The shared data-to-pixel transform: the frame re-ticks as "
                                 "the visible range changes."),
                  coordinationColumn);

    // ---- Line ------------------------------------------------------------
    auto* line = new md::LineChart;
    line->setSeries({series(QStringLiteral("Revenue"),
                            values({18.0, 24.0, 21.0, 32.0, 29.0, 41.0, 38.0, 47.0})),
                     series(QStringLiteral("Costs"),
                            values({12.0, 15.0, 14.0, 19.0, 22.0, 20.0, 25.0, 27.0}))});
    line->setSmooth(true);
    line->setShowMarkers(true);
    fit(line);
    page->addCard(QStringLiteral("Line"),
                  QStringLiteral("Two smooth series over eight samples, with markers; the "
                                 "base auto-fits the union of both ranges."),
                  line);

    // ---- Scatter ---------------------------------------------------------
    auto* scatter = new md::ScatterChart;
    scatter->setPointSize(8.0);
    scatter->setShape(md::ScatterChart::PointShape::Circle);
    scatter->setSeries({series(QStringLiteral("Sensor A"),
                               values({22.0, 35.0, 29.0, 48.0, 44.0, 63.0, 57.0, 71.0})),
                        series(QStringLiteral("Sensor B"),
                               values({12.0, 18.0, 24.0, 20.0, 36.0, 31.0, 45.0, 40.0}))});
    fit(scatter);
    page->addCard(QStringLiteral("Scatter"),
                  QStringLiteral("Two point clouds; hovering highlights the nearest sample and "
                                 "leaving clears the halo."),
                  scatter);

    // ---- Pie -------------------------------------------------------------
    const auto slice = [](const QString& name, qreal value) {
        md::ChartSeries item;
        item.name = name;
        item.points.append(QPointF(0.0, value));
        return item;
    };
    auto* pie = new md::PieChart;
    pie->setSeries({slice(QStringLiteral("Design"), 34.0),
                    slice(QStringLiteral("Build"), 26.0),
                    slice(QStringLiteral("Test"), 20.0),
                    slice(QStringLiteral("Docs"), 12.0),
                    slice(QStringLiteral("Ops"), 8.0)});
    pie->setSliceGap(2.0);
    pie->setSelectedSlice(0);
    fit(pie);
    page->addCard(QStringLiteral("Pie"),
                  QStringLiteral("One series per slice; the selected slice is exploded and "
                                 "each share above 5 % is labelled."),
                  pie);

    // ---- Bar -------------------------------------------------------------
    auto* bar = new md::BarChart;
    const QVector<QColor> barPalette = bar->chartPalette();
    md::ChartSeries north = series(QStringLiteral("North"),
                                   values({42.0, 58.0, 51.0, 67.0}));
    north.colour = barPalette.value(0);
    md::ChartSeries south = series(QStringLiteral("South"),
                                   values({36.0, 44.0, 61.0, 55.0}));
    south.colour = barPalette.value(1);
    bar->setSeries({north, south});
    bar->setShowValues(true);
    bar->setGroupGap(0.3);
    fit(bar);
    page->addCard(QStringLiteral("Bar"),
                  QStringLiteral("Two categories of four grouped bars, grown from a zero "
                                 "baseline with the values painted inside."),
                  bar);

    // ---- Histogram -------------------------------------------------------
    auto* histogram = new md::HistogramChart;
    histogram->setSamples({42.0, 45.0, 47.0, 48.0, 49.0, 50.0, 50.0, 51.0,
                           52.0, 52.0, 53.0, 53.0, 54.0, 54.0, 54.0, 55.0,
                           55.0, 56.0, 56.0, 57.0, 57.0, 58.0, 58.0, 59.0,
                           59.0, 60.0, 61.0, 61.0, 62.0, 63.0, 64.0, 65.0,
                           67.0, 68.0, 70.0, 72.0, 74.0, 76.0, 79.0, 84.0});
    histogram->setBinCount(8);
    histogram->setBinMethod(md::HistogramChart::BinMethod::Sturges);
    fit(histogram);
    page->addCard(QStringLiteral("Histogram"),
                  QStringLiteral("One pooled distribution in eight Sturges bins; the x axis "
                                 "is the sample scale, not the bin index."),
                  histogram);

    // ---- Box -------------------------------------------------------------
    auto* box = new md::BoxPlotChart;
    box->setSeries({series(QStringLiteral("Morning"),
                           values({12.0, 16.0, 18.0, 22.0, 25.0, 28.0, 31.0, 35.0,
                                   41.0, 58.0})),
                    series(QStringLiteral("Afternoon"),
                           values({20.0, 23.0, 25.0, 27.0, 29.0, 31.0, 33.0, 36.0,
                                   39.0, 44.0}))});
    box->setShowOutliers(true);
    fit(box);
    page->addCard(QStringLiteral("Box"),
                  QStringLiteral("Two five-number summaries with type-7 quartiles; the 58 in "
                                 "the morning series is beyond the 1.5 IQR fence."),
                  box);

    // ---- Heatmap ---------------------------------------------------------
    auto* heatmap = new md::HeatmapChart;
    heatmap->setRowLabels({QStringLiteral("Mon"), QStringLiteral("Tue"),
                           QStringLiteral("Wed"), QStringLiteral("Thu")});
    heatmap->setColumnLabels({QStringLiteral("08"), QStringLiteral("10"),
                              QStringLiteral("12"), QStringLiteral("14"),
                              QStringLiteral("16"), QStringLiteral("18")});
    heatmap->setRows(4);
    heatmap->setColumns(6);
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 6; ++column)
            heatmap->setValue(row, column, qreal(20 + (row * 13 + column * 7) % 60));
    }
    heatmap->setValue(2, 5, qQNaN());
    heatmap->setShowValues(true);
    fit(heatmap);
    page->addCard(QStringLiteral("Heatmap"),
                  QStringLiteral("A 4x6 categorical matrix over the theme's surface-to-primary "
                                 "ramp; the NaN cell is hatched, not drawn as zero."),
                  heatmap);

    // ---- Radar -----------------------------------------------------------
    auto* radar = new md::RadarChart;
    radar->setAxes({QStringLiteral("Speed"), QStringLiteral("Power"),
                    QStringLiteral("Range"), QStringLiteral("Accuracy"),
                    QStringLiteral("Handling")});
    radar->setRange(0.0, 100.0);
    radar->setRingCount(4);
    radar->setFillOpacity(0.18);
    radar->setSeries({series(QStringLiteral("Alpha"),
                             values({82.0, 64.0, 90.0, 71.0, 76.0})),
                      series(QStringLiteral("Beta"),
                             values({61.0, 86.0, 68.0, 92.0, 63.0}))});
    fit(radar);
    page->addCard(QStringLiteral("Radar"),
                  QStringLiteral("Two normalised polygons on five axes, with all fills painted "
                                 "before any outline."),
                  radar);

    // ---- Funnel ----------------------------------------------------------
    auto* funnel = new md::FunnelChart;
    funnel->setStageLabels({QStringLiteral("Visited"), QStringLiteral("Signed up"),
                            QStringLiteral("Trial"), QStringLiteral("Paid"),
                            QStringLiteral("Renewed")});
    funnel->setSeries({series(QString(), values({1200.0, 880.0, 620.0, 410.0, 240.0}))});
    funnel->setGap(4.0);
    funnel->setShowPercentages(true);
    fit(funnel);
    page->addCard(QStringLiteral("Funnel"),
                  QStringLiteral("One stage series; every stage is a share of the first value "
                                 "and the last stage tapers to a point."),
                  funnel);

    // ---- Clock -----------------------------------------------------------
    auto* clockView = new md::Clock;
    clockView->setDisplay(md::Clock::Display::Both);
    clockView->setUse24Hour(true);
    clockView->setShowSeconds(true);
    clockView->setRunning(true);
    clockView->setFixedSize(chartSize);

    auto* digital = new md::Switch;
    auto* clockNote = new md::Text(QStringLiteral(
        "Both: the analog dial and the 24-hour read-out, ticking once a second."));
    clockNote->setRole(md::Text::TextRole::Caption);
    clockNote->setWordWrap(true);
    QObject::connect(digital, &md::Switch::toggled, clockView,
                     [clockView, clockNote](bool on) {
                         clockView->setDisplay(on ? md::Clock::Display::Digital
                                                  : md::Clock::Display::Analog);
                         clockNote->setText(on ? QStringLiteral("Digital: setDisplay(Digital)")
                                               : QStringLiteral("Analog: setDisplay(Analog)"));
                     });

    auto* clockColumn = new QWidget;
    auto* clockLayout = new QVBoxLayout(clockColumn);
    clockLayout->setContentsMargins(0, 0, 0, 0);
    clockLayout->setSpacing(12);
    clockLayout->addWidget(clockView, 0, Qt::AlignLeft);
    clockLayout->addWidget(
        row({new md::Text(QStringLiteral("Digital read-out")), digital}));
    clockLayout->addWidget(clockNote, 0, Qt::AlignLeft);
    page->addCard(QStringLiteral("Clock"),
                  QStringLiteral("Both faces, 24-hour and running; the switch collapses the "
                                 "clock to one face with setDisplay()."),
                  clockColumn);

    // ---- Schedule --------------------------------------------------------
    auto* schedule = new md::Schedule;
    schedule->setMode(md::Schedule::Mode::Week);
    const QDate weekStart(2026, 11, 9);
    schedule->setDate(weekStart);
    schedule->setDayRange(QTime(9, 0), QTime(18, 0));
    schedule->setNowLineVisible(false);
    schedule->setEditable(true);

    const auto event = [weekStart](const QString& id, const QString& title, int day,
                                   const QTime& start, const QTime& end) {
        md::ScheduleEvent entry;
        entry.id = id;
        entry.title = title;
        entry.start = QDateTime(weekStart.addDays(day), start);
        entry.end = QDateTime(weekStart.addDays(day), end);
        return entry;
    };
    schedule->setEvents({
        event(QStringLiteral("kickoff"), QStringLiteral("Sprint kickoff"), 0,
              QTime(9, 30), QTime(10, 30)),
        event(QStringLiteral("design"), QStringLiteral("Design review"), 1,
              QTime(11, 0), QTime(12, 0)),
        event(QStringLiteral("build"), QStringLiteral("Implementation"), 2,
              QTime(13, 0), QTime(14, 30)),
        event(QStringLiteral("demo"), QStringLiteral("Demo"), 3,
              QTime(15, 0), QTime(16, 0)),
        event(QStringLiteral("retro"), QStringLiteral("Retrospective"), 4,
              QTime(16, 0), QTime(17, 0)),
    });
    page->addCard(QStringLiteral("Schedule"),
                  QStringLiteral("A week of five events in an editable agenda: drag an event "
                                 "vertically to move it in 15-minute snaps, or use Alt+arrow "
                                 "keys."),
                  schedule);

    // ---- Scheduler -------------------------------------------------------
    // The queue belongs to this page, not to main(): it is destroyed with the
    // page and its pending counts never leak into TaskQueue::main().
    auto* queue = new md::schedule::TaskQueue(page);
    auto* scheduler = new md::Scheduler;
    scheduler->setQueue(queue);
    scheduler->setRefreshInterval(150);

    auto* postTasks = new md::Button(QStringLiteral("Post 5 tasks"));
    postTasks->setVariant(md::Button::ButtonVariant::FilledTonal);
    auto* queueNote = new md::Text(
        QStringLiteral("queue: idle — no pending tasks"));
    queueNote->setRole(md::Text::TextRole::Caption);
    queueNote->setWordWrap(true);
    QObject::connect(postTasks, &md::Button::clicked, page,
                     [queue, scheduler, queueNote] {
                         const md::schedule::Priority priorities[] = {
                             md::schedule::Priority::UserInput,
                             md::schedule::Priority::UiUpdate,
                             md::schedule::Priority::UiEvent,
                             md::schedule::Priority::Background,
                             md::schedule::Priority::Idle,
                         };
                         for (int index = 0; index < 5; ++index) {
                             queue->postCoalesced(priorities[index], quint64(index + 1), [] {
                                 std::this_thread::sleep_for(
                                     std::chrono::milliseconds(5));
                             });
                         }
                         // Sample the queue before the event loop can drain it,
                         // so the lane counts describe the five new tasks.
                         scheduler->refresh();
                         queueNote->setText(
                             QStringLiteral("queue: %1 pending across five priorities")
                                 .arg(queue->pending()));
                     });

    auto* schedulerColumn = new QWidget;
    auto* schedulerLayout = new QVBoxLayout(schedulerColumn);
    schedulerLayout->setContentsMargins(0, 0, 0, 0);
    schedulerLayout->setSpacing(12);
    schedulerLayout->addWidget(row({postTasks}));
    schedulerLayout->addWidget(scheduler, 0, Qt::AlignLeft);
    schedulerLayout->addWidget(queueNote);
    page->addCard(QStringLiteral("Scheduler"),
                  QStringLiteral("A panel over a dedicated queue owned by this page, not "
                                 "TaskQueue::main(); Post 5 tasks posts one coalesced task per "
                                 "priority and the lanes show the pending share."),
                  schedulerColumn);

    page->addStretch();
    return page;
}

} // namespace

// The window owns the page registry so that --screenshot can walk it without a
// second composition of the same content.
class GalleryWindow : public QMainWindow
{
public:
    explicit GalleryWindow(bool dark, QWidget* parent = nullptr)
        : QMainWindow(parent)
        , dark_(dark)
    {
        rebuild();
    }

    [[nodiscard]] int pageCount() const { return int(pages_.size()); }
    [[nodiscard]] QString pageTitle(int index) const { return pages_.at(index).title; }

    void showPage(int index)
    {
        if (index < 0 || index >= static_cast<int>(pages_.size()))
            return;

        stack_->setCurrentIndex(index);
        activePage_ = index;
    }

    // The drawer's destination: switch to the page and scroll to its card.
    void navigateTo(const QString& page, const QString& card)
    {
        const int index = pageIndex(page);
        if (index < 0)
            return;

        stack_->setCurrentIndex(index);
        activePage_ = index;

        if (auto* target = dynamic_cast<Page*>(stack_->widget(index));
            target != nullptr && !card.isEmpty()) {
            target->scrollToCard(card);
        }
    }

    [[nodiscard]] int pageIndex(const QString& title) const
    {
        for (int index = 0; index < static_cast<int>(pages_.size()); ++index) {
            if (pages_.at(static_cast<std::size_t>(index)).title == title)
                return index;
        }
        return -1;
    }

    void scrollToCard(int page, const QString& card)
    {
        if (page < 0 || page >= static_cast<int>(pages_.size()))
            return;
        showPage(page);
        if (auto* target = dynamic_cast<Page*>(stack_->widget(page)); target != nullptr)
            target->scrollToCard(card);
    }

    // Used by --screenshot: a page is taller than the window, so the second
    // capture scrolls to the bottom to prove the lower cards render too.
    void scrollPageToBottom(int index)
    {
        if (auto* area = qobject_cast<QScrollArea*>(stack_->widget(index))) {
            area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
            QCoreApplication::processEvents();
        }
    }

    // The toolbar tints itself from the active page's scroll offset, so the
    // capture walks every page from a known top position. A page that is
    // already at zero still gets a nudge, because the toolbar only reacts to a
    // scroll notification and would otherwise keep the previous page's tint.
    void scrollPageToTop(int index)
    {
        if (auto* area = qobject_cast<QScrollArea*>(stack_->widget(index))) {
            QScrollBar* bar = area->verticalScrollBar();
            if (bar->value() == 0 && bar->maximum() > 0)
                bar->setValue(1);
            bar->setValue(0);
            QCoreApplication::processEvents();
        }
    }

    // Scroll-driven chrome (the toolbar tint, the scrollbars) animates for a
    // frame or two after the offset changes. A capture has to wait for that to
    // finish, or two runs disagree on whatever frame they happened to catch.
    static void settle(int milliseconds = 250)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < milliseconds)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    // Drives the Pool page's controls without a pointer, then waits for the
    // counters to move. --pool-smoke uses this so the wiring itself — not
    // just the pool classes — is exercised on a build machine.
    bool runPoolSmoke()
    {
        PoolDemo* demo = poolDemo_.get();
        if (demo == nullptr)
            return false;

        demo->submitJobs(64);
        demo->churnObjectPool(4096);
        demo->startRingStress();
        demo->startChannelDemo();
        demo->pingWorker(8);
        demo->postIoTask();
        demo->readProjectFile();
        demo->startChild();
        demo->sendPing();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        bool extraPingsSent = false;

        while (std::chrono::steady_clock::now() < deadline) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            demo->poll();

            const PoolDemo::Snapshot snapshot = demo->snapshot();

            // The first round trip waits for the child to finish Qt startup, so
            // it is tens of milliseconds; two more show the steady-state value.
            if (!extraPingsSent && snapshot.childRx >= 1) {
                demo->sendPing();
                demo->sendPing();
                extraPingsSent = true;
            }

            const bool ready = snapshot.pool.completed >= 64 && snapshot.ringConsumed > 0
                && snapshot.channelConsumed > 0 && snapshot.pingReplies >= 8
                && snapshot.childRx >= 3 && snapshot.childShmFrames >= 5
                && snapshot.ioFileStatus.startsWith(QStringLiteral("read"));
            if (ready)
                return true;

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const PoolDemo::Snapshot snapshot = demo->snapshot();
        qWarning("pool smoke timed out: jobs=%llu ring=%llu channel=%llu pings=%llu "
                 "childRx=%llu shm=%llu io='%s'",
                 static_cast<unsigned long long>(snapshot.pool.completed),
                 static_cast<unsigned long long>(snapshot.ringConsumed),
                 static_cast<unsigned long long>(snapshot.channelConsumed),
                 static_cast<unsigned long long>(snapshot.pingReplies),
                 static_cast<unsigned long long>(snapshot.childRx),
                 static_cast<unsigned long long>(snapshot.childShmFrames),
                 qPrintable(snapshot.ioFileStatus));
        return false;
    }

private:
    struct Entry
    {
        QString title;
        QString subtitle;
        std::function<QWidget*(const Theme&)> build;
    };

    void rebuild()
    {
        if (drawer_ != nullptr) {
            drawer_->close();
            drawer_->deleteLater();
            drawer_ = nullptr;
        }
        drawerTargets_.clear();
        g_poolDemo = poolDemo_.get();

        const Theme theme = custom_ ? *custom_
                                    : (dark_ ? Theme::baselineDark() : Theme::baselineLight());

        // A preset applied from the theme page replaces the whole palette, so
        // the window follows it instead of the light/dark flag.
        requestTheme = [this](const md::Theme& applied) {
            QTimer::singleShot(0, this, [this, applied] {
                custom_ = applied;
                dark_ = applied.colours().surface.lightness() < 128;
                Theme::setApplicationTheme(applied);
                QApplication::setPalette(applied.toPalette());
                rebuild();
            });
        };

        pages_ = {
            {QStringLiteral("Theme"),
             QStringLiteral("Android colour attributes"),
             &themePage},
            {QStringLiteral("Foundations"),
             QStringLiteral("Shape, elevation, surfaces, motion"),
             &foundationsPage},
            {QStringLiteral("Basic"),
             QStringLiteral("Buttons, selection, progress"),
             &basicPage},
            {QStringLiteral("Layouts"),
             QStringLiteral("Containers and app bars"),
             &containersPage},
            {QStringLiteral("Inputs"),
             QStringLiteral("Text fields and forms"),
             &inputsPage},
            {QStringLiteral("Pickers"),
             QStringLiteral("Dropdowns, colour and wheels"),
             &pickersPage},
            {QStringLiteral("Dates"),
             QStringLiteral("Calendar, cascade, mention"),
             &datesPage},
            {QStringLiteral("Display"),
             QStringLiteral("Type, lists, tabs, overlays"),
             &displayPage},
            {QStringLiteral("Media"),
             QStringLiteral("Avatars, cards, chips, transfer"),
             &mediaPage},
            {QStringLiteral("Time"),
             QStringLiteral("Time, calendar, carousel, anchors"),
             &timeNavPage},
            {QStringLiteral("Navigation"),
             QStringLiteral("Drawer, pager and page scaffold"),
             &navigationPage},
            {QStringLiteral("Data"),
             QStringLiteral("Tables and video"),
             &dataPage},
            {QStringLiteral("Selects"),
             QStringLiteral("Tree and virtual selectors"),
             &selectsPage},
            {QStringLiteral("Feedback"),
             QStringLiteral("Bars, badges, states"),
             &feedbackPage},
            {QStringLiteral("Overlays"),
             QStringLiteral("Menus, alerts, dialogs, language"),
             &overlaysPage},
            {QStringLiteral("Pool"),
             QStringLiteral("Pools, threads, IO and IPC"),
             &poolPage},
            {QStringLiteral("Additions"),
             QStringLiteral("Switch, chips, actions, search, sheets"),
             &additionsPage},
            {QStringLiteral("Charts"),
             QStringLiteral("Canvas, charts and time"),
             &chartsPage},
        };

        auto* root = new md::Constraint;
        root->setObjectName(QStringLiteral("galleryShell"));

        auto* brand = new md::Typography(QStringLiteral("Periodicity Gallery"),
                                        md::Typography::TypeRole::TitleLarge);
        brand->setColour(theme.colours().onSurface);

        auto* menuButton = new md::Button(QStringLiteral("Components"));
        menuButton->setVariant(md::Button::ButtonVariant::Text);
        menuButton->setFixedSize(menuButton->sizeHint());
        QObject::connect(menuButton, &md::Button::clicked, this, [this] {
            if (drawer_ != nullptr)
                drawer_->open();
        });

        auto* toggle = new md::Button(dark_ ? QStringLiteral("Light") : QStringLiteral("Dark"));
        toggle->setVariant(md::Button::ButtonVariant::FilledTonal);
        // The constraint measures with atMost on the unanchored axis; pin the
        // natural size so the label is never elided by a stale measurement.
        toggle->setFixedSize(toggle->sizeHint());
        QObject::connect(toggle, &md::Button::clicked, this, [this] { toggleTheme(); });

        stack_ = new QStackedWidget;
        for (const Entry& entry : pages_)
            stack_->addWidget(entry.build(theme));

        // The component index is a NavigationView inside a modal Drawer, the
        // Android drawer shape: sections are pages, rows are cards.
        auto* navigation = new md::NavigationView;
        navigation->setHeaderVisible(false);
        navigation->setFixedWidth(320);

        QHash<int, QPair<QString, QString>> targets;
        for (int index = 0; index < stack_->count(); ++index) {
            auto* page = dynamic_cast<Page*>(stack_->widget(index));
            if (page == nullptr)
                continue;

            const QString pageTitle = pages_.at(static_cast<std::size_t>(index)).title;
            navigation->addSection(pageTitle);

            for (const QString& card : page->cardTitles())
                targets.insert(navigation->addItem(card), qMakePair(pageTitle, card));
        }

        drawer_ = new md::Drawer(this);
        drawer_->setHeader(QStringLiteral("Periodicity"),
                           QStringLiteral("Component index"));
        drawer_->setWidthDp(320.0);
        drawer_->setContent(navigation);

        QObject::connect(navigation, &md::NavigationView::itemActivated, this,
                         [this, targets](int index, const QString&) {
                             const auto it = targets.constFind(index);
                             if (it == targets.constEnd())
                                 return;
                             navigateTo(it.value().first, it.value().second);
                             if (drawer_ != nullptr)
                                 drawer_->close();
                         });

        auto* stackHost = new WidgetHost(stack_, WidgetHost::Mode::Stretch);

        root->addView(menuButton);
        root->addView(brand);
        root->addView(toggle);
        root->addView(stackHost);

        md::Constraint::Anchors menuAnchors;
        menuAnchors.start = 16.0;
        menuAnchors.top = 20.0;
        root->setConstraints(menuButton, menuAnchors);

        md::Constraint::Anchors brandAnchors;
        brandAnchors.start = 16.0 + menuButton->sizeHint().width() + 16.0;
        brandAnchors.top = 20.0;
        root->setConstraints(brand, brandAnchors);

        md::Constraint::Anchors toggleAnchors;
        toggleAnchors.end = 24.0;
        toggleAnchors.top = 16.0;
        root->setConstraints(toggle, toggleAnchors);

        // "top = header.bottom + 12". A Constraint anchor is a distance from the
        // parent, not a sibling reference, so the header's bottom is resolved
        // from its two children's measured heights and used as the stack's top.
        const qreal headerBottom = std::max(20.0 + brand->sizeHint().height(),
                                            16.0 + toggle->sizeHint().height());

        md::Constraint::Anchors stackAnchors;
        stackAnchors.start = 0.0;
        stackAnchors.end = 0.0;
        stackAnchors.top = headerBottom + 12.0;
        stackAnchors.bottom = 0.0;
        root->setConstraints(stackHost, stackAnchors);

        setCentralWidget(root);

        // A theme change rebuilds the whole window; return to the page the user
        // was looking at instead of resetting to the first one.
        const int restore = std::clamp(activePage_, 0, static_cast<int>(pages_.size()) - 1);
        stack_->setCurrentIndex(restore);
        activePage_ = restore;

        resize(1180, 840);
        setWindowTitle(QStringLiteral("Periodicity — Gallery"));
    }

    void toggleTheme()
    {
        dark_ = !dark_;
        custom_.reset();
        const Theme theme = dark_ ? Theme::baselineDark() : Theme::baselineLight();
        Theme::setApplicationTheme(theme);
        QApplication::setPalette(theme.toPalette());
        QTimer::singleShot(0, this, [this] { rebuild(); });
    }

    bool dark_ = false;
    std::optional<md::Theme> custom_;
    QStackedWidget* stack_ = nullptr;
    md::Drawer* drawer_ = nullptr;
    QVector<QPair<QString, QString>> drawerTargets_;
    std::vector<Entry> pages_;
    int activePage_ = 0;

    // The pool facilities outlive a page: a theme change rebuilds every
    // page, and a worker pool that restarted on every toggle would be a lie.
    // Declared last so it is destroyed first, before the pages that read it.
    std::unique_ptr<PoolDemo> poolDemo_ = std::make_unique<PoolDemo>();
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    const QStringList args = QApplication::arguments();
    const bool dark = args.contains(QStringLiteral("--dark"));
    const Theme theme = dark ? Theme::baselineDark() : Theme::baselineLight();
    Theme::setApplicationTheme(theme);
    QApplication::setPalette(theme.toPalette());

    if (const int dialogShot = args.indexOf(QStringLiteral("--dialog-shot")); dialogShot >= 0) {
        QWidget host;
        host.resize(900, 620);
        host.show();

        auto* dialog = new md::ConfirmDialog(QStringLiteral("Delete preset?"),
                                             QStringLiteral("The palette will fall back "
                                                            "to the baseline colours."),
                                             &host);
        dialog->open();
        QCoreApplication::processEvents();
        const QString path = dialogShot + 1 < args.size()
                                 ? args.at(dialogShot + 1)
                                 : QStringLiteral(".tmp/dialog.png");
        host.grab().save(path);
        return 0;
    }

    GalleryWindow window(dark);

    if (const int smoke = args.indexOf(QStringLiteral("--pool-smoke")); smoke >= 0) {
        window.show();
        QCoreApplication::processEvents();
        window.showPage(window.pageIndex(QStringLiteral("Pool")));
        QCoreApplication::processEvents();

        const bool ok = window.runPoolSmoke();

        // The page's ticker refreshes every 120 ms; give it two passes so the
        // capture shows the values the smoke just produced, not the initial
        // read-out from before any work was submitted.
        const auto settleUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        while (std::chrono::steady_clock::now() < settleUntil)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        const QString path = smoke + 1 < args.size()
            ? args.at(smoke + 1)
            : QStringLiteral("pool-smoke.png");
        const QFileInfo pathInfo(path);
        const QString bottomPath = pathInfo.path() + QLatin1Char('/') + pathInfo.completeBaseName()
            + QStringLiteral("-bottom.") + pathInfo.suffix();

        window.grab().save(path);
        window.scrollPageToBottom(window.pageIndex(QStringLiteral("Pool")));
        QCoreApplication::processEvents();
        window.grab().save(bottomPath);
        return ok ? 0 : 3;
    }

    if (const int cardShot = args.indexOf(QStringLiteral("--card-shot")); cardShot >= 0) {
        window.show();
        QCoreApplication::processEvents();
        window.scrollToCard(window.pageIndex(QStringLiteral("Media")),
                            QStringLiteral("Card"));
        QCoreApplication::processEvents();
        const QString path = cardShot + 1 < args.size()
                                 ? args.at(cardShot + 1)
                                 : QStringLiteral(".tmp/card.png");
        window.grab().save(path);
        return 0;
    }

    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0) {
        const QString directory = shotIndex + 1 < args.size()
                                      ? args.at(shotIndex + 1)
                                      : QStringLiteral(".");
        QDir().mkpath(directory);

        window.show();
        QCoreApplication::processEvents();

        for (int index = 0; index < window.pageCount(); ++index) {
            window.showPage(index);
            window.scrollPageToTop(index);
            GalleryWindow::settle();
            const QString stem = QStringLiteral("%1/%2").arg(
                directory, window.pageTitle(index).toLower());
            if (!window.grab().save(stem + QStringLiteral(".png")))
                return 2;

            window.scrollPageToBottom(index);
            GalleryWindow::settle();
            if (!window.grab().save(stem + QStringLiteral("-bottom.png")))
                return 2;

            // Leave the page at the top so the next capture starts from the
            // same toolbar state no matter which page came before.
            window.scrollPageToTop(index);
            GalleryWindow::settle(120);
        }
        return 0;
    }

    window.show();
    return app.exec();
}
