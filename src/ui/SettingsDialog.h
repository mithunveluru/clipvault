#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QSpinBox;

namespace clipvault {

class SettingsService;

// Preferences apply immediately since every one is reversible
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(SettingsService& settings, QString hotkeyNote, QString databasePath,
                   bool capturePaused, QWidget* parent = nullptr);

signals:
    void clearHistoryRequested();
    void pauseToggleRequested();

private:
    void buildUi(const QString& hotkeyNote, const QString& databasePath, bool capturePaused);

    SettingsService& m_settings;
    QCheckBox* m_launchOnStartup = nullptr;
    QKeySequenceEdit* m_hotkeyEdit = nullptr;
    QComboBox* m_retention = nullptr;
    QSpinBox* m_maxItems = nullptr;
    QCheckBox* m_startPaused = nullptr;
    QCheckBox* m_pauseCapture = nullptr;
};

} // namespace clipvault
