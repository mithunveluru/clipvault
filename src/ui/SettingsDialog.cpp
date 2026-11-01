#include "SettingsDialog.h"

#include "../services/OnnxEmbeddingProvider.h"
#include "../services/SemanticIndex.h"
#include "../services/SettingsService.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>

namespace clipvault {
namespace {

QString semanticStatusText(SemanticIndex::Status status)
{
    switch (status) {
    case SemanticIndex::Status::Off: return QObject::tr("Status: off");
    case SemanticIndex::Status::Verifying: return QObject::tr("Status: verifying the model files\u2026");
    case SemanticIndex::Status::Ready: return QObject::tr("Status: ready");
    case SemanticIndex::Status::FilesMissing:
        return QObject::tr("Status: files missing \u2014 the folder needs model.onnx and vocab.txt");
    case SemanticIndex::Status::HashMismatch:
        return QObject::tr("Status: hash mismatch \u2014 these are not the expected files, nothing was loaded");
    case SemanticIndex::Status::Failed: return QObject::tr("Status: the model could not be loaded");
    }
    return QString();
}

} // namespace

SettingsDialog::SettingsDialog(SettingsService& settings, QString hotkeyNote, QString databasePath,
                               bool capturePaused, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("ClipVault Settings"));
    setModal(true);
    buildUi(hotkeyNote, databasePath, capturePaused);
    fitToContents();
}

void SettingsDialog::fitToContents()
{
    // the default two thirds of screen cap squeezes the wrapped privacy text
    resize(sizeHint().boundedTo(screen()->availableGeometry().size()));
}

