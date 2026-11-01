#pragma once

#include "../core/ClipboardItem.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>

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
        SecretKindRole,
        // seconds until an in memory secret is wiped or minus one
        RemainingRole,
        RevealedRole,
    };

    explicit ClipboardListModel(QObject* parent = nullptr);

    void setItems(QList<ClipboardItem> items);
    const QList<ClipboardItem>& items() const { return m_items; }
    std::optional<ClipboardItem> itemAt(const QModelIndex& index) const;
    // Returns minus one when the id is absent
    int rowForId(qint64 id) const;

    // secrets stay masked until revealed one row at a time
    void setRevealed(qint64 id, bool revealed);
    [[nodiscard]] bool isRevealed(qint64 id) const { return m_revealed.contains(id); }
    void clearRevealed();
    void setRemaining(QHash<qint64, int> remaining);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;

private:
    QList<ClipboardItem> m_items;
    QSet<qint64> m_revealed;
    QHash<qint64, int> m_remaining;
};

// Collapses an entry to one readable line
QString previewOf(const QString& content);

QString relativeTime(const QDateTime& when, const QDateTime& now);

// readable form of a secret rule id such as github token
QString secretKindLabel(const QString& kind);

// fixed width so the mask never hints at the length
QString secretMask();

} // namespace clipvault
