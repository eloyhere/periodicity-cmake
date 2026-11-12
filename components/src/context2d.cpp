// Periodicity — implementation of the retained 2D painter façade.
//
// Each primitive is stored with a snapshot of the style and data transform in
// effect when it was issued. Replay therefore has the same result whether the
// Canvas paints immediately or a hundred frames later, and save()/restore()
// behaves like the matching QPainter calls for all following primitives.

#include <md/components/context2d.h>

#include <md/core/density.h>
#include <md/theme/theme.h>

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPolygonF>
#include <QScopeGuard>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

namespace md {
namespace {

// Keeps a failed drawing entry point from unwinding into Qt. Context2D is not
// a QObject, so there is no central guard report; each call reports and drops
// the failed operation instead.
template <class Fn>
void guardContext2D(const char* where, Fn&& fn)
{
    try {
        std::forward<Fn>(fn)();
    } catch (const std::exception& error) {
        qWarning("md::Context2D: %s failed: %s", where, error.what());
    } catch (...) {
        qWarning("md::Context2D: %s failed: unknown exception", where);
    }
}

constexpr std::size_t kMaximumSaveDepth = 1024;

} // namespace

struct Context2D::Impl
{
    enum class Kind {
        Line,
        Polyline,
        Rect,
        RoundedRect,
        Ellipse,
        Arc,
        Path,
        TextPoint,
        TextRect,
    };

    struct State
    {
        QPen pen;
        QBrush brush;
        QFont font;
        QTransform transform;
        bool hasClipRect = false;
        QRectF clipRect;
        bool hasClipPath = false;
        QPainterPath clipPath;
    };

    struct Command
    {
        Kind kind = Kind::Line;
        State state;
        QPointF from;
        QPointF to;
        QPolygonF polygon;
        QRectF rect;
        QPainterPath path;
        QString text;
        qreal radiusX = 0.0;
        qreal radiusY = 0.0;
        int startAngle = 0;
        int spanAngle = 0;
        int flags = 0;
    };

    static State themeState(QWidget* host)
    {
        const Theme& theme = Theme::forWidget(host);
        const Density& density = theme.density();

        State state;
        state.pen = QPen(theme.colours().onSurface);
        state.pen.setWidthF(density.dp(1.5));
        state.pen.setCapStyle(Qt::RoundCap);
        state.pen.setJoinStyle(Qt::RoundJoin);
        state.brush = QBrush(theme.colours().primary);
        state.font = QFont();
        state.transform = QTransform();
        return state;
    }

