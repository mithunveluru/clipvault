#include "core/WordPieceTokenizer.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include <QtTest>

using namespace clipvault;

namespace {

QByteArray readFixture(const char* name)
{
    QFile file(QStringLiteral(CLIPVAULT_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return file.readAll();
}

// the fixture keeps only the pieces the reference cases can reach so every other line gets a filler
// that never matches because brackets split off as punctuation
QByteArray sparseVocabFile(const QTemporaryDir& dir, int* pieces)
{
    QMap<int, QByteArray> tokens;
    for (const QByteArray& line : readFixture("minilm_vocab_subset.tsv").split('\n')) {
        const qsizetype tab = line.indexOf('\t');
        if (tab > 0)
            tokens.insert(line.left(tab).toInt(), line.mid(tab + 1));
    }
    if (tokens.isEmpty())
        return { };
    *pieces = static_cast<int>(tokens.size());
    QByteArray vocab;
    for (int id = 0; id <= tokens.lastKey(); ++id)
        vocab += tokens.value(id, QByteArrayLiteral("[filler]")) + '\n';
    QFile file(dir.filePath(QStringLiteral("vocab.txt")));
    if (!file.open(QIODevice::WriteOnly) || file.write(vocab) != vocab.size())
        return { };
    file.close();
    if (!file.open(QIODevice::ReadOnly))
        return { };
    return file.readAll();
}

} // namespace

class TestWordPieceTokenizer : public QObject {
    Q_OBJECT

    QByteArray m_vocab;
    int m_pieces = 0;

private slots:
    void initTestCase()
    {
        const QTemporaryDir dir;
        QVERIFY(dir.isValid());
        m_vocab = sparseVocabFile(dir, &m_pieces);
        QVERIFY(!m_vocab.isEmpty());
    }

    void vocabularyLoads()
    {
        const WordPieceTokenizer tokenizer(m_vocab);
        QVERIFY(tokenizer.isValid());
        // distinct tokens so the filler counts once
        QCOMPARE(tokenizer.vocabSize(), m_pieces + 1);
        QCOMPARE(tokenizer.encode(QString()), (std::vector<std::int64_t> { 101, 102 }));
    }

    void matchesReferenceTokenizer_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QList<qint64>>("expected");
        const QByteArray lines = readFixture("tokenizer_reference.jsonl");
        QVERIFY(!lines.isEmpty());
        int row = 0;
        for (const QByteArray& line : lines.split('\n')) {
            if (line.trimmed().isEmpty())
                continue;
            const QJsonObject object = QJsonDocument::fromJson(line).object();
            QList<qint64> ids;
            for (const QJsonValue& id : object.value(QStringLiteral("ids")).toArray())
                ids << id.toInteger();
            QTest::addRow("case %d", row++) << object.value(QStringLiteral("text")).toString() << ids;
        }
        QVERIFY(row >= 12);
    }

    void matchesReferenceTokenizer()
    {
        QFETCH(QString, text);
        QFETCH(QList<qint64>, expected);
        const WordPieceTokenizer tokenizer(m_vocab);
        const std::vector<std::int64_t> ids = tokenizer.encode(text);
        QCOMPARE(QList<qint64>(ids.begin(), ids.end()), expected);
    }

    void truncatesToMaxLength()
    {
        const WordPieceTokenizer tokenizer(m_vocab);
        const std::vector<std::int64_t> ids = tokenizer.encode(QStringLiteral("word ").repeated(500));
        QCOMPARE(ids.size(), std::size_t(WordPieceTokenizer::kMaxLength));
        QCOMPARE(ids.front(), std::int64_t(101));
        QCOMPARE(ids.back(), std::int64_t(102));
    }

    void longestPieceWins()
    {
        // hell and line are in the fixture too yet the longer words must win
        const WordPieceTokenizer tokenizer(m_vocab);
        QCOMPARE(tokenizer.encode(QStringLiteral("hello lines hell line")),
            (std::vector<std::int64_t> { 101, 7592, 3210, 3109, 2240, 102 }));
    }

    void basicTokensSplitPunctuationAndCjk()
    {
        QCOMPARE(WordPieceTokenizer::basicTokens(QStringLiteral("Hi,\u00A0th\u00E9re\u2028\u65E5\u672C")),
            (QStringList { QStringLiteral("hi"), QStringLiteral(","), QStringLiteral("there"),
                QStringLiteral("\u65E5"), QStringLiteral("\u672C") }));
        // control characters vanish rather than splitting words
        QCOMPARE(WordPieceTokenizer::basicTokens(QStringLiteral("ab\u0007cd\u200B")),
            QStringList { QStringLiteral("abcd") });
    }

    void unknownWordBecomesUnk()
    {
        const WordPieceTokenizer tokenizer(QByteArrayLiteral("[PAD]\n[UNK]\n[CLS]\n[SEP]\nab\n##c\n"));
        QVERIFY(tokenizer.isValid());
        QCOMPARE(tokenizer.encode(QStringLiteral("abc abd")), (std::vector<std::int64_t> { 2, 4, 5, 1, 3 }));
    }
};

QTEST_GUILESS_MAIN(TestWordPieceTokenizer)
#include "test_wordpiece_tokenizer.moc"
