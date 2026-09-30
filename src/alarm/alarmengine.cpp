#include "alarm/alarmengine.h"

namespace dm {

AlarmEngine::AlarmEngine(QObject *parent) : QObject(parent) {}

void AlarmEngine::setRules(const QList<AlarmRule> &rules)
{
    m_rulesByPoint.clear();
    for (const auto &rule : rules) {
        if (rule.enabled)
            m_rulesByPoint.insert(rule.pointId, rule);
    }
}

QList<AlarmRule> AlarmEngine::rules() const { return m_rulesByPoint.values(); }
QList<AlarmEvent> AlarmEngine::activeAlarms() const { return m_activeByRule.values(); }

void AlarmEngine::evaluate(const TelemetrySample &sample)
{
    const auto it = m_rulesByPoint.constFind(sample.pointId);
    if (it == m_rulesByPoint.cend() || sample.quality == DataQuality::Bad)
        return;

    const AlarmRule &rule = it.value();
    const bool violated = sample.value < rule.lowLimit || sample.value > rule.highLimit;
    const bool alreadyActive = m_activeByRule.contains(rule.id);

    if (violated && !alreadyActive) {
        AlarmEvent event;
        event.ruleId = rule.id;
        event.deviceId = sample.deviceId;
        event.pointId = sample.pointId;
        event.deviceName = sample.deviceName;
        event.pointName = sample.pointName;
        event.value = sample.value;
        event.state = AlarmState::Active;
        event.triggeredAt = sample.timestamp;
        event.message = QStringLiteral("%1 超出范围 [%2, %3]")
                            .arg(sample.pointName)
                            .arg(rule.lowLimit, 0, 'f', 1)
                            .arg(rule.highLimit, 0, 'f', 1);
        m_activeByRule.insert(rule.id, event);
        emit alarmChanged(event);
    } else if (!violated && alreadyActive) {
        AlarmEvent event = m_activeByRule.take(rule.id);
        event.value = sample.value;
        event.state = AlarmState::Recovered;
        event.recoveredAt = sample.timestamp;
        event.message = QStringLiteral("%1 已恢复正常").arg(sample.pointName);
        emit alarmChanged(event);
    }
}

void AlarmEngine::acknowledge(int ruleId)
{
    auto it = m_activeByRule.find(ruleId);
    if (it == m_activeByRule.end() || it->acknowledged)
        return;
    it->acknowledged = true;
    it->state = AlarmState::Acknowledged;
    emit alarmChanged(it.value());
}

} // namespace dm
