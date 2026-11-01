#include "ProvenanceDialog.h"

#include "../services/ProvenanceService.h"

#include <QLocale>
#include <QPushButton>

#include <utility>

namespace clipvault {

ProvenanceDialog::ProvenanceDialog(
    const ClipboardItem& shown, ProvenanceService& provenance, bool revealed, QWidget* parent)
    : QDialog(parent)
    , m_form(this)
    , m_buttons(QDialogButtonBox::Close)
{
    setWindowTitle(tr("Clip source"));
    setModal(true);
    setMinimumWidth(420);

    // the path of a url clip is the clip itself so a masked secret keeps both hidden
    const bool masked = !shown.secretKind.isEmpty() && !revealed;
    ClipboardItem item = shown;
    if (masked) {
        item.source.path.clear();
        item.source.title.clear();
    }

    const std::array<std::pair<QString, QString>, 6> rows { {
        { tr("Application"), item.source.app },
        { tr("Window class"), item.source.windowClass },
        { tr("Window title"), item.source.title },
        { tr("File, folder or link"), item.source.path },
        { tr("Pasted into"), item.copiedInto },
        { tr("Copied"), QLocale().toString(item.createdAt.toLocalTime(), QLocale::LongFormat) },
    } };
    for (std::size_t i = 0; i < rows.size(); ++i) {
        QLabel& field = m_fields.at(i);
        const QString& value = rows.at(i).second;
        // titles and paths come from other apps so markup in them must stay text
        field.setTextFormat(Qt::PlainText);
        field.setText(value.isEmpty() ? tr("Not recorded") : value);
        field.setWordWrap(true);
        field.setTextInteractionFlags(Qt::TextSelectableByMouse);
        field.setEnabled(!value.isEmpty());
        m_form.addRow(rows.at(i).first, &field);
    }
    if (masked) {
        m_fields.at(2).setText(tr("Hidden while the secret is masked"));
        m_fields.at(3).setText(tr("Hidden while the secret is masked"));
    }

    QPushButton* openButton = m_buttons.addButton(tr("Open source"), QDialogButtonBox::ActionRole);
    openButton->setEnabled(ProvenanceService::canOpen(item.source.path));
    connect(openButton, &QPushButton::clicked, this, [this, &provenance, path = item.source.path] {
        if (!provenance.openSource(path))
            emit statusMessage(tr("Could not open the source location."));
    });

    QPushButton* activateButton = m_buttons.addButton(tr("Activate window"), QDialogButtonBox::ActionRole);
    activateButton->setEnabled(provenance.canActivate(item.id));
    if (!activateButton->isEnabled())
        activateButton->setToolTip(tr("Only clips copied since ClipVault started know their window."));
    connect(activateButton, &QPushButton::clicked, this, [this, &provenance, id = item.id, source = item.source] {
        if (!provenance.activateSource(id, source))
            emit statusMessage(tr("This desktop does not let ClipVault raise that window."));
    });

    connect(&m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_form.addRow(&m_buttons);
}

} // namespace clipvault
