#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QComboBox;
class QLayout;
class QKeySequenceEdit;
class QSpinBox;
class QVBoxLayout;

namespace clipvault {

class SemanticIndex;
class SettingsService;

// Preferences apply immediately since every one is reversible
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(SettingsService& settings, QString hotkeyNote, QString databasePath,
                   bool capturePaused, QWidget* parent = nullptr);

    // how the desktop reports the focused window shown under source tracking
    void setFocusMechanism(const QString& description);
    // shows the live semantic status while the dialog is open
    void setSemanticIndex(const SemanticIndex& index);

signals:
    void clearHistoryRequested();
    void pauseToggleRequested();
    // sent only after the user confirmed
    void clearProvenanceRequested();

private:
    void buildUi(const QString& hotkeyNote, const QString& databasePath, bool capturePaused);
    void addProvenanceSection(QVBoxLayout* layout, QWidget* parent);
    void fitToContents();
    void buildSemanticGroup(QLayout* root);

    SettingsService& m_settings;
    QCheckBox* m_launchOnStartup = nullptr;
    QKeySequenceEdit* m_hotkeyEdit = nullptr;
    QComboBox* m_retention = nullptr;
    QSpinBox* m_maxItems = nullptr;
    QCheckBox* m_startPaused = nullptr;
    QCheckBox* m_pauseCapture = nullptr;
    QCheckBox* m_secretRadar = nullptr;
    QSpinBox* m_secretTtl = nullptr;
    QCheckBox* m_secretPinnedNeverExpire = nullptr;
    QCheckBox* m_secretCopyBackWarning = nullptr;
    QLineEdit* m_secretRiskyApps = nullptr;
    QCheckBox* m_provenanceEnabled = nullptr;
    QCheckBox* m_provenanceTitles = nullptr;
    QLineEdit* m_provenanceBlocklist = nullptr;
    QLabel* m_focusMechanism = nullptr;
    QLabel* m_semanticStatus = nullptr;
};

} // namespace clipvault
