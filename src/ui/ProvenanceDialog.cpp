#include "ProvenanceDialog.h"

#include "../services/ProvenanceService.h"

#include <QLocale>
#include <QPushButton>

#include <utility>

namespace clipvault {

ProvenanceDialog::ProvenanceDialog(const ClipboardItem& item, ProvenanceService& provenance, QWidget* parent)
    : QDialog(parent)
    , m_form(this)
    , m_buttons(QDialogButtonBox::Close)
{
    setWindowTitle(tr("Clip source"));
    setModal(true);
    setMinimumWidth(420);

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
        field.setText(value.isEmpty() ? tr("Not recorded") : value);
        field.setWordWrap(true);
        field.setTextInteractionFlags(Qt::TextSelectableByMouse);
        field.setEnabled(!value.isEmpty());
        m_form.addRow(rows.at(i).first, &field);
    }

    QPushButton* openButton = m_buttons.addButton(tr("Open source"), QDialogButtonBox::ActionRole);
    openButton->setEnabled(!item.source.path.isEmpty());
    connect(openButton, &QPushButton::clicked, this, [this, &provenance, path = item.source.path] {
        if (!provenance.openSource(path))
            emit statusMessage(tr("Could not open the source location."));
    });

    QPushButton* activateButton = m_buttons.addButton(tr("Activate window"), QDialogButtonBox::ActionRole);
    activateButton->setEnabled(!item.source.app.isEmpty() || !item.source.windowClass.isEmpty());
    connect(activateButton, &QPushButton::clicked, this, [this, &provenance, source = item.source] {
        if (!provenance.activateSource(source))
            emit statusMessage(tr("This desktop does not let ClipVault raise that window."));
    });

    connect(&m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_form.addRow(&m_buttons);
}

} // namespace clipvault