void SettingsDialog::buildUi(const QString& hotkeyNote, const QString& databasePath,
                             bool capturePaused)
{
    auto* root = new QVBoxLayout(this);

    auto* general = new QGroupBox(tr("General"), this);
    auto* generalForm = new QFormLayout(general);

    m_launchOnStartup = new QCheckBox(tr("Start ClipVault when I log in"), general);
    m_launchOnStartup->setChecked(m_settings.launchOnStartup());
    connect(m_launchOnStartup, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setLaunchOnStartup(on); });
    generalForm->addRow(m_launchOnStartup);

    m_hotkeyEdit = new QKeySequenceEdit(QKeySequence(m_settings.hotkey()), general);
    connect(m_hotkeyEdit, &QKeySequenceEdit::editingFinished, this, [this] {
        const QKeySequence sequence = m_hotkeyEdit->keySequence();
        if (sequence.isEmpty())
            return;
        // Only the first chord is meaningful for a global shortcut
        const QKeySequence single(sequence[0]);
        m_hotkeyEdit->setKeySequence(single);
        m_settings.setHotkey(single.toString(QKeySequence::PortableText));
    });
    generalForm->addRow(tr("Global shortcut"), m_hotkeyEdit);

    auto* hotkeyLabel = new QLabel(hotkeyNote, general);
    hotkeyLabel->setWordWrap(true);
    hotkeyLabel->setEnabled(false);
    generalForm->addRow(QString(), hotkeyLabel);

    m_retention = new QComboBox(general);
    for (const auto& option : SettingsService::retentionOptions())
        m_retention->addItem(option.label, option.seconds);
    const int retentionIndex = m_retention->findData(m_settings.retentionSeconds());
    m_retention->setCurrentIndex(retentionIndex >= 0 ? retentionIndex : 2);
    connect(m_retention, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_settings.setRetentionSeconds(m_retention->itemData(index).toInt());
    });
    generalForm->addRow(tr("Keep history for"), m_retention);

    m_maxItems = new QSpinBox(general);
    m_maxItems->setRange(10, 100000);
    m_maxItems->setSingleStep(100);
    m_maxItems->setValue(m_settings.maxItems());
    m_maxItems->setToolTip(tr("Pinned entries are never removed by this limit."));
    connect(m_maxItems, &QSpinBox::valueChanged, this,
            [this](int value) { m_settings.setMaxItems(value); });
    generalForm->addRow(tr("Maximum history items"), m_maxItems);

    m_startPaused = new QCheckBox(tr("Start with capture paused"), general);
    m_startPaused->setChecked(m_settings.startPaused());
    connect(m_startPaused, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setStartPaused(on); });
    generalForm->addRow(m_startPaused);

    root->addWidget(general);

    auto* privacy = new QGroupBox(tr("Privacy"), this);
    auto* privacyLayout = new QVBoxLayout(privacy);

    m_pauseCapture = new QCheckBox(tr("Pause clipboard capture"), privacy);
    m_pauseCapture->setChecked(capturePaused);
    connect(m_pauseCapture, &QCheckBox::toggled, this, &SettingsDialog::pauseToggleRequested);
    privacyLayout->addWidget(m_pauseCapture);

    auto* clearButton = new QPushButton(tr("Clear history…"), privacy);
    connect(clearButton, &QPushButton::clicked, this, &SettingsDialog::clearHistoryRequested);
    privacyLayout->addWidget(clearButton, 0, Qt::AlignLeft);

    auto* privacyNote = new QLabel(
        tr("Clipboard history is stored only on this computer. ClipVault has no accounts, "
           "no telemetry and makes no network connections. Clipboard contents are never "
           "written to the log."),
        privacy);
    privacyNote->setWordWrap(true);
    privacyNote->setEnabled(false);
    privacyLayout->addWidget(privacyNote);

    auto* pathLabel = new QLabel(tr("Database: %1").arg(databasePath), privacy);
    pathLabel->setWordWrap(true);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setEnabled(false);
    privacyLayout->addWidget(pathLabel);

    addProvenanceSection(privacyLayout, privacy);

    root->addWidget(privacy);

    auto* radar = new QGroupBox(tr("Secret radar"), this);
    auto* radarForm = new QFormLayout(radar);

    m_secretRadar = new QCheckBox(tr("Keep copied passwords and keys in memory only"), radar);
    m_secretRadar->setChecked(m_settings.secretRadarEnabled());
    connect(m_secretRadar, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setSecretRadarEnabled(on); });
    radarForm->addRow(m_secretRadar);

    m_secretTtl = new QSpinBox(radar);
    m_secretTtl->setRange(SettingsService::kMinSecretTtlSeconds, SettingsService::kMaxSecretTtlSeconds);
    m_secretTtl->setSuffix(tr(" s"));
    m_secretTtl->setValue(m_settings.secretTtlSeconds());
    m_secretTtl->setToolTip(tr("When the time runs out the secret is wiped from memory and, if it is "
                               "still on the clipboard, the clipboard is cleared."));
    connect(m_secretTtl, &QSpinBox::valueChanged, this,
            [this](int value) { m_settings.setSecretTtlSeconds(value); });
    radarForm->addRow(tr("Forget secrets after"), m_secretTtl);

    m_secretPinnedNeverExpire = new QCheckBox(tr("Pinned secrets never expire"), radar);
    m_secretPinnedNeverExpire->setChecked(m_settings.secretPinnedNeverExpire());
    m_secretPinnedNeverExpire->setToolTip(tr("Pinning a secret saves it to disk. Without this option it is "
                                             "still removed when the history retention period ends."));
    connect(m_secretPinnedNeverExpire, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setSecretPinnedNeverExpire(on); });
    radarForm->addRow(m_secretPinnedNeverExpire);

    m_secretCopyBackWarning = new QCheckBox(tr("Warn when a chat app is focused after copying a secret"),
                                            radar);
    m_secretCopyBackWarning->setChecked(m_settings.secretCopyBackWarning());
    connect(m_secretCopyBackWarning, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setSecretCopyBackWarning(on); });
    radarForm->addRow(m_secretCopyBackWarning);

    m_secretRiskyApps = new QLineEdit(m_settings.secretRiskyApps().join(QStringLiteral(", ")), radar);
    m_secretRiskyApps->setPlaceholderText(tr("Comma separated app names"));
    connect(m_secretRiskyApps, &QLineEdit::editingFinished, this, [this] {
        QStringList apps;
        for (const QString& part : m_secretRiskyApps->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const QString app = part.trimmed().toLower();
            if (!app.isEmpty())
                apps << app;
        }
        m_settings.setSecretRiskyApps(apps);
    });
    radarForm->addRow(tr("Chat apps"), m_secretRiskyApps);

    auto* radarNote = new QLabel(
        tr("Detection is a best guess from the text's shape and can miss secrets or flag ordinary text. "
           "The warning is best effort: ClipVault cannot see when or where you paste, only which "
           "window gets focus afterwards, and only on desktops that report focus."),
        radar);
    radarNote->setWordWrap(true);
    radarNote->setEnabled(false);
    radarForm->addRow(radarNote);

    root->addWidget(radar);

    buildSemanticGroup(root);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(buttons);
}

