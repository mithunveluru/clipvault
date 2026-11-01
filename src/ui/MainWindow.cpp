#include "MainWindow.h"

#include "../core/Logging.h"
#include "../services/EphemeralStore.h"
#include "../services/HistoryService.h"
#include "../services/ProvenanceService.h"
#include "../services/SearchService.h"
#include "../services/SettingsService.h"
#include "ClipboardItemDelegate.h"
#include "ClipboardListModel.h"
#include "ProvenanceDialog.h"
#include "SettingsDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QFrame>
#include <QHash>
#include <QHideEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace clipvault {
namespace {
constexpr int kStatusFlashMs = 4000;
} // namespace

MainWindow::MainWindow(HistoryService& history, SearchService& search, SettingsService& settings,
                       QWidget* parent)
    : QWidget(parent)
    , m_history(history)
    , m_search(search)
    , m_settings(settings)
{
    setWindowTitle(tr("ClipVault"));
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    resize(470, 560);
    setMinimumSize(360, 320);
    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, &MainWindow::updateCountdown);
    buildUi();
    applyPaletteStyling();
    refresh();
}

void MainWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(14, 10, 8, 10);
    auto* title = new QLabel(tr("ClipVault"), this);
    title->setObjectName(QStringLiteral("titleLabel"));
    header->addWidget(title);
    header->addStretch();

    m_settingsButton = new QToolButton(this);
    m_settingsButton->setText(QStringLiteral("⚙"));
    m_settingsButton->setToolTip(tr("Settings"));
    m_settingsButton->setAccessibleName(tr("Settings"));
    m_settingsButton->setAutoRaise(true);
    m_settingsButton->setFocusPolicy(Qt::NoFocus);
    connect(m_settingsButton, &QToolButton::clicked, this, &MainWindow::openSettings);
    header->addWidget(m_settingsButton);
    root->addLayout(header);

    auto* headerRule = new QFrame(this);
    headerRule->setObjectName(QStringLiteral("rule"));
    headerRule->setFrameShape(QFrame::HLine);
    root->addWidget(headerRule);

    auto* searchRow = new QWidget(this);
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(12, 10, 12, 10);
    m_searchEdit = new QLineEdit(searchRow);
    m_searchEdit->setPlaceholderText(tr("Search clipboard…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setAccessibleName(tr("Search clipboard history"));
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::refresh);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::updateTitleNote);
    searchLayout->addWidget(m_searchEdit);
    root->addWidget(searchRow);

    m_titleNote = new QLabel(tr("Window titles are not recorded, so title: matches nothing. "
                                "Turn on \u201cAlso record window titles\u201d in Settings, Privacy."),
                             this);
    m_titleNote->setObjectName(QStringLiteral("footerLabel"));
    m_titleNote->setWordWrap(true);
    m_titleNote->setContentsMargins(14, 0, 14, 8);
    m_titleNote->hide();
    root->addWidget(m_titleNote);
    connect(&m_settings, &SettingsService::changed, this, &MainWindow::updateTitleNote);

    m_pausedBanner = new QLabel(tr("Clipboard capture is paused. Your existing history is still available."), this);
    m_pausedBanner->setObjectName(QStringLiteral("pausedBanner"));
    m_pausedBanner->setWordWrap(true);
    m_pausedBanner->hide();
    root->addWidget(m_pausedBanner);

    m_model = new ClipboardListModel(this);
    m_list = new QListView(this);
    m_list->setModel(m_model);
    auto* delegate = new ClipboardItemDelegate(m_list);
    m_list->setItemDelegate(delegate);
    // rows with a source are one line taller
    m_list->setUniformItemSizes(false);
    connect(delegate, &ClipboardItemDelegate::appChipClicked, this, &MainWindow::filterByApp);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setMouseTracking(true);
    m_list->setAccessibleName(tr("Clipboard history"));
    m_list->installEventFilter(this);
    // activated already covers Enter and double click
    connect(m_list, &QListView::activated, this, &MainWindow::activateCurrent);

    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName(QStringLiteral("emptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_list);
    m_stack->addWidget(m_emptyLabel);
    root->addWidget(m_stack, 1);

    auto* footerRule = new QFrame(this);
    footerRule->setObjectName(QStringLiteral("rule"));
    footerRule->setFrameShape(QFrame::HLine);
    root->addWidget(footerRule);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(14, 7, 14, 8);
    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("footerLabel"));
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("footerLabel"));
    footer->addWidget(m_countLabel);
    footer->addStretch();
    footer->addWidget(m_statusLabel);
    root->addLayout(footer);

    // Ctrl+D never collides with text editing
    auto* deleteShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this);
    connect(deleteShortcut, &QShortcut::activated, this, &MainWindow::deleteCurrent);

    auto* pinShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+P")), this);
    connect(pinShortcut, &QShortcut::activated, this, &MainWindow::togglePinCurrent);

    auto* revealShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this);
    connect(revealShortcut, &QShortcut::activated, this, &MainWindow::toggleRevealCurrent);
    auto* detailsShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+I")), this);
    connect(detailsShortcut, &QShortcut::activated, this, &MainWindow::showSourceDetails);

    auto* appFilterShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")), this);
    connect(appFilterShortcut, &QShortcut::activated, this, [this] {
        filterByApp(m_list->currentIndex().data(ClipboardListModel::SourceAppRole).toString());
    });

    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, this, [this] {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });

    connect(&m_history, &HistoryService::historyChanged, this, &MainWindow::refresh);
    connect(&m_history, &HistoryService::captureRejected, this, &MainWindow::flashStatus);
}

