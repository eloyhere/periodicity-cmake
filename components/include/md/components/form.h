#pragma once

#include <md/view/view_group.h>

#include <QRegularExpression>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <functional>
#include <vector>

class QPainter;
class QRectF;

namespace md {

struct FormRule
{
    using Validator = std::function<QString(const QVariant&)>;
    Validator validator;

    [[nodiscard]] QString validate(const QVariant& value) const;
    [[nodiscard]] static FormRule required(QString message = QStringLiteral("This field is required"));
    [[nodiscard]] static FormRule matches(QRegularExpression expression,
                                          QString message = QStringLiteral("Invalid format"));
    [[nodiscard]] static FormRule custom(Validator validator);
};

class FormItem final : public View
{
    Q_OBJECT
public:
    explicit FormItem(QString label, QWidget* editor, QWidget* parent = nullptr);
    [[nodiscard]] QString label() const { return label_; }
    void setLabel(QString label);
    [[nodiscard]] QWidget* editor() const noexcept { return editor_; }
    void setRules(std::vector<FormRule> rules);
    [[nodiscard]] const std::vector<FormRule>& rules() const noexcept { return rules_; }
    [[nodiscard]] bool validate();
    [[nodiscard]] QString errorText() const { return error_; }
    void clearError();
signals:
    void validationChanged(bool valid, const QString& error);
protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void paintContent(QPainter& painter, const QRectF& content) override;
    void arrangeOverride(const QRectF& content) override;
private:
    QString label_;
    QWidget* editor_ = nullptr;
    std::vector<FormRule> rules_;
    QString error_;
};

class Form final : public ViewGroup
{
    Q_OBJECT
public:
    explicit Form(QWidget* parent = nullptr);
    void addItem(FormItem* item);
    [[nodiscard]] bool validate();
    void clearErrors();
signals:
    void validationChanged(bool valid);
protected:
    void measureOverride(MeasureSpec width, MeasureSpec height) override;
    void arrangeChildren(const QRectF& content) override;
private:
    qreal spacing_ = 12.0;
};

} // namespace md