void SettingsDialog::addProvenanceSection(QVBoxLayout* layout, QWidget* parent)
{
    m_provenanceEnabled = new QCheckBox(tr("Record where clips come from"), parent);
    m_provenanceEnabled->setChecked(m_settings.provenanceEnabled());
    layout->addWidget(m_provenanceEnabled);

    m_provenanceTitles = new QCheckBox(tr("Also record window titles"), parent);
    m_provenanceTitles->setChecked(m_settings.provenanceTitles());
    connect(m_provenanceTitles, &QCheckBox::toggled, this,
            [this](bool on) { m_settings.setProvenanceTitles(on); });
    layout->addWidget(m_provenanceTitles);

    auto* recorded = new QLabel(
        tr("When this is on, ClipVault records for each clip: the application id and window class "
           "of the window you copied from; a best effort file or folder path (the terminal working "
           "directory, a file named in the editor title, or the clip itself when it is a web "
           "address, which is shown for reference and never opened); the window title, only when “Also record window titles” is on, and "
           "turning that off removes every stored title; and "
           "the app you pasted into when you copy a saved item back out of ClipVault, which is not "
           "recorded for secrets kept only in memory. Nothing is "
           "recorded for the apps listed below."),
        parent);
    recorded->setWordWrap(true);
    layout->addWidget(recorded);

    auto* blocklistLabel = new QLabel(tr("Never record these apps (app ids or window classes, "
                                         "separated by commas):"),
                                      parent);
    blocklistLabel->setWordWrap(true);
    layout->addWidget(blocklistLabel);
    m_provenanceBlocklist = new QLineEdit(m_settings.provenanceBlocklist().join(QStringLiteral(", ")), parent);
    m_provenanceBlocklist->setAccessibleName(tr("Apps never recorded as a source"));
    connect(m_provenanceBlocklist, &QLineEdit::editingFinished, this, [this] {
        m_settings.setProvenanceBlocklist(m_provenanceBlocklist->text().split(QLatin1Char(',')));
        m_provenanceBlocklist->setText(m_settings.provenanceBlocklist().join(QStringLiteral(", ")));
    });
    layout->addWidget(m_provenanceBlocklist);

    auto* clearSources = new QPushButton(tr("Clear source information…"), parent);
    connect(clearSources, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
            this, tr("Clear source information"),
            tr("Forget the source app, window, path and pasted into app of every clip? "
               "The clips themselves are kept."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Yes)
            emit clearProvenanceRequested();
    });
    layout->addWidget(clearSources, 0, Qt::AlignLeft);

    auto* bestEffort = new QLabel(
        tr("This is best effort. On Wayland without the ClipVault GNOME Shell extension only "
           "XWayland windows are seen. The terminal working directory and editor title parsing "
           "are heuristics and can be wrong or missing."),
        parent);
    bestEffort->setWordWrap(true);
    bestEffort->setEnabled(false);
    layout->addWidget(bestEffort);

    m_focusMechanism = new QLabel(parent);
    m_focusMechanism->setWordWrap(true);
    m_focusMechanism->setEnabled(false);
    layout->addWidget(m_focusMechanism);

    const auto applyEnabled = [this](bool on) {
        m_provenanceTitles->setEnabled(on);
        m_provenanceBlocklist->setEnabled(on);
    };
    applyEnabled(m_provenanceEnabled->isChecked());
    connect(m_provenanceEnabled, &QCheckBox::toggled, this, [this, applyEnabled](bool on) {
        m_settings.setProvenanceEnabled(on);
        applyEnabled(on);
    });
}