void MainWindow::applyPaletteStyling()
{
    // Palette derived so the window follows the system theme
    const QPalette pal = palette();
    const QColor text = pal.color(QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor accent = pal.color(QPalette::Highlight);
    const auto mix = [](const QColor& a, const QColor& b, qreal f) {
        return QColor::fromRgbF(a.redF() * (1 - f) + b.redF() * f,
                                a.greenF() * (1 - f) + b.greenF() * f,
                                a.blueF() * (1 - f) + b.blueF() * f);
    };
    const QString subtle = mix(text, pal.color(QPalette::Window), 0.55).name();
    const QString border = mix(text, pal.color(QPalette::Window), 0.78).name();

    setStyleSheet(QStringLiteral(R"(
        QLabel#titleLabel { font-weight: 600; font-size: 13pt; }
        QLabel#footerLabel { color: %1; font-size: 9pt; }
        QLabel#emptyLabel { color: %1; font-size: 11pt; }
        QLabel#pausedBanner {
            color: %1; font-size: 9pt;
            padding: 7px 14px;
            background: %4;
            border-top: 1px solid %2;
            border-bottom: 1px solid %2;
        }
        QFrame#rule { color: %2; max-height: 1px; }
        QToolButton { font-size: 14pt; padding: 2px 6px; border-radius: 5px; }
        QLineEdit {
            padding: 7px 10px;
            border: 1px solid %2;
            border-radius: 7px;
            background: %3;
            selection-background-color: %5;
        }
        QLineEdit:focus { border: 1px solid %5; }
    )")
                      .arg(subtle, border, base.name(),
                           mix(pal.color(QPalette::Window), text, 0.05).name(), accent.name()));
}

void MainWindow::refresh()
{
    const qint64 previousId = [this] {
        const auto current = m_model->itemAt(m_list->currentIndex());
        return current ? current->id : -1;
    }();

    // secrets in memory match on their kind label only so search never probes the text
    const QString query = m_searchEdit->text().trimmed();
    QList<ClipboardItem> items;
    for (ClipboardItem& secret : m_history.ephemeral().items()) {
        if (query.isEmpty() || secretKindLabel(secret.secretKind).contains(query, Qt::CaseInsensitive))
            items.append(std::move(secret));
    }
    items.append(m_search.search(m_searchEdit->text()));
    m_model->setItems(std::move(items));
    updateCountdown();

    if (m_model->rowCount() > 0) {
        const int restored = previousId != -1 ? m_model->rowForId(previousId) : -1;
        const int row = restored >= 0 ? restored : 0;
        m_list->setCurrentIndex(m_model->index(row, 0));
    }

    updateEmptyState();
    updateFooter();
}

