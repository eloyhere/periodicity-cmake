// Periodicity — the theme switcher.
//
// A preset is a whole `md::Theme`. The component shows one row per preset with
// the three colour attributes a host usually configures — colourPrimary,
// colourSecondary and colourAccent — and a click applies the preset to the
// application theme and its QPalette.
//
// The attributes come from `Theme::colourPrimary()` and friends, so there is no
// separate palette type: the Theme is the palette.

#pragma once

#include <md/theme/theme.h>
#include <md/view/view.h>

#include <QColor>
#include <QList>
#include <QString>

class QKeyEvent;
class QMouseEvent;
class QPainter;
class QRectF;

namespace md {

class ThemeSwitcher final : public View
{
    Q_OBJECT

public:
    struct Preset
    {
        QString name;
        Theme theme;
    };

    explicit ThemeSwitcher(QWidget* parent = nullptr);

    // Adds a preset built from three seed colours. The type, shape and motion
    // scales come from the baseline of the current brightness.
    void addPreset(QString name, QColor primary, QColor second, QColor accent);
    void addPreset(QString name, Theme theme);

    [[nodiscard]] QList<Preset> presets() const { return presets_; }
    [[nodiscard]] int presetCount() const noexcept { return presets_.size(); }

    [[nodiscard]] int currentPreset() const noexcept { return current_; }
    void setCurrentPreset(int index);

    // Applies the preset to the application theme (and the QPalette of the
    // running QApplication), then emits presetActivated/themeApplied. An
    // out-of-range index is ignored.
    void applyPreset(int index);

    // Painted geometry, so a test or a sibling can hit-test without guessing.
    [[nodiscard]] QRectF presetRect(int index) const;
    [[nodiscard]] QRectF swatchRect(int index, int swatch) const;

    // The Android attribute names this component labels its swatches with.
    [[nodiscard]] static QString swatchLabel(int swatch);

signals:
    void presetActivated(int index);
    void themeApplied(const md::Theme& theme);

protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void changeEvent(QEvent* event) override;
    [[nodiscard]] bool focusable() const override;

private:
    [[nodiscard]] int rowAt(const QPointF& position) const;

    QList<Preset> presets_;
    int current_ = -1;
};

} // namespace md