void SettingsDialog::setFocusMechanism(const QString& description)
{
    m_focusMechanism->setText(description);
    fitToContents();
}

void SettingsDialog::buildSemanticGroup(QLayout* root)
{
    auto* group = new QGroupBox(tr("Semantic search"), this);
    auto* layout = new QVBoxLayout(group);
    root->addWidget(group);

    if (!SemanticIndex::isCompiledIn()) {
        auto* note = new QLabel(tr("This build of ClipVault has no semantic search."), group);
        note->setWordWrap(true);
        note->setEnabled(false);
        layout->addWidget(note);
        return;
    }

    auto* enabled = new QCheckBox(tr("Also find clips by meaning, on this computer only"), group);
    enabled->setChecked(m_settings.semanticEnabled());
    connect(enabled, &QCheckBox::toggled, this, [this](bool on) { m_settings.setSemanticEnabled(on); });
    layout->addWidget(enabled);

    auto* folderRow = new QHBoxLayout;
    auto* folder = new QLineEdit(m_settings.semanticModelDir(), group);
    folder->setReadOnly(true);
    folder->setPlaceholderText(tr("No model folder chosen"));
    auto* choose = new QPushButton(tr("Choose folder\u2026"), group);
    connect(choose, &QPushButton::clicked, this, [this, folder] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Folder with model.onnx and vocab.txt"),
            m_settings.semanticModelDir());
        if (dir.isEmpty())
            return;
        folder->setText(dir);
        m_settings.setSemanticModelDir(dir);
    });
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(choose);
    layout->addLayout(folderRow);

    auto* note = new QLabel(tr("ClipVault never downloads the model. Fetch these two files yourself from %1 at "
                               "revision %2, put them in one folder and choose it above:\n"
                               "\u2022 %3 saved as %4, sha256 %5\n"
                               "\u2022 %6 saved as %7, sha256 %8\n"
                               "Files with any other hash are refused.")
                                .arg(QLatin1String(semantic_model::kRepository), QLatin1String(semantic_model::kRevision),
                                    QLatin1String(semantic_model::kUpstreamModelPath),
                                    QLatin1String(semantic_model::kModelFile), QLatin1String(semantic_model::kModelSha256),
                                    QLatin1String(semantic_model::kUpstreamVocabPath),
                                    QLatin1String(semantic_model::kVocabFile), QLatin1String(semantic_model::kVocabSha256)),
        group);
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    note->setEnabled(false);
    layout->addWidget(note);

    m_semanticStatus = new QLabel(semanticStatusText(SemanticIndex::Status::Off), group);
    m_semanticStatus->setWordWrap(true);
    layout->addWidget(m_semanticStatus);
}

void SettingsDialog::setSemanticIndex(const SemanticIndex& index)
{
    if (!m_semanticStatus)
        return;
    m_semanticStatus->setText(semanticStatusText(index.status()));
    connect(&index, &SemanticIndex::statusChanged, m_semanticStatus,
        [label = m_semanticStatus](SemanticIndex::Status status) { label->setText(semanticStatusText(status)); });
}

} // namespace clipvault