void MainWindow::updateEmptyState()
{
    if (m_model->rowCount() > 0) {
        m_stack->setCurrentWidget(m_list);
        return;
    }

    if (!m_searchEdit->text().trimmed().isEmpty())
        m_emptyLabel->setText(tr("No matches found"));
    else if (m_paused)
        m_emptyLabel->setText(tr("No clipboard history yet\n\n"
                                 "Capture is paused, so nothing new is being recorded."));
    else
        m_emptyLabel->setText(tr("No clipboard history yet\n\n"
                                 "Copy something and ClipVault will keep it here."));
    m_stack->setCurrentWidget(m_emptyLabel);
}

void MainWindow::updateFooter()
{
    const int total = m_history.count();
    const int shown = m_model->rowCount();
    // Plural forms need a translator so they are written out
    if (shown != total)
        m_countLabel->setText(tr("%1 of %2 items").arg(shown).arg(total));
    else
        m_countLabel->setText(total == 1 ? tr("1 item") : tr("%1 items").arg(total));
    if (m_statusLabel->property("flashing").toBool())
        return;
    m_statusLabel->setText(m_paused ? tr("⏸  Paused") : tr("●  Capturing"));
}

void MainWindow::setCapturePaused(bool paused)
{
    m_paused = paused;
    m_pausedBanner->setVisible(paused);
    updateEmptyState();
    updateFooter();
}

void MainWindow::flashStatus(const QString& message)
{
    m_statusLabel->setProperty("flashing", true);
    m_statusLabel->setText(message);
    QTimer::singleShot(kStatusFlashMs, this, [this] {
        m_statusLabel->setProperty("flashing", false);
        updateFooter();
    });
}

void MainWindow::updateCountdown()
{
    QHash<qint64, int> remaining;
    for (const ClipboardItem& item : m_model->items()) {
        if (item.id < 0)
            remaining.insert(item.id, m_history.ephemeral().remainingSeconds(item.id));
    }
    m_model->setRemaining(remaining);
    if (remaining.isEmpty() || !isVisible())
        m_countdown.stop();
    else if (!m_countdown.isActive())
        m_countdown.start();
}

void MainWindow::toggleRevealCurrent()
{
    const auto item = m_model->itemAt(m_list->currentIndex());
    if (!item || item->secretKind.isEmpty())
        return;
    m_model->setRevealed(item->id, !m_model->isRevealed(item->id));
}

void MainWindow::moveSelection(int delta)
{
    const int rows = m_model->rowCount();
    if (rows == 0)
        return;
    const int current = m_list->currentIndex().isValid() ? m_list->currentIndex().row() : 0;
    const int next = qBound(0, current + delta, rows - 1);
    m_list->setCurrentIndex(m_model->index(next, 0));
    m_list->scrollTo(m_model->index(next, 0), QAbstractItemView::EnsureVisible);
}

void MainWindow::activateCurrent()
{
    const auto item = m_model->itemAt(m_list->currentIndex());
    if (!item)
        return;
    if (!item->secretKind.isEmpty())
        emit secretChosen(item->id, item->secretKind);
    emit itemCopiedOut(item->id);
    emit itemChosen(item->content);
}

void MainWindow::deleteCurrent()
{
    const auto item = m_model->itemAt(m_list->currentIndex());
    if (!item)
        return;
    const int row = m_list->currentIndex().row();
    if (!m_history.remove(item->id)) {
        flashStatus(tr("Could not delete that entry."));
        return;
    }
    // refresh already ran so restore the cursor position
    if (m_model->rowCount() > 0)
        m_list->setCurrentIndex(m_model->index(qBound(0, row, m_model->rowCount() - 1), 0));
}

void MainWindow::togglePinCurrent()
{
    const auto item = m_model->itemAt(m_list->currentIndex());
    if (!item)
        return;
    if (!m_history.setPinned(item->id, !item->pinned)) {
        flashStatus(tr("Could not update that entry."));
        return;
    }
    const int row = m_model->rowForId(item->id);
    if (row >= 0) {
        m_list->setCurrentIndex(m_model->index(row, 0));
        m_list->scrollTo(m_model->index(row, 0), QAbstractItemView::EnsureVisible);
    }
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_settings, m_hotkeyNote, m_databasePath, m_paused, this);
    connect(&dialog, &SettingsDialog::clearHistoryRequested, this, &MainWindow::confirmClearHistory);
    connect(&dialog, &SettingsDialog::pauseToggleRequested, this, &MainWindow::pauseToggleRequested);
    if (m_provenance) {
        dialog.setFocusMechanism(m_provenance->mechanismDescription());
        connect(&dialog, &SettingsDialog::clearProvenanceRequested, this, [this] {
            if (!m_provenance->clearProvenance())
                flashStatus(tr("Could not clear source information."));
        });
    }
    dialog.exec();
}

