// Periodicity — the Android-style attribute layer on Theme.
//
// Android's view system names its palette with `colorPrimary`, `colorPrimaryDark`,
// `colorSecondary` and `colorAccent`. A host application usually thinks in those
// names, so Theme answers them directly instead of forcing a second palette type:
//
//   * `colourPrimary()` / `colourSecondary()` / `colourAccent()` read the token
//     scheme back through the Android names;
//   * `colour("colorPrimary")` resolves an attribute name, accepting both the
//     American and British spellings and the short `Second`/`Acent` forms;
//   * `fromColours()` builds a whole Theme from three seed colours.

#include <md/theme/theme.h>

#include <QtGui/qrgb.h>

#include <algorithm>

namespace md {
namespace {

QColor blend(const QColor& from, const QColor& to, qreal amount)
{
    const qreal t = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t,
                            from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t,
                            from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

QColor onColour(const QColor& colour)
{
    return qGray(colour.rgb()) > 140 ? QColor(0x1D, 0x1B, 0x20) : QColor(Qt::white);
}

QColor container(const QColor& seed, bool dark)
{
    return dark ? blend(seed, QColor(Qt::black), 0.62)
                : blend(seed, QColor(Qt::white), 0.84);
}

QString normalised(const QString& attribute)
{
    QString key = attribute.trimmed().toLower();
    key.remove(QLatin1Char(' '));
    key.remove(QLatin1Char('_'));
    key.remove(QLatin1Char('-'));
    if (key.startsWith(QStringLiteral("colour")))
        key.replace(0, 6, QStringLiteral("color"));
    if (!key.startsWith(QStringLiteral("color")))
        key.prepend(QStringLiteral("color"));
    return key;
}

} // namespace

Theme Theme::fromColours(QColor primary, QColor secondary, QColor accent, bool dark)
{
    const Theme base = dark ? Theme::baselineDark() : Theme::baselineLight();
    ColorScheme scheme = base.colours();

    const QColor first = primary.isValid() ? primary : scheme.primary;
    const QColor secondColour = secondary.isValid() ? secondary : scheme.secondary;
    const QColor accentColour = accent.isValid() ? accent : scheme.tertiary;

    scheme.primary = first;
    scheme.onPrimary = onColour(first);
    scheme.primaryContainer = container(first, dark);
    scheme.onPrimaryContainer = onColour(scheme.primaryContainer);
    scheme.primaryFixed = blend(first, QColor(Qt::white), 0.82);
    scheme.primaryFixedDim = blend(first, QColor(Qt::white), 0.66);
    scheme.onPrimaryFixed = onColour(scheme.primaryFixed);
    scheme.onPrimaryFixedVariant = blend(first, QColor(Qt::black), 0.36);

    scheme.secondary = secondColour;
    scheme.onSecondary = onColour(secondColour);
    scheme.secondaryContainer = container(secondColour, dark);
    scheme.onSecondaryContainer = onColour(scheme.secondaryContainer);
    scheme.secondaryFixed = blend(secondColour, QColor(Qt::white), 0.82);
    scheme.secondaryFixedDim = blend(secondColour, QColor(Qt::white), 0.66);
    scheme.onSecondaryFixed = onColour(scheme.secondaryFixed);
    scheme.onSecondaryFixedVariant = blend(secondColour, QColor(Qt::black), 0.36);

    scheme.tertiary = accentColour;
    scheme.onTertiary = onColour(accentColour);
    scheme.tertiaryContainer = container(accentColour, dark);
    scheme.onTertiaryContainer = onColour(scheme.tertiaryContainer);
    scheme.tertiaryFixed = blend(accentColour, QColor(Qt::white), 0.82);
    scheme.tertiaryFixedDim = blend(accentColour, QColor(Qt::white), 0.66);
    scheme.onTertiaryFixed = onColour(scheme.tertiaryFixed);
    scheme.onTertiaryFixedVariant = blend(accentColour, QColor(Qt::black), 0.36);

    // Android keeps the window background neutral and lets the app bars carry
    // the brand colour; a light seed tint keeps the M3 surfaces from looking
    // flat next to those bars.
    const QColor raised = dark ? QColor(Qt::black) : QColor(Qt::white);
    const QColor lifted = dark ? QColor(Qt::white) : QColor(Qt::black);

    scheme.surface = blend(first, raised, dark ? 0.86 : 0.97);
    scheme.surfaceVariant = blend(first, raised, dark ? 0.70 : 0.88);
    scheme.onSurface = onColour(scheme.surface);
    scheme.onSurfaceVariant = onColour(scheme.surfaceVariant);
    scheme.surfaceDim = blend(scheme.surface, lifted, 0.06);
    scheme.surfaceBright = blend(scheme.surface, raised, 0.06);
    scheme.surfaceContainerLowest = blend(scheme.surface, raised, 0.04);
    scheme.surfaceContainerLow = blend(scheme.surface, lifted, 0.04);
    scheme.surfaceContainer = blend(scheme.surface, lifted, 0.07);
    scheme.surfaceContainerHigh = blend(scheme.surface, lifted, 0.11);
    scheme.surfaceContainerHighest = blend(scheme.surface, lifted, 0.15);
    scheme.outline = blend(scheme.onSurface, scheme.surfaceVariant, 0.45);
    scheme.outlineVariant = blend(scheme.surfaceVariant, scheme.onSurfaceVariant, 0.25);

    scheme.inversePrimary = blend(first, raised, 0.35);
    scheme.surfaceTint = first;

    return base.withColours(scheme);
}

QColor Theme::colourPrimary() const
{
    return colours_.primary;
}

QColor Theme::colourPrimaryDark() const
{
    // Android uses this attribute for the status bar: a genuinely darker shade
    // of the primary, not the Material 3 container tone (which is a light tint
    // in a light theme).
    const bool dark = colours_.surface.lightness() < 128;
    return blend(colours_.primary, QColor(Qt::black), dark ? 0.35 : 0.28);
}

QColor Theme::colourSecondary() const
{
    return colours_.secondary;
}

QColor Theme::colourSecondaryDark() const
{
    const bool dark = colours_.surface.lightness() < 128;
    return blend(colours_.secondary, QColor(Qt::black), dark ? 0.35 : 0.28);
}

QColor Theme::colourAccent() const
{
    return colours_.tertiary;
}

QColor Theme::colour(const QString& attribute) const
{
    const QString key = normalised(attribute);

    if (key == QLatin1String("colorprimary"))
        return colours_.primary;
    if (key == QLatin1String("colorprimarydark") || key == QLatin1String("colorprimaryvariant"))
        return colourPrimaryDark();
    if (key == QLatin1String("colorsecond") || key == QLatin1String("colorsecondary"))
        return colours_.secondary;
    if (key == QLatin1String("colorseconddark") || key == QLatin1String("colorsecondaryvariant"))
        return colourSecondaryDark();
    if (key == QLatin1String("coloracent") || key == QLatin1String("coloraccent")
        || key == QLatin1String("colortertiary"))
        return colours_.tertiary;

    if (key == QLatin1String("coloronprimary"))
        return colours_.onPrimary;
    if (key == QLatin1String("coloronsecond") || key == QLatin1String("coloronsecondary"))
        return colours_.onSecondary;
    if (key == QLatin1String("coloronacent") || key == QLatin1String("coloronaccent"))
        return colours_.onTertiary;

    if (key == QLatin1String("colorbackground") || key == QLatin1String("colorwindowbackground"))
        return colours_.surface;
    if (key == QLatin1String("colorsurface"))
        return colours_.surface;
    if (key == QLatin1String("colorsurfacevariant"))
        return colours_.surfaceVariant;
    if (key == QLatin1String("coloronsurface"))
        return colours_.onSurface;
    if (key == QLatin1String("coloronsurfacevariant"))
        return colours_.onSurfaceVariant;

    if (key == QLatin1String("colorerror"))
        return colours_.error;
    if (key == QLatin1String("coloronerror"))
        return colours_.onError;

    if (key == QLatin1String("colorcontrolhighlight"))
        return colours_.primary;
    if (key == QLatin1String("colorcontrolnormal"))
        return colours_.onSurfaceVariant;

    return QColor();
}

} // namespace md
