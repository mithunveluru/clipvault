#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QClipboard;

namespace clipvault {

// Reports new clipboard text and stays on the GUI thread
class ClipboardMonitor : public QObject {
    Q_OBJECT
public:
    explicit ClipboardMonitor(QObject* parent = nullptr);

    bool isPaused() const { return m_paused; }
    void setPaused(bool paused);

    // Writes text and suppresses the resulting change event
    void writeBack(const QString& text);

    // clears the clipboard only while it still holds exactly this text
    void clearIfHolds(const QString& text);

signals:
    void textCaptured(const QString& text);
    void pausedChanged(bool paused);

private:
    void onClipboardChanged();

    QClipboard* m_clipboard = nullptr;
    bool m_paused = false;

    // Matched by content never by timing
    QString m_selfWrittenText;
    bool m_hasSelfWrite = false;

    // Guards against XWayland re-announcing the same selection
    QString m_lastCapturedText;
    QElapsedTimer m_sinceLastCapture;
};

} // namespace clipvault
