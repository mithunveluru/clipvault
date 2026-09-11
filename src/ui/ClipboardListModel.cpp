#include "ClipboardListModel.h"

#include <QRegularExpression>

namespace clipvault {

QString previewOf(const QString& content)
{
    // First line carrying anything visible becomes the preview
    static const QRegularExpression whitespace(QStringLiteral("[\\s\\x{00a0}]+"));
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString collapsed = line.simplified();
        if (!collapsed.isEmpty()) {
            QString preview = collapsed;
            preview.replace(whitespace, QStringLiteral(" "));
            // Truncated so the painter never measures a megabyte
            return preview.left(512);
        }
    }
    return content.simplified().left(512);
}

QString relativeTime(const QDateTime& when, const QDateTime& now)
{
    const qint64 seconds = when.secsTo(now);
    if (seconds < 0)
        return QObject::tr("just now");
    if (seconds < 45)
        return QObject::tr("just now");
    if (seconds < 90)
        return QObject::tr("a minute ago");

    const qint64 minutes = seconds / 60;
    if (minutes < 60)
        return minutes == 1 ? QObject::tr("a minute ago") : QObject::tr("%1 minutes ago").arg(minutes);

    const qint64 hours = minutes / 60;
    if (hours < 24)
        return hours == 1 ? QObject::tr("an hour ago") : QObject::tr("%1 hours ago").arg(hours);

    const qint64 days = hours / 24;
    if (days < 30)
        return days == 1 ? QObject::tr("yesterday") : QObject::tr("%1 days ago").arg(days);

    return when.toLocalTime().toString(QStringLiteral("d MMM yyyy"));
}

ClipboardListModel::ClipboardListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void ClipboardListModel::setItems(QList<ClipboardItem> items)
{
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
}

std::optional<ClipboardItem> ClipboardListModel::itemAt(const QModelIndex& index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return std::nullopt;
    return m_items.at(index.row());
}

int ClipboardListModel::rowForId(qint64 id) const
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items.at(row).id == id)
            return row;
    }
    return -1;
}

int ClipboardListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant ClipboardListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const ClipboardItem& item = m_items.at(index.row());
    switch (role) {
    case IdRole:        return item.id;
    case ContentRole:   return item.content;
    case PreviewRole:
    case Qt::DisplayRole: return previewOf(item.content);
    case TypeRole:      return static_cast<int>(item.type);
    case PinnedRole:    return item.pinned;
    case CreatedAtRole: return item.createdAt;
    case Qt::AccessibleTextRole:
        return QStringLiteral("%1, %2, %3")
            .arg(toString(item.type), previewOf(item.content),
                 relativeTime(item.createdAt, QDateTime::currentDateTimeUtc()));
    default:
        return {};
    }
}

} // namespace clipvault
