#pragma once

#include "domain/models.h"

#include <QAbstractTableModel>
#include <QHash>

namespace dm {

class LatestDataModel final : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit LatestDataModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    void updateSamples(const QList<TelemetrySample> &samples);
    const QList<TelemetrySample> &samples() const;

private:
    QList<TelemetrySample> m_samples;
    QHash<int, int> m_rowByPoint;
};

} // namespace dm
