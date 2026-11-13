// Periodicity — gfx layer: uniforms and the built-in software shaders.
//
// Nothing here compiles GLSL and nothing touches a device. The built-ins are
// plain functions over FragmentInput, so the same shader can run under the
// software rasterizer today and be translated for a device in phase 2 without a
// behavioural fork: the uniform names and fallbacks are the interface.

#include <md/gfx/shader.h>

#include <QDebug>

#include <cmath>

namespace md::gfx {

namespace {

bool isFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value));
}

bool isFinite(const QVector3D& vector)
{
    return isFinite(vector.x()) && isFinite(vector.y()) && isFinite(vector.z());
}

bool isFinite(const QMatrix4x4& matrix)
{
    const float* data = matrix.constData();
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(static_cast<double>(data[i])))
            return false;
    }
    return true;
}

QColor vertexColourOrWhite(const FragmentInput& input)
{
    return input.colour.isValid() ? input.colour : QColor(Qt::white);
}

QVector4D standardVertex(const Vertex3D& vertex, const Uniforms& uniforms)
{
    const QMatrix4x4 mvp = uniforms.matrix(QStringLiteral("mvp"), QMatrix4x4());
    return mvp * QVector4D(vertex.position, 1.0F);
}

QColor shadeSolid(const FragmentInput&, const Uniforms& uniforms)
{
    return uniforms.colour(QStringLiteral("colour"), QColor(Qt::white));
}

QColor shadeVertexColour(const FragmentInput& input, const Uniforms&)
{
    return vertexColourOrWhite(input);
}

QColor shadeLambert(const FragmentInput& input, const Uniforms& uniforms)
{
    QVector3D light = uniforms.vector(QStringLiteral("lightDirection"),
                                      QVector3D(0.0F, 0.0F, 1.0F));
    if (light.lengthSquared() <= 0.0F)
        light = QVector3D(0.0F, 0.0F, 1.0F);
    else
        light.normalize();

    QVector3D normal = input.normal;
    if (normal.lengthSquared() > 0.0F)
        normal.normalize();

    const float intensity = static_cast<float>(
        qBound(0.0, static_cast<qreal>(QVector3D::dotProduct(normal, light)), 1.0));
    const float ambient = static_cast<float>(
        qBound(0.0, uniforms.real(QStringLiteral("ambient"), 0.2), 1.0));
    const float factor = ambient + (1.0F - ambient) * intensity;

    const QColor base = uniforms.colour(QStringLiteral("baseColour"), QColor(Qt::white));
    const QColor vertex = vertexColourOrWhite(input);
    return QColor::fromRgbF(base.redF() * vertex.redF() * factor,
                            base.greenF() * vertex.greenF() * factor,
                            base.blueF() * vertex.blueF() * factor,
                            base.alphaF() * vertex.alphaF());
}

QColor shadeUnlit(const FragmentInput& input, const Uniforms& uniforms)
{
    const QColor base = vertexColourOrWhite(input);
    const Texture texture = uniforms.texture(QStringLiteral("texture"));
    if (!texture.isValid())
        return base;

    const QColor texel = texture.sample(input.uv, 0.0);
    return QColor::fromRgbF(base.redF() * texel.redF(),
                            base.greenF() * texel.greenF(),
                            base.blueF() * texel.blueF(),
                            base.alphaF() * texel.alphaF());
}

} // namespace

// ---------------------------------------------------------------------------
// Uniforms

void Uniforms::set(const QString& name, qreal value)
{
    if (name.isEmpty() || !isFinite(value)) {
        qWarning() << "gfx::Uniforms::set: rejected non-finite value for" << name;
        return;
    }
    values_.insert(name, value);
}

void Uniforms::set(const QString& name, const QColor& value)
{
    if (name.isEmpty() || !value.isValid() || !isFinite(value.redF())
        || !isFinite(value.greenF()) || !isFinite(value.blueF())
        || !isFinite(value.alphaF())) {
        qWarning() << "gfx::Uniforms::set: rejected invalid colour for" << name;
        return;
    }
    values_.insert(name, value);
}