    QPointer<QWidget> host;
    State state;
    std::vector<State> stack;
    std::vector<Command> commands;
    bool saveLimitWarned = false;
};

Context2D::Context2D(QWidget* host)
    : d_(std::make_unique<Impl>())
{
    d_->host = host;
    d_->state = Impl::themeState(host);
}

Context2D::~Context2D() = default;

void Context2D::save()
{
    guardContext2D("Context2D::save", [this] {
        if (d_->stack.size() >= kMaximumSaveDepth) {
            if (!d_->saveLimitWarned) {
                d_->saveLimitWarned = true;
                qWarning("md::Context2D: save() nesting limit reached");
            }
            return;
        }

        d_->stack.push_back(d_->state);
    });
}

void Context2D::restore()
{
    guardContext2D("Context2D::restore", [this] {
        if (d_->stack.empty())
            return;

        d_->state = d_->stack.back();
        d_->stack.pop_back();
    });
}

void Context2D::setClipRect(const QRectF& rect)
{
    guardContext2D("Context2D::setClipRect", [this, &rect] {
        d_->state.clipRect = rect;
        d_->state.hasClipRect = true;
    });
}

void Context2D::setClipPath(const QPainterPath& path)
{
    guardContext2D("Context2D::setClipPath", [this, &path] {
        d_->state.clipPath = path;
        d_->state.hasClipPath = true;
    });
}

void Context2D::clearClip()
{
    guardContext2D("Context2D::clearClip", [this] {
        d_->state.hasClipRect = false;
        d_->state.clipRect = QRectF();
        d_->state.hasClipPath = false;
        d_->state.clipPath = QPainterPath();
    });
}

void Context2D::setPen(const QPen& pen)
{
    guardContext2D("Context2D::setPen", [this, &pen] { d_->state.pen = pen; });
}

void Context2D::setPen(const QColor& colour)
{
    guardContext2D("Context2D::setPen", [this, &colour] {
        QPen next = d_->state.pen;
        next.setColor(colour.isValid()
                          ? colour
                          : Theme::forWidget(d_->host.data()).colours().onSurface);
        d_->state.pen = next;
    });
}

void Context2D::setBrush(const QBrush& brush)
{
    guardContext2D("Context2D::setBrush", [this, &brush] { d_->state.brush = brush; });
}

void Context2D::setBrush(const QColor& colour)
{
    guardContext2D("Context2D::setBrush", [this, &colour] {
        d_->state.brush = colour.isValid() ? QBrush(colour) : QBrush(Qt::NoBrush);
    });
}

void Context2D::setFont(const QFont& font)
{
    guardContext2D("Context2D::setFont", [this, &font] { d_->state.font = font; });
}

QPen Context2D::pen() const
{
    return d_->state.pen;
}

QBrush Context2D::brush() const
{
    return d_->state.brush;
}

QFont Context2D::font() const
{
    return d_->state.font;
}

void Context2D::setTransform(qreal scaleX, qreal scaleY, qreal offsetX, qreal offsetY)
{
    guardContext2D("Context2D::setTransform", [this, scaleX, scaleY, offsetX, offsetY] {
        if (!std::isfinite(scaleX) || !std::isfinite(scaleY)
            || !std::isfinite(offsetX) || !std::isfinite(offsetY)) {
            qWarning("md::Context2D: ignoring a non-finite data transform");
            return;
        }

        d_->state.transform = QTransform(scaleX, 0.0, 0.0, scaleY, offsetX, offsetY);
    });
}

void Context2D::setTransform(const QTransform& transform)
{
    guardContext2D("Context2D::setTransform", [this, &transform] {
        const qreal values[] = {
            transform.m11(), transform.m12(), transform.m21(),
            transform.m22(), transform.dx(), transform.dy(),
        };
        for (const qreal value : values) {
            if (!std::isfinite(value)) {
                qWarning("md::Context2D: ignoring a non-finite data transform");
                return;
            }
        }

        d_->state.transform = transform;
    });
}

QTransform Context2D::transform() const
{
    return d_->state.transform;
}

QPointF Context2D::mapToWidget(const QPointF& point) const
{
    return d_->state.transform.map(point);
}

QPointF Context2D::mapToData(const QPointF& point) const
{
    bool invertible = false;
    const QTransform inverse = d_->state.transform.inverted(&invertible);
    if (!invertible)
        return point;

    return inverse.map(point);
}

void Context2D::drawLine(const QPointF& from, const QPointF& to)
{
    guardContext2D("Context2D::drawLine", [this, &from, &to] {
        Impl::Command command;
        command.kind = Impl::Kind::Line;
        command.state = d_->state;
        command.from = from;
        command.to = to;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawPolyline(const QPolygonF& points)
{
    guardContext2D("Context2D::drawPolyline", [this, &points] {
        Impl::Command command;
        command.kind = Impl::Kind::Polyline;
        command.state = d_->state;
        command.polygon = points;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawRect(const QRectF& rect)
{
    guardContext2D("Context2D::drawRect", [this, &rect] {
        Impl::Command command;
        command.kind = Impl::Kind::Rect;
        command.state = d_->state;
        command.rect = rect;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawRoundedRect(const QRectF& rect, qreal radiusX, qreal radiusY)
{
    guardContext2D("Context2D::drawRoundedRect", [this, &rect, radiusX, radiusY] {
        Impl::Command command;
        command.kind = Impl::Kind::RoundedRect;
        command.state = d_->state;
        command.rect = rect;
        command.radiusX = std::max<qreal>(0.0, radiusX);
        command.radiusY = std::max<qreal>(0.0, radiusY);
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawEllipse(const QRectF& rect)
{
    guardContext2D("Context2D::drawEllipse", [this, &rect] {
        Impl::Command command;
        command.kind = Impl::Kind::Ellipse;
        command.state = d_->state;
        command.rect = rect;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawEllipse(const QPointF& centre, qreal radiusX, qreal radiusY)
{
    guardContext2D("Context2D::drawEllipse", [this, &centre, radiusX, radiusY] {
        Impl::Command command;
        command.kind = Impl::Kind::Ellipse;
        command.state = d_->state;
        command.rect = QRectF(centre.x() - std::abs(radiusX),
                              centre.y() - std::abs(radiusY),
                              2.0 * std::abs(radiusX), 2.0 * std::abs(radiusY));
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawArc(const QRectF& rect, int startAngle, int spanAngle)
{
    guardContext2D("Context2D::drawArc", [this, &rect, startAngle, spanAngle] {
        Impl::Command command;
        command.kind = Impl::Kind::Arc;
        command.state = d_->state;
        command.rect = rect;
        command.startAngle = startAngle;
        command.spanAngle = spanAngle;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawPath(const QPainterPath& path)
{
    guardContext2D("Context2D::drawPath", [this, &path] {
        Impl::Command command;
        command.kind = Impl::Kind::Path;
        command.state = d_->state;
        command.path = path;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawText(const QPointF& position, const QString& text)
{
    guardContext2D("Context2D::drawText", [this, &position, &text] {
        Impl::Command command;
        command.kind = Impl::Kind::TextPoint;
        command.state = d_->state;
        command.from = position;
        command.text = text;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::drawText(const QRectF& rect, int flags, const QString& text)
{
    guardContext2D("Context2D::drawText", [this, &rect, flags, &text] {
        Impl::Command command;
        command.kind = Impl::Kind::TextRect;
        command.state = d_->state;
        command.rect = rect;
        command.flags = flags;
        command.text = text;
        d_->commands.push_back(std::move(command));
    });
}

void Context2D::paint(QPainter& painter, const QRectF& viewport)
{
    guardContext2D("Context2D::paint", [this, &painter, &viewport] {
        if (!painter.isActive() || viewport.isEmpty() || d_->commands.empty())
            return;

        for (const Impl::Command& command : d_->commands) {
            try {
                painter.save();
                const auto restorePainter = qScopeGuard([&painter] {
                    painter.restore();
                });

                // The viewport clip is in widget coordinates; the data clip is
                // applied after the data transform. Replacing the world
                // transform first makes the data transform absolute and keeps
                // the content clip valid independently of it.
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setRenderHint(QPainter::TextAntialiasing, true);
                painter.setTransform(QTransform());
                painter.setClipRect(viewport, Qt::IntersectClip);
                painter.setTransform(command.state.transform);

                if (command.state.hasClipRect) {
                    painter.setClipRect(command.state.clipRect, Qt::IntersectClip);
                }
                if (command.state.hasClipPath) {
                    painter.setClipPath(command.state.clipPath, Qt::IntersectClip);
                }

                painter.setPen(command.state.pen);
                painter.setBrush(command.state.brush);
                painter.setFont(command.state.font);

                switch (command.kind) {
                case Impl::Kind::Line:
                    painter.drawLine(command.from, command.to);
                    break;
                case Impl::Kind::Polyline:
                    painter.drawPolyline(command.polygon);
                    break;
                case Impl::Kind::Rect:
                    painter.drawRect(command.rect);
                    break;
                case Impl::Kind::RoundedRect:
                    painter.drawRoundedRect(command.rect, command.radiusX,
                                            command.radiusY);
                    break;
                case Impl::Kind::Ellipse:
                    painter.drawEllipse(command.rect);
                    break;
                case Impl::Kind::Arc:
                    painter.drawArc(command.rect, command.startAngle,
                                    command.spanAngle);
                    break;
                case Impl::Kind::Path:
                    painter.drawPath(command.path);
                    break;
                case Impl::Kind::TextPoint:
                    painter.drawText(command.from, command.text);
                    break;
                case Impl::Kind::TextRect:
                    painter.drawText(command.rect, command.flags, command.text);
                    break;
                }
            } catch (const std::exception& error) {
                qWarning("md::Context2D: replaying a primitive failed: %s",
                         error.what());
            } catch (...) {
                qWarning("md::Context2D: replaying a primitive failed: "
                         "unknown exception");
            }
        }
    });
}

void Context2D::clear()
{
    guardContext2D("Context2D::clear", [this] { d_->commands.clear(); });
}

void Context2D::reset()
{
    guardContext2D("Context2D::reset", [this] {
        d_->commands.clear();
        d_->stack.clear();
        d_->saveLimitWarned = false;
        d_->state = Impl::themeState(d_->host.data());
    });
}

void Context2D::requestDraw()
{
    guardContext2D("Context2D::requestDraw", [this] {
        if (d_->host)
            d_->host->update();
    });
}

} // namespace md
