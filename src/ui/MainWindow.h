#pragma once

#include <QList>
#include <QString>
#include <QTimer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QStackedWidget;
class QToolButton;

namespace clipvault {

class ClipboardListModel;
class HistoryService;
class ProvenanceService;
class SearchService;
class SemanticIndex;
class SettingsService;

// Doubles as the quick pick popup opened by the shortcut
class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(HistoryService& history, SearchService& search, SettingsService& settings,
               QWidget* parent = nullptr);

    // Positioned near the pointer and closed on focus loss
    void showAsPopup();
    void showNormalWindow();

    // Re-runs the current query after history changes
    void refresh();

    void setCapturePaused(bool paused);
    // Opened from the tray menu and the header button
    void openSettings();
    void confirmClearHistory();
    // Strings owned by the controller and shown in settings
    void setHotkeyNote(const QString& note) { m_hotkeyNote = note; }
    // False when nothing can reopen the window
    void setBackgroundReachable(bool reachable) { m_backgroundReachable = reachable; }
    void setDatabasePath(const QString& path) { m_databasePath = path; }
    // optional and the source details stay unavailable without it
    void setProvenanceService(ProvenanceService* provenance) { m_provenance = provenance; }
    // Content free message shown briefly in the footer
    void flashStatus(const QString& message);
    // semantic hits are appended once they arrive for the current text
    void setSemanticIndex(SemanticIndex* index);

signals:
    // The controller owns writing the choice to the clipboard
    void itemChosen(const QString& content);
    // sent just before item chosen when the entry is a detected secret
    void secretChosen(qint64 id, const QString& kind);
    // id of the item just written back so its paste target can be recorded
    void itemCopiedOut(qint64 id);
    void pauseToggleRequested();
    void quitRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void applyPaletteStyling();
    void updateEmptyState();
    void updateFooter();
    void moveSelection(int delta);
    void activateCurrent();
    void deleteCurrent();
    void togglePinCurrent();
    void toggleRevealCurrent();
    void updateCountdown();
    void positionNearPointer();
    void showSourceDetails();
    void filterByApp(const QString& app);
    void updateTitleNote();

    HistoryService& m_history;
    SearchService& m_search;
    SettingsService& m_settings;
    ProvenanceService* m_provenance = nullptr;

    ClipboardListModel* m_model = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListView* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QLabel* m_pausedBanner = nullptr;
    QLabel* m_titleNote = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_settingsButton = nullptr;
    QTimer m_countdown;

    SemanticIndex* m_semantic = nullptr;
    QString m_semanticQuery;
    QList<qint64> m_semanticIds;

    QString m_hotkeyNote;
    QString m_databasePath;
    bool m_backgroundReachable = true;
    bool m_paused = false;
    bool m_popupMode = false;
    // Armed only once the popup has held focus see changeEvent
    bool m_hideOnDeactivate = false;
};

} // namespace clipvault