void Uniforms::set(const QString& name, const QVector3D& value)
{
    if (name.isEmpty() || !isFinite(value)) {
        qWarning() << "gfx::Uniforms::set: rejected non-finite vector for" << name;
        return;
    }
    values_.insert(name, value);
}

void Uniforms::set(const QString& name, const QMatrix4x4& value)
{
    if (name.isEmpty() || !isFinite(value)) {
        qWarning() << "gfx::Uniforms::set: rejected non-finite matrix for" << name;
        return;
    }
    values_.insert(name, value);
}

void Uniforms::set(const QString& name, const Texture& value)
{
    if (name.isEmpty()) {
        qWarning() << "gfx::Uniforms::set: rejected empty uniform name";
        return;
    }
    values_.insert(name, value);
}

qreal Uniforms::real(const QString& name, qreal fallback) const
{
    const auto it = values_.constFind(name);
    if (it == values_.cend())
        return fallback;
    if (const auto* value = std::get_if<qreal>(&it.value()))
        return *value;
    return fallback;
}

QColor Uniforms::colour(const QString& name, const QColor& fallback) const
{
    const auto it = values_.constFind(name);
    if (it == values_.cend())
        return fallback;
    if (const auto* value = std::get_if<QColor>(&it.value()))
        return *value;
    return fallback;
}

QVector3D Uniforms::vector(const QString& name, const QVector3D& fallback) const
{
    const auto it = values_.constFind(name);
    if (it == values_.cend())
        return fallback;
    if (const auto* value = std::get_if<QVector3D>(&it.value()))
        return *value;
    return fallback;
}

QMatrix4x4 Uniforms::matrix(const QString& name, const QMatrix4x4& fallback) const
{
    const auto it = values_.constFind(name);
    if (it == values_.cend())
        return fallback;
    if (const auto* value = std::get_if<QMatrix4x4>(&it.value()))
        return *value;
    return fallback;
}

Texture Uniforms::texture(const QString& name, const Texture& fallback) const
{
    const auto it = values_.constFind(name);
    if (it == values_.cend())
        return fallback;
    if (const auto* value = std::get_if<Texture>(&it.value()))
        return *value;
    return fallback;
}

// ---------------------------------------------------------------------------
// Shader

Shader::Shader(QString name, VertexStage vertexStage, FragmentStage fragmentStage)
    : name_(std::move(name))
    , vertexStage_(std::move(vertexStage))
    , fragmentStage_(std::move(fragmentStage))
{
}

bool Shader::isValid() const
{
    return !name_.isEmpty() && static_cast<bool>(vertexStage_)
        && static_cast<bool>(fragmentStage_);
}

// ---------------------------------------------------------------------------
// ShaderLibrary

Shader ShaderLibrary::find(const QString& name)
{
    if (name == QStringLiteral("solid"))
        return solid();
    if (name == QStringLiteral("vertexColour"))
        return vertexColour();
    if (name == QStringLiteral("lambert"))
        return lambert();
    if (name == QStringLiteral("unlit"))
        return unlit();

    // An unknown program is an invalid value, not an exception: callers look up
    // shaders by name every frame and must not need a failure path.
    return Shader();
}

Shader ShaderLibrary::solid()
{
    return Shader(QStringLiteral("solid"), standardVertex, shadeSolid);
}

Shader ShaderLibrary::vertexColour()
{
    return Shader(QStringLiteral("vertexColour"), standardVertex, shadeVertexColour);
}

Shader ShaderLibrary::lambert()
{
    return Shader(QStringLiteral("lambert"), standardVertex, shadeLambert);
}

Shader ShaderLibrary::unlit()
{
    return Shader(QStringLiteral("unlit"), standardVertex, shadeUnlit);
}

QStringList ShaderLibrary::names()
{
    return {QStringLiteral("solid"), QStringLiteral("vertexColour"),
            QStringLiteral("lambert"), QStringLiteral("unlit")};
}

} // namespace md::gfx
