#include "TestSupport.h"
#include "core/Logging.h"
#include "core/SecretDetector.h"
#include "ui/ClipboardListModel.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>

using namespace clipvault;
using namespace clipvault::test;

namespace {

// synthetic values that only look like credentials
QStringList gateSecrets()
{
    return {
        QStringLiteral("gh"
                       "p_GateSyntheticToken000000000000000000A1"),
        QStringLiteral("AK"
                       "IAGATESYNTHETIC0A1"),
        QStringLiteral("sk"
                       "-proj-GateSynthetic0000000000000000000000A1"),
        QStringLiteral("postgres://gate:Synthetic0Pass1@db.example.internal/app"),
        QStringLiteral("export TOKEN=xo"
                       "xb-000000000000-0000000000000-GateSyntheticA1"),
        QStringLiteral("G8nfnL5Ofa6qD8mJ7ZDNBmGateDLZc5t4UuHF7KV"),
    };
}

QByteArray utf16Bytes(const QString& text)
{
    return QByteArray(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
}

// every form in which the text or its digest could land in a file
QList<QByteArray> needlesFor(const QString& secret)
{
    const QString hash = HistoryService::hashOf(secret);
    return { secret.toUtf8(), utf16Bytes(secret), hash.toLatin1(), utf16Bytes(hash) };
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return file.readAll();
}

} // namespace

class TestSecretGate : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_logDir.isValid());
        installLogHandler();
        // debug on so every log line that could ever carry text is written
        QLoggingCategory::setFilterRules(QStringLiteral("clipvault.*.debug=true"));
        enableLogFile(logPath());
    }

    void secretBytesNeverReachDiskOrLog()
    {
        Fixture fixture;
        QVERIFY2(fixture.isOpen(), qPrintable(fixture.openError()));

        const QString control = QStringLiteral("gate control entry that must be stored");
        QVERIFY(fixture.history().add(control).stored());
        for (const QString& secret : gateSecrets()) {
            QVERIFY2(detectSecret(secret).isSecret(), qPrintable(detectSecret(secret).kind));
            const auto outcome
                = fixture.history().add(secret, Provenance { QStringLiteral("terminal"), { }, { }, { } });
            QVERIFY(outcome.id < -1);
            // a repeat copy takes the dedupe path too
            fixture.history().add(secret);
        }
        QVERIFY(fixture.history().add(control + QStringLiteral(" again")).stored());
        fixture.history().runMaintenance();
        QCOMPARE(fixture.history().ephemeral().count(), gateSecrets().size());

        const QString db = fixture.databasePath();
        const QStringList files = { db, db + QStringLiteral("-wal"), db + QStringLiteral("-shm"), logPath() };
        QByteArray everything;
        for (const QString& path : files)
            everything += readAll(path);

        // proves the scan reads real bytes
        QVERIFY(everything.contains(control.toUtf8()));
        QVERIFY(everything.contains(HistoryService::hashOf(control).toLatin1()));
        QVERIFY(QFile::exists(logPath()));
        QVERIFY(readAll(logPath()).contains("secret kept in memory only"));

        for (const QString& path : files) {
            const QByteArray bytes = readAll(path);
            for (const QString& secret : gateSecrets()) {
                for (const QByteArray& needle : needlesFor(secret))
                    QVERIFY2(!bytes.contains(needle),
                        qPrintable(
                            QStringLiteral("secret form found in %1").arg(QFileInfo(path).fileName())));
            }
        }
    }

    void pinnedSecretIsPersistedAndMasked()
    {
        Fixture fixture;
        QVERIFY2(fixture.isOpen(), qPrintable(fixture.openError()));
        const QString secret = QStringLiteral("gh"
                                              "p_GatePinnedSyntheticToken0000000000000B2");
        const auto outcome = fixture.history().add(secret);
        QVERIFY(outcome.id < -1);
        QVERIFY(fixture.history().setPinned(outcome.id, true));

        const auto stored = fixture.repository().findByHash(HistoryService::hashOf(secret));
        QVERIFY(stored.has_value());
        QVERIFY(stored->pinned);
        QCOMPARE(stored->secretKind, QStringLiteral("github_token"));

        const QString db = fixture.databasePath();
        const QByteArray onDisk = readAll(db) + readAll(db + QStringLiteral("-wal"));
        QVERIFY(onDisk.contains(secret.toUtf8()));

        // also survives a restart with its kind
        QVERIFY(fixture.reopen());
        const auto reloaded = fixture.repository().findById(stored->id);
        QVERIFY(reloaded.has_value());
        QCOMPARE(reloaded->secretKind, QStringLiteral("github_token"));

        ClipboardListModel model;
        model.setItems({ *reloaded });
        const QModelIndex row = model.index(0, 0);
        for (const int role :
            { int(Qt::DisplayRole), int(ClipboardListModel::PreviewRole), int(Qt::AccessibleTextRole) }) {
            const QString shown = model.data(row, role).toString();
            QVERIFY2(!shown.contains(QStringLiteral("GatePinned")), qPrintable(shown));
        }
        QCOMPARE(model.data(row, Qt::DisplayRole).toString(), secretMask());
        QCOMPARE(
            model.data(row, ClipboardListModel::SecretKindRole).toString(), QStringLiteral("github_token"));
        // copy back still gets the real text
        QCOMPARE(model.data(row, ClipboardListModel::ContentRole).toString(), secret);

        model.setRevealed(stored->id, true);
        QVERIFY(model.data(row, Qt::DisplayRole).toString().contains(QStringLiteral("GatePinned")));
        model.clearRevealed();
        QCOMPARE(model.data(row, Qt::DisplayRole).toString(), secretMask());
    }

private:
    QString logPath() const { return m_logDir.filePath(QStringLiteral("clipvault.log")); }

    QTemporaryDir m_logDir;
};

QTEST_GUILESS_MAIN(TestSecretGate)
#include "test_secret_gate.moc"