void MainWindow::showSourceDetails()
{
    const auto item = m_model->itemAt(m_list->currentIndex());
    if (!item || !m_provenance)
        return;
    ProvenanceDialog dialog(*item, *m_provenance, m_model->isRevealed(item->id), this);
    connect(&dialog, &ProvenanceDialog::statusMessage, this, &MainWindow::flashStatus);
    dialog.exec();
}

void MainWindow::filterByApp(const QString& app)
{
    if (app.isEmpty())
        return;
    m_searchEdit->setText(SearchService::appQuery(app));
    m_searchEdit->setFocus();
}

void MainWindow::updateTitleNote()
{
    m_titleNote->setVisible(!m_settings.provenanceTitles() && SearchService::mentionsTitle(m_searchEdit->text()));
}

void MainWindow::confirmClearHistory()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Clear clipboard history"));
    box.setText(tr("Delete all clipboard history?"));
    box.setInformativeText(tr("This cannot be undone."));
    QPushButton* keepPinned = box.addButton(tr("Delete, keep pinned"), QMessageBox::DestructiveRole);
    QPushButton* deleteAll = box.addButton(tr("Delete everything"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == keepPinned)
        m_history.clearAll(/*includePinned=*/false);
    else if (box.clickedButton() == deleteAll)
        m_history.clearAll(/*includePinned=*/true);
}

void MainWindow::positionNearPointer()
{
    const QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QRect available = screen->availableGeometry();
    const int x = available.center().x() - width() / 2;
    const int y = available.top() + available.height() / 2 - height() * 2 / 3;
    move(qMax(available.left(), x), qMax(available.top(), y));
}

void MainWindow::showAsPopup()
{
    m_popupMode = true;
    m_hideOnDeactivate = false;
    m_searchEdit->clear();
    refresh();
    positionNearPointer();
    show();
    raise();
    activateWindow();
    m_searchEdit->setFocus();
}

void MainWindow::showNormalWindow()
{
    m_popupMode = false;
    m_hideOnDeactivate = false;
    refresh();
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
    m_searchEdit->setFocus();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);

    if (watched == m_searchEdit) {
        switch (keyEvent->key()) {
        case Qt::Key_Down:     moveSelection(1);  return true;
        case Qt::Key_Up:       moveSelection(-1); return true;
        case Qt::Key_PageDown: moveSelection(8);  return true;
        case Qt::Key_PageUp:   moveSelection(-8); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:    activateCurrent(); return true;
        default:
            break;
        }
    } else if (watched == m_list) {
        if (keyEvent->key() == Qt::Key_Delete) {
            deleteCurrent();
            return true;
        }
        // Typing returns to the search field instead of view search
        if (!keyEvent->text().isEmpty() && keyEvent->text().at(0).isPrint()
            && !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            m_searchEdit->setFocus();
            QApplication::sendEvent(m_searchEdit, keyEvent);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ActivationChange) {
        if (isActiveWindow()) {
            // Armed late so refused activation does not hide
            m_hideOnDeactivate = m_popupMode;
        } else if (m_hideOnDeactivate && !QApplication::activeModalWidget()
                   && !QApplication::activePopupWidget()) {
            // A modal child deactivates this window too
            hide();
        }
    } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
        applyPaletteStyling();
    }
    QWidget::changeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateCountdown();
}

void MainWindow::hideEvent(QHideEvent* event)
{
    m_model->clearRevealed();
    m_countdown.stop();
    QWidget::hideEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Closing hides so capture continues in the background
    if (m_backgroundReachable) {
        event->ignore();
        hide();
        return;
    }

    // Nothing could reopen it so closing ends the session
    event->accept();
    emit quitRequested();
}

} // namespace clipvault
