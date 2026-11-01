#include "ClipboardMonitor.h"

#include "../core/Logging.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace clipvault {
namespace {

// Identical text inside this window counts as one copy
constexpr int kIdenticalRepeatMs = 1000;

// Hints set by password managers to opt out of history
bool isMarkedSensitive(const QMimeData* mime)
{
    if (!mime)
        return false;
    if (mime->hasFormat(QStringLiteral("x-kde-passwordManagerHint")))
        return mime->data(QStringLiteral("x-kde-passwordManagerHint")).trimmed()
            == QByteArrayLiteral("secret");
    return mime->hasFormat(QStringLiteral("org.nspasteboard.ConcealedType"));
}

} // namespace

ClipboardMonitor::ClipboardMonitor(QObject* parent)
    : QObject(parent)
    , m_clipboard(QGuiApplication::clipboard())
{
    if (!m_clipboard) {
        qCCritical(lcClip) << "system clipboard is unavailable; capture disabled";
        return;
    }
    connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardMonitor::onClipboardChanged);
}

void ClipboardMonitor::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    qCInfo(lcClip) << (paused ? "capture paused" : "capture resumed");
    emit pausedChanged(m_paused);
}

void ClipboardMonitor::writeBack(const QString& text)
{
    if (!m_clipboard) {
        qCWarning(lcClip) << "cannot write back, clipboard unavailable";
        return;
    }
    m_selfWrittenText = text;
    m_hasSelfWrite = true;
    // Also the current text so a re-announcement is ignored
    m_lastCapturedText = text;
    m_sinceLastCapture.restart();
    m_clipboard->setText(text, QClipboard::Clipboard);
    qCDebug(lcClip) << "clipboard updated from history" << redact(text);
}

void ClipboardMonitor::clearIfHolds(const QString& text)
{
    // drop our own references so the caller can wipe the only copy
    if (m_lastCapturedText == text)
        m_lastCapturedText.clear();
    if (m_selfWrittenText == text) {
        m_selfWrittenText.clear();
        m_hasSelfWrite = false;
    }
    if (!m_clipboard || m_clipboard->text(QClipboard::Clipboard) != text)
        return;
    m_clipboard->clear(QClipboard::Clipboard);
    qCInfo(lcClip) << "cleared an expired secret from the clipboard";
}

void ClipboardMonitor::onClipboardChanged()
{
    if (!m_clipboard)
        return;

    // Compositors briefly report no owner while ownership transfers
    const QMimeData* mime = m_clipboard->mimeData(QClipboard::Clipboard);
    if (!mime)
        return;

    const QString text = m_clipboard->text(QClipboard::Clipboard);

    if (m_hasSelfWrite) {
        m_hasSelfWrite = false;
        if (text == m_selfWrittenText) {
            m_selfWrittenText.clear();
            qCDebug(lcClip) << "ignored own clipboard write";
            return;
        }
        m_selfWrittenText.clear();
    }

    if (m_paused) {
        qCDebug(lcClip) << "clipboard change ignored, capture paused";
        return;
    }

    if (text.isEmpty())
        return;

    if (isMarkedSensitive(mime)) {
        qCInfo(lcClip) << "clipboard change skipped, marked sensitive by the source application";
        return;
    }

    if (text == m_lastCapturedText && m_sinceLastCapture.isValid()
        && m_sinceLastCapture.elapsed() < kIdenticalRepeatMs) {
        qCDebug(lcClip) << "ignored repeat announcement of the current clipboard";
        return;
    }

    m_lastCapturedText = text;
    m_sinceLastCapture.restart();

    qCDebug(lcClip) << "clipboard change detected" << redact(text);
    emit textCaptured(text);
}

} // namespace clipvault
