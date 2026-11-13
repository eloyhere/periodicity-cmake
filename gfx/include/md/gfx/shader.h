// Periodicity — gfx layer: the program interface and its uniform bag.
//
// A Shader is a name plus two plain functions: a software vertex stage that maps
// a Vertex3D to clip space, and a software fragment stage that turns an
// interpolated FragmentInput into a colour. Nothing here compiles GLSL; that is
// render/'s job in phase 2, and the optional source pair lives there.
//
// ShaderLibrary owns the built-in programs. An unknown name returns a default
// Shader that is isValid() == false — never an exception — so looking a shader
// up can be done on every frame without a failure path.
//
// Uniform names the built-ins read (a missing uniform takes the documented
// fallback, which is why every built-in works on an empty bag):
//
//   "mvp"             QMatrix4x4  model-view-projection (identity)
//   "colour"          QColor      solid fill (white)
//   "baseColour"      QColor      lambert albedo (white)
//   "lightDirection"  QVector3D   lambert light direction, model space, (0,0,1)
//   "ambient"         qreal       lambert ambient term, [0,1], 0.2
//   "texture"         Texture     unlit texture, optional
//
// The rasterizer evaluates the fragment stage; the vertex stage is used for
// Mesh3D and ignored for Mesh2D, whose positions are already in target pixels.

#pragma once

#include <md/gfx/mesh.h>
#include <md/gfx/texture.h>

#include <QColor>
#include <QHash>
#include <QMatrix4x4>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector3D>

#include <functional>
#include <variant>

namespace md::gfx {

// A typed, name-addressed uniform bag. Setting a name replaces whatever type was
// there before. Asking for a name with the wrong type returns the fallback
// rather than converting it, so a typo is visible in the output instead of
// silently turning into black.
class Uniforms
{
public:
    Uniforms() = default;

    void set(const QString& name, qreal value);
    void set(const QString& name, const QColor& value);
    void set(const QString& name, const QVector3D& value);
    void set(const QString& name, const QMatrix4x4& value);
    void set(const QString& name, const Texture& value);

    bool contains(const QString& name) const { return values_.contains(name); }
    bool remove(const QString& name) { return values_.remove(name); }
    void clear() { values_.clear(); }
    bool isEmpty() const { return values_.isEmpty(); }
    int size() const { return values_.size(); }

    qreal real(const QString& name, qreal fallback = 0.0) const;
    QColor colour(const QString& name, const QColor& fallback = QColor(Qt::white)) const;
    QVector3D vector(const QString& name, const QVector3D& fallback = QVector3D()) const;
    QMatrix4x4 matrix(const QString& name, const QMatrix4x4& fallback = QMatrix4x4()) const;
    Texture texture(const QString& name, const Texture& fallback = Texture()) const;

private:
    using Value = std::variant<qreal, QColor, QVector3D, QMatrix4x4, Texture>;
    QHash<QString, Value> values_;
};

struct FragmentInput
{
    QPointF uv;
    QColor colour = QColor(Qt::white);
    QVector3D normal;
    QVector3D worldPosition;
    qreal depth = 0.0;
};

class Shader
{
public:
    using VertexStage = std::function<QVector4D(const Vertex3D&, const Uniforms&)>;
    using FragmentStage = std::function<QColor(const FragmentInput&, const Uniforms&)>;

    Shader() = default;
    Shader(QString name, VertexStage vertexStage, FragmentStage fragmentStage);

    // A shader needs a name and both stages. The default Shader is invalid.
    bool isValid() const;

    QString name() const { return name_; }
    const VertexStage& vertexStage() const { return vertexStage_; }
    const FragmentStage& fragmentStage() const { return fragmentStage_; }

private:
    QString name_;
    VertexStage vertexStage_;
    FragmentStage fragmentStage_;
};

class ShaderLibrary
{
public:
    // The four built-ins are "solid", "vertexColour", "lambert" and "unlit".
    // An unknown name (including an empty one) yields an invalid Shader.
    static Shader find(const QString& name);

    static Shader solid();
    static Shader vertexColour();
    static Shader lambert();
    static Shader unlit();

    static QStringList names();
};

} // namespace md::gfx
