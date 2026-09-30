#pragma once

#include <QDateTime>
#include <QWidget>

class QCalendarWidget;
class QComboBox;
class QFrame;
class QLineEdit;
class QToolButton;

namespace dm {

class DateTimePicker final : public QWidget
{
    Q_OBJECT
public:
    explicit DateTimePicker(const QDateTime &dateTime, bool endOfMinute = false,
                            QWidget *parent = nullptr);

    QDateTime dateTime() const;
    void setDateTime(const QDateTime &dateTime);

signals:
    void dateTimeChanged(const QDateTime &dateTime);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void showPopup();
    void applySelection();

private:
    void setupYearSelector();
    void updateDisplayText();

    QDateTime m_dateTime;
    bool m_endOfMinute = false;
    QLineEdit *m_display = nullptr;
    QToolButton *m_dropButton = nullptr;
    QFrame *m_popup = nullptr;
    QCalendarWidget *m_calendar = nullptr;
    QComboBox *m_yearCombo = nullptr;
    QComboBox *m_hourCombo = nullptr;
    QComboBox *m_minuteCombo = nullptr;
};

} // namespace dm
