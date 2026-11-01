#include "EphemeralStore.h"

#include "../core/SecureZero.h"

#include <QDateTime>

#include <cmath>

namespace clipvault {

EphemeralStore::EphemeralStore(QObject* parent)
    : QObject(parent)
    , m_wipe([](QString& text) { secureZero(text); })
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &EphemeralStore::expireDue);
}

EphemeralStore::~EphemeralStore()
{
    for (Entry& entry : m_entries)
        m_wipe(entry.item.content);
}

EphemeralStore::Added EphemeralStore::add(const QString& content, const QString& kind, ClipboardType type,
    const Provenance& source, std::chrono::milliseconds ttl)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).item.content != content)
            continue;
        Entry entry = m_entries.takeAt(i);
        entry.deadline.setRemainingTime(ttl);
        entry.item.createdAt = now;
        entry.item.expiresAt = now.addMSecs(ttl.count());
        const qint64 id = entry.item.id;
        m_entries.prepend(std::move(entry));
        schedule();
        emit changed();
        return { id, false };
    }

    Entry entry;
    // a private copy so the wipe reaches the only buffer this store owns
    entry.item.content = QString(content.constData(), content.size());
    entry.item.id = m_nextId--;
    entry.item.type = type;
    entry.item.secretKind = kind;
    entry.item.createdAt = now;
    entry.item.expiresAt = now.addMSecs(ttl.count());
    entry.item.source = source;
    entry.deadline.setRemainingTime(ttl);
    const qint64 id = entry.item.id;
    m_entries.prepend(std::move(entry));
    schedule();
    emit changed();
    return { id, true };
}

QList<ClipboardItem> EphemeralStore::items() const
{
    QList<ClipboardItem> out;
    out.reserve(m_entries.size());
    for (const Entry& entry : m_entries)
        out.append(entry.item);
    return out;
}

std::optional<ClipboardItem> EphemeralStore::find(qint64 id) const
{
    const qsizetype index = indexOf(id);
    if (index < 0)
        return std::nullopt;
    return m_entries.at(index).item;
}

int EphemeralStore::remainingSeconds(qint64 id) const
{
    const qsizetype index = indexOf(id);
    if (index < 0)
        return -1;
    const qint64 ms = m_entries.at(index).deadline.remainingTime();
    return static_cast<int>(std::ceil(static_cast<double>(ms) / 1000.0));
}

std::optional<ClipboardItem> EphemeralStore::take(qint64 id)
{
    const qsizetype index = indexOf(id);
    if (index < 0)
        return std::nullopt;
    ClipboardItem item = std::move(m_entries[index].item);
    m_entries.removeAt(index);
    schedule();
    emit changed();
    return item;
}

bool EphemeralStore::remove(qint64 id)
{
    std::optional<ClipboardItem> item = take(id);
    if (!item)
        return false;
    m_wipe(item->content);
    return true;
}

void EphemeralStore::clear()
{
    if (m_entries.isEmpty())
        return;
    QList<Entry> gone = std::move(m_entries);
    m_entries.clear();
    m_timer.stop();
    emit changed();
    for (Entry& entry : gone)
        m_wipe(entry.item.content);
}

void EphemeralStore::clearSourceTitles()
{
    for (Entry& entry : m_entries)
        entry.item.source.title.clear();
    emit changed();
}

qsizetype EphemeralStore::indexOf(qint64 id) const
{
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).item.id == id)
            return i;
    }
    return -1;
}

void EphemeralStore::expireDue()
{
    QList<Entry> gone;
    for (qsizetype i = m_entries.size() - 1; i >= 0; --i) {
        if (m_entries.at(i).deadline.hasExpired())
            gone.append(m_entries.takeAt(i));
    }
    schedule();
    if (gone.isEmpty())
        return;

    if (m_clearClipboard) {
        for (const Entry& entry : gone)
            m_clearClipboard(entry.item.content);
    }
    // listeners drop their copies first so the wipe below hits an unshared buffer
    emit changed();
    for (Entry& entry : gone)
        m_wipe(entry.item.content);
}

void EphemeralStore::schedule()
{
    if (m_entries.isEmpty()) {
        m_timer.stop();
        return;
    }
    qint64 soonest = m_entries.first().deadline.remainingTime();
    for (const Entry& entry : m_entries)
        soonest = qMin(soonest, entry.deadline.remainingTime());
    m_timer.start(static_cast<int>(qMax<qint64>(0, soonest)));
}

} // namespace clipvault
