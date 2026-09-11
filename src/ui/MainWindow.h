#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QStackedWidget;
class QToolButton;

namespace clipvault {

class ClipboardListModel;
class HistoryService;
class SearchService;
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
    // Content free message shown briefly in the footer
    void flashStatus(const QString& message);

signals:
    // The controller owns writing the choice to the clipboard
    void itemChosen(const QString& content);
    void pauseToggleRequested();
    void quitRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void applyPaletteStyling();
    void updateEmptyState();
    void updateFooter();
    void moveSelection(int delta);
    void activateCurrent();
    void deleteCurrent();
    void togglePinCurrent();
    void positionNearPointer();

    HistoryService& m_history;
    SearchService& m_search;
    SettingsService& m_settings;

    ClipboardListModel* m_model = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QListView* m_list = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QLabel* m_pausedBanner = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_settingsButton = nullptr;

    QString m_hotkeyNote;
    QString m_databasePath;
    bool m_backgroundReachable = true;
    bool m_paused = false;
    bool m_popupMode = false;
    // Armed only once the popup has held focus see changeEvent
    bool m_hideOnDeactivate = false;
};

} // namespace clipvault
