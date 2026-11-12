#include <md/components/form.h>

#include <md/theme/theme.h>

#include <QLineEdit>
#include <QPainter>
#include <QVariant>

#include <algorithm>

namespace md {
QString FormRule::validate(const QVariant& value) const { return validator ? validator(value) : QString{}; }
FormRule FormRule::required(QString message) {
    return { [message = std::move(message)](const QVariant& value) {
        if (!value.isValid() || value.isNull()) return message;
        if (value.metaType().id() == QMetaType::QString && value.toString().trimmed().isEmpty()) return message;
        return QString{};
    }};
}
FormRule FormRule::matches(QRegularExpression expression, QString message) {
    return {[expression = std::move(expression), message = std::move(message)](const QVariant& value) {
        return expression.match(value.toString()).hasMatch() ? QString{} : message;
    }};
}
FormRule FormRule::custom(Validator validator) { return {std::move(validator)}; }

FormItem::FormItem(QString label, QWidget* editor, QWidget* parent)
    : View(parent), label_(std::move(label)), editor_(editor) {
    setFocusPolicy(Qt::StrongFocus);
    if (editor_) { editor_->setParent(this); setFocusProxy(editor_); }
}
void FormItem::setLabel(QString label) { if (label_ != label) { label_ = std::move(label); updateGeometry(); requestUpdate(); } }
void FormItem::setRules(std::vector<FormRule> rules) { rules_ = std::move(rules); }
bool FormItem::validate() {
    QVariant value;
    if (auto* line = qobject_cast<QLineEdit*>(editor_)) value = line->text();
    else if (editor_) value = editor_->property("value");
    QString result;
    for (const auto& rule : rules_) { result = rule.validate(value); if (!result.isEmpty()) break; }
    if (error_ != result) { error_ = result; requestUpdate(); emit validationChanged(error_.isEmpty(), error_); }
    return error_.isEmpty();
}
void FormItem::clearError() { if (!error_.isEmpty()) { error_.clear(); requestUpdate(); emit validationChanged(true, {}); } }
void FormItem::measureOverride(MeasureSpec w, MeasureSpec h) { const qreal width = w.mode() == MeasureSpec::Mode::Unspecified ? 280 : w.size(); setMeasuredSize({w.resolve(width), h.resolve(80)}); }
void FormItem::arrangeOverride(const QRectF& r) { if (editor_) editor_->setGeometry(QRect(qRound(r.left()), qRound(r.top()+24), qRound(r.width()), 36)); }
void FormItem::paintContent(QPainter& p, const QRectF& r) {
    const auto& c = Theme::forWidget(this).colours(); p.setPen(c.onSurface); p.drawText(QPointF(r.left(),r.top()+16),label_);
    if (!error_.isEmpty()) { p.setPen(c.error); p.drawText(QRectF(r.left(),r.bottom()-16,r.width(),16),Qt::AlignLeft|Qt::AlignVCenter,error_); }
}
Form::Form(QWidget* parent) : ViewGroup(parent) { setFocusPolicy(Qt::NoFocus); }
void Form::addItem(FormItem* item) { addView(item); }
bool Form::validate() { bool valid=true; for (View* v : views()) if (auto* item=qobject_cast<FormItem*>(v)) valid = item->validate() && valid; emit validationChanged(valid); return valid; }
void Form::clearErrors() { for (View* v : views()) if (auto* item=qobject_cast<FormItem*>(v)) item->clearError(); }
void Form::measureOverride(MeasureSpec w, MeasureSpec h) {
    qreal height=0, width=0; const auto children=views();
    for (View* child:children) { child->measure(MeasureSpec::atMost(w.mode()==MeasureSpec::Mode::Unspecified?1e9:w.size()),MeasureSpec::unspecified()); height+=child->measuredSize().height(); width=std::max(width,child->measuredSize().width()); }
    if (children.size()>1) height+=spacing_*(children.size()-1); setMeasuredSize({w.resolve(width),h.resolve(height)});
}
void Form::arrangeChildren(const QRectF& r) { qreal y=r.top(); for (View* child:views()) { const qreal h=child->measuredSize().height(); child->arrange(QRect(qRound(r.left()),qRound(y),qRound(r.width()),qRound(h))); y+=h+spacing_; } }
} // namespace md
