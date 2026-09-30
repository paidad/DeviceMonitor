#include "ui/latestdatamodel.h"

#include <QBrush>

namespace dm {

LatestDataModel::LatestDataModel(QObject *parent) : QAbstractTableModel(parent) {}
int LatestDataModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_samples.size(); }
int LatestDataModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : 6; }

QVariant LatestDataModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_samples.size()) return {};
    const auto &s = m_samples.at(index.row());
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0: return s.deviceName;
        case 1: return s.pointName;
        case 2: return QString::number(s.value, 'f', 2);
        case 3: return s.unit;
        case 4: return qualityText(s.quality);
        case 5: return s.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
    }
    if (role == Qt::ForegroundRole && s.quality != DataQuality::Good)
        return QBrush(QColor(QStringLiteral("#ef4444")));
    if (role == Qt::TextAlignmentRole && index.column() == 2)
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QVariant LatestDataModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QStringList headers = {QStringLiteral("设备"), QStringLiteral("测点"), QStringLiteral("数值"),
                                        QStringLiteral("单位"), QStringLiteral("质量"), QStringLiteral("更新时间")};
    return headers.value(section);
}

void LatestDataModel::updateSamples(const QList<TelemetrySample> &samples)
{
    for (const auto &sample : samples) {
        if (m_rowByPoint.contains(sample.pointId)) {
            const int row = m_rowByPoint.value(sample.pointId);
            m_samples[row] = sample;
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        } else {
            const int row = m_samples.size();
            beginInsertRows({}, row, row);
            m_rowByPoint.insert(sample.pointId, row);
            m_samples.append(sample);
            endInsertRows();
        }
    }
}

const QList<TelemetrySample> &LatestDataModel::samples() const { return m_samples; }

} // namespace dm
