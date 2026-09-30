#include "ui/datetimepicker.h"

#include <QApplication>
#include <QCalendarWidget>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace dm {

DateTimePicker::DateTimePicker(const QDateTime &dateTime, bool endOfMinute, QWidget *parent)
    : QWidget(parent), m_dateTime(dateTime), m_endOfMinute(endOfMinute)
{
    setObjectName(QStringLiteral("dateTimePicker"));
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_display = new QLineEdit(this);
    m_display->setReadOnly(true);
    m_display->setFocusPolicy(Qt::NoFocus);
    m_display->setCursor(Qt::PointingHandCursor);
    m_display->installEventFilter(this);
    m_dropButton = new QToolButton(this);
    m_dropButton->setObjectName(QStringLiteral("dateTimeDropButton"));
    m_dropButton->setText(QStringLiteral("▾"));
    m_dropButton->setFocusPolicy(Qt::NoFocus);
    m_dropButton->setCursor(Qt::PointingHandCursor);
    m_dropButton->setFixedWidth(26);
    layout->addWidget(m_display, 1);
    layout->addWidget(m_dropButton);

    const int minimumWidth = m_display->fontMetrics().horizontalAdvance(QStringLiteral("2099-12-31 23:59")) + 64;
    setMinimumWidth(minimumWidth);

    m_popup = new QFrame(this, Qt::Popup);
    m_popup->setObjectName(QStringLiteral("dateTimePickerPopup"));
    auto *popupLayout = new QVBoxLayout(m_popup);
    popupLayout->setContentsMargins(10, 10, 10, 10);
    popupLayout->setSpacing(10);
    m_calendar = new QCalendarWidget(m_popup);
    const int currentYear = QDate::currentDate().year();
    m_calendar->setDateRange(QDate(2000, 1, 1), QDate(currentYear + 10, 12, 31));
    popupLayout->addWidget(m_calendar);
    setupYearSelector();

    auto *timeRow = new QHBoxLayout;
    auto *timeLabel = new QLabel(QStringLiteral("时间"), m_popup);
    m_hourCombo = new QComboBox(m_popup);
    m_minuteCombo = new QComboBox(m_popup);
    m_hourCombo->setMinimumWidth(76);
    m_minuteCombo->setMinimumWidth(76);
    m_hourCombo->setMaxVisibleItems(12);
    m_minuteCombo->setMaxVisibleItems(12);
    for (int hour = 0; hour < 24; ++hour)
        m_hourCombo->addItem(QStringLiteral("%1").arg(hour, 2, 10, QLatin1Char('0')), hour);
    for (int minute = 0; minute < 60; ++minute)
        m_minuteCombo->addItem(QStringLiteral("%1").arg(minute, 2, 10, QLatin1Char('0')), minute);
    timeRow->addWidget(timeLabel);
    timeRow->addWidget(m_hourCombo);
    timeRow->addWidget(new QLabel(QStringLiteral("时"), m_popup));
    timeRow->addWidget(m_minuteCombo);
    timeRow->addWidget(new QLabel(QStringLiteral("分"), m_popup));
    timeRow->addStretch();
    popupLayout->addLayout(timeRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, m_popup);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    popupLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &DateTimePicker::applySelection);
    connect(buttons, &QDialogButtonBox::rejected, m_popup, &QWidget::hide);
    connect(m_dropButton, &QToolButton::clicked, this, &DateTimePicker::showPopup);
    updateDisplayText();
}

QDateTime DateTimePicker::dateTime() const { return m_dateTime; }

void DateTimePicker::setDateTime(const QDateTime &dateTime)
{
    if (!dateTime.isValid()) return;
    m_dateTime = dateTime;
    updateDisplayText();
}

bool DateTimePicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_display && event->type() == QEvent::MouseButtonPress) {
        showPopup();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void DateTimePicker::showPopup()
{
    m_calendar->setSelectedDate(m_dateTime.date());
    m_calendar->setCurrentPage(m_dateTime.date().year(), m_dateTime.date().month());
    m_hourCombo->setCurrentIndex(m_hourCombo->findData(m_dateTime.time().hour()));
    m_minuteCombo->setCurrentIndex(m_minuteCombo->findData(m_dateTime.time().minute()));
    const int yearIndex = m_yearCombo->findData(m_dateTime.date().year());
    if (yearIndex >= 0) m_yearCombo->setCurrentIndex(yearIndex);
    m_popup->adjustSize();

    QPoint position = mapToGlobal(QPoint(0, height() + 4));
    const QRect available = screen()->availableGeometry();
    if (position.x() + m_popup->width() > available.right())
        position.setX(available.right() - m_popup->width());
    if (position.y() + m_popup->height() > available.bottom())
        position.setY(mapToGlobal(QPoint(0, -m_popup->height() - 4)).y());
    m_popup->move(position);
    m_popup->show();
    m_popup->raise();
}

void DateTimePicker::applySelection()
{
    const int second = m_endOfMinute ? 59 : 0;
    const int millisecond = m_endOfMinute ? 999 : 0;
    const QTime time(m_hourCombo->currentData().toInt(), m_minuteCombo->currentData().toInt(),
                     second, millisecond);
    m_dateTime = QDateTime(m_calendar->selectedDate(), time);
    updateDisplayText();
    m_popup->hide();
    emit dateTimeChanged(m_dateTime);
}

void DateTimePicker::updateDisplayText()
{
    m_display->setText(m_dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

void DateTimePicker::setupYearSelector()
{
    auto *yearButton = m_calendar->findChild<QToolButton *>(QStringLiteral("qt_calendar_yearbutton"));
    auto *yearEditor = m_calendar->findChild<QSpinBox *>(QStringLiteral("qt_calendar_yearedit"));
    auto *navigationBar = m_calendar->findChild<QWidget *>(QStringLiteral("qt_calendar_navigationbar"));
    auto *navigationLayout = navigationBar ? qobject_cast<QHBoxLayout *>(navigationBar->layout()) : nullptr;
    if (!navigationLayout) return;

    m_yearCombo = new QComboBox(navigationBar);
    m_yearCombo->setObjectName(QStringLiteral("calendarYearCombo"));
    m_yearCombo->setFixedWidth(96);
    m_yearCombo->setMaxVisibleItems(12);
    const int maximumYear = QDate::currentDate().year() + 10;
    for (int year = 2000; year <= maximumYear; ++year)
        m_yearCombo->addItem(QStringLiteral("%1 年").arg(year), year);

    int insertIndex = -1;
    if (yearButton) {
        insertIndex = navigationLayout->indexOf(yearButton);
        yearButton->hide();
    }
    if (yearEditor) yearEditor->hide();
    if (insertIndex >= 0) navigationLayout->insertWidget(insertIndex, m_yearCombo);
    else navigationLayout->addWidget(m_yearCombo);

    connect(m_yearCombo, &QComboBox::currentIndexChanged, m_calendar, [this](int index) {
        if (index < 0) return;
        const int year = m_yearCombo->itemData(index).toInt();
        const QDate selected = m_calendar->selectedDate();
        const int day = qMin(selected.day(), QDate(year, selected.month(), 1).daysInMonth());
        m_calendar->setSelectedDate(QDate(year, selected.month(), day));
        m_calendar->setCurrentPage(year, selected.month());
    });
    connect(m_calendar, &QCalendarWidget::currentPageChanged, m_yearCombo, [this](int year, int) {
        const int index = m_yearCombo->findData(year);
        if (index < 0 || index == m_yearCombo->currentIndex()) return;
        QSignalBlocker blocker(m_yearCombo);
        m_yearCombo->setCurrentIndex(index);
    });
}

} // namespace dm
