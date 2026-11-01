#include "ClipboardListModel.h"

#include <QRegularExpression>

#include <utility>

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

QString secretKindLabel(const QString& kind)
{
    return QString(kind).replace(QLatin1Char('_'), QLatin1Char(' '));
}

QString secretMask()
{
    return QString(12, QChar(0x2022));
}

QString sourceLineOf(const Provenance& source)
{
    const QString app = source.app.isEmpty() ? source.windowClass : source.app;
    const QString detail = source.path.isEmpty() ? source.title : source.path;
    if (app.isEmpty() || detail.isEmpty())
        return app.isEmpty() ? detail : app;
    return QStringLiteral("%1  \u00b7  %2").arg(app, detail);
}

ClipboardListModel::ClipboardListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void ClipboardListModel::setItems(QList<ClipboardItem> items)
{
    beginResetModel();
    m_items = std::move(items);
    // a reveal never outlives the row it was made on
    QSet<qint64> still;
    for (const ClipboardItem& item : m_items) {
        if (m_revealed.contains(item.id))
            still.insert(item.id);
    }
    m_revealed = std::move(still);
    endResetModel();
}

void ClipboardListModel::setRevealed(qint64 id, bool revealed)
{
    const int row = rowForId(id);
    if (row < 0 || m_items.at(row).secretKind.isEmpty())
        return;
    if (revealed)
        m_revealed.insert(id);
    else
        m_revealed.remove(id);
    emit dataChanged(index(row, 0), index(row, 0));
}

void ClipboardListModel::clearRevealed()
{
    const QSet<qint64> was = std::exchange(m_revealed, { });
    for (const qint64 id : was) {
        const int row = rowForId(id);
        if (row >= 0)
            emit dataChanged(index(row, 0), index(row, 0));
    }
}

void ClipboardListModel::setRemaining(QHash<qint64, int> remaining)
{
    m_remaining = std::move(remaining);
    if (!m_items.isEmpty())
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0), { RemainingRole });
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
    const bool masked = !item.secretKind.isEmpty() && !m_revealed.contains(item.id);
    // path and title can carry the secret itself so only the app shows while masked
    Provenance source = item.source;
    if (masked) {
        source.path.clear();
        source.title.clear();
    }
    switch (role) {
    case IdRole:        return item.id;
    case ContentRole:   return item.content;
    case PreviewRole:
    case Qt::DisplayRole: return masked ? secretMask() : previewOf(item.content);
    case SecretKindRole: return item.secretKind;
    case RemainingRole: return m_remaining.value(item.id, -1);
    case RevealedRole:  return m_revealed.contains(item.id);
    case TypeRole:      return static_cast<int>(item.type);
    case PinnedRole:    return item.pinned;
    case CreatedAtRole: return item.createdAt;
    case SourceRole:    return sourceLineOf(source);
    case SourceAppRole: return source.app.isEmpty() ? source.windowClass : source.app;
    case SourceDetailRole: return source.path.isEmpty() ? source.title : source.path;
    case Qt::AccessibleTextRole:
        return QStringLiteral("%1, %2, %3")
            .arg(masked ? tr("hidden %1").arg(secretKindLabel(item.secretKind)) : toString(item.type),
                 masked ? tr("masked") : previewOf(item.content),
                 relativeTime(item.createdAt, QDateTime::currentDateTimeUtc()));
    default:
        return {};
    }
}

} // namespace clipvault
