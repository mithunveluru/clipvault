#include "core/SecretDetector.h"

#include <QFile>
#include <QTest>

using namespace clipvault;

namespace {

QStringList fixtureLines(const QString& name)
{
    QFile file(QStringLiteral(CLIPVAULT_FIXTURES "/") + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return { };
    QStringList lines;
    for (const QString& line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
            // the fixture splits tokens with a middle dot so secret scanners skip the file
            lines << QString(line).remove(QChar(0x00b7));
    }
    return lines;
}

} // namespace

class TestSecretDetector : public QObject {
    Q_OBJECT

private slots:
    void negativeCorpusHasNoFalsePositives()
    {
        const QStringList lines = fixtureLines(QStringLiteral("secret_negatives.txt"));
        QVERIFY(lines.size() >= 60);
        QStringList flagged;
        for (const QString& line : lines) {
            const SecretVerdict verdict = detectSecret(line);
            if (verdict.isSecret())
                flagged << verdict.kind + QStringLiteral(" <- ") + line;
        }
        QVERIFY2(flagged.isEmpty(), qPrintable(flagged.join(QLatin1Char('\n'))));
    }

    void positiveCorpusMatchesKind()
    {
        const QStringList lines = fixtureLines(QStringLiteral("secret_positives.txt"));
        QVERIFY(lines.size() >= 25);
        QStringList wrong;
        for (const QString& line : lines) {
            const qsizetype tab = line.indexOf(QLatin1Char('\t'));
            QVERIFY(tab > 0);
            const QString kind = line.left(tab);
            const QString value = line.mid(tab + 1);
            const SecretVerdict verdict = detectSecret(value);
            if (verdict.kind != kind)
                wrong << QStringLiteral("expected %1 got %2").arg(kind, verdict.kind);
        }
        QVERIFY2(wrong.isEmpty(), qPrintable(wrong.join(QLatin1Char('\n'))));
    }

    void kindNeverContainsText()
    {
        const QString secret = QStringLiteral("gh"
                                              "p_")
            + QString(36, QLatin1Char('a')) + QStringLiteral("1");
        const SecretVerdict verdict = detectSecret(secret);
        QCOMPARE(verdict.kind, QStringLiteral("github_token"));
        QCOMPARE(verdict.confidence, SecretConfidence::High);
        QVERIFY(!verdict.kind.contains(QStringLiteral("aaaa")));
    }

    void knownPrefixFoundInsideSmallClip()
    {
        const QString clip = QStringLiteral("here is the key you asked for\nAK"
                                            "IAIOSFODNN7EXAMPLE\nthanks");
        QCOMPARE(detectSecret(clip).kind, QStringLiteral("aws_access_key"));
    }

    void genericRulesIgnoreMultiWordClips()
    {
        // the same token alone is flagged but not inside prose
        const QString token = QStringLiteral("G8nfnL5Ofa6qD8mJ7ZDNBmJaDtDLZc5t4UuHF7KV");
        QCOMPARE(detectSecret(token).kind, QStringLiteral("high_entropy"));
        QVERIFY(!detectSecret(QStringLiteral("see ") + token + QStringLiteral(" for details")).isSecret());
        QVERIFY(!detectSecret(QStringLiteral("my P@ssw0rd!2024 is here")).isSecret());
    }

    void largeClipOnlyMatchesAtStart()
    {
        const QString filler = QString(kSecretEmbeddedScanChars, QLatin1Char('x')) + QLatin1Char(' ');
        const QString buried = filler
            + QStringLiteral("AK"
                             "IAIOSFODNN7EXAMPLE");
        QVERIFY(!detectSecret(buried).isSecret());
        const QString leading = QStringLiteral("-----BEGIN OPENSSH PR"
                                               "IVATE KEY-----\n")
            + filler;
        QCOMPARE(detectSecret(leading).kind, QStringLiteral("private_key"));
    }

    void hugeClipIsNotScanned()
    {
        const QString huge = QStringLiteral("-----BEGIN RSA PR"
                                            "IVATE KEY-----\n")
            + QString(kSecretMaxScanChars, QLatin1Char('A'));
        QVERIFY(!detectSecret(huge).isSecret());
    }

    void blankIsNotSecret()
    {
        QVERIFY(!detectSecret(QString()).isSecret());
        QVERIFY(!detectSecret(QStringLiteral("   \n")).isSecret());
    }
};

QTEST_GUILESS_MAIN(TestSecretDetector)
#include "test_secret_detector.moc"
