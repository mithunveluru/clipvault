#pragma once

#include "../core/ClipboardItem.h"

#include <QAbstractListModel>
#include <QList>

namespace clipvault {

// Read only view over a supplied result set
class ClipboardListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        ContentRole,
        PreviewRole,
        TypeRole,
        PinnedRole,
        CreatedAtRole,
    };

    explicit ClipboardListModel(QObject* parent = nullptr);

    void setItems(QList<ClipboardItem> items);
    const QList<ClipboardItem>& items() const { return m_items; }
    std::optional<ClipboardItem> itemAt(const QModelIndex& index) const;
    // Returns minus one when the id is absent
    int rowForId(qint64 id) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;

private:
    QList<ClipboardItem> m_items;
};

// Collapses an entry to one readable line
QString previewOf(const QString& content);

QString relativeTime(const QDateTime& when, const QDateTime& now);

} // namespace clipvault
