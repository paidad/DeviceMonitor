#pragma once

#include "domain/models.h"

#include <QHash>
#include <QObject>

namespace dm {

class AlarmEngine final : public QObject
{
    Q_OBJECT
public:
    explicit AlarmEngine(QObject *parent = nullptr);

    void setRules(const QList<AlarmRule> &rules);
    QList<AlarmRule> rules() const;
    QList<AlarmEvent> activeAlarms() const;

public slots:
    void evaluate(const TelemetrySample &sample);
    void acknowledge(int ruleId);

signals:
    void alarmChanged(const dm::AlarmEvent &event);

private:
    QHash<int, AlarmRule> m_rulesByPoint;
    QHash<int, AlarmEvent> m_activeByRule;
};

} // namespace dm
