#include "core/Classifier.h"

#include <QtTest>

using namespace clipvault;

class TestClassifier : public QObject {
    Q_OBJECT

private slots:
    void classifies_data();
    void classifies();
    void sentence_containing_a_link_stays_text();
    void prose_is_not_code();
};

void TestClassifier::classifies_data()
{
    QTest::addColumn<QString>("content");
    QTest::addColumn<int>("expected");

    const auto row = [](const char* name, const QString& content, ClipboardType type) {
        QTest::newRow(name) << content << static_cast<int>(type);
    };

    row("empty", QString(), ClipboardType::Unknown);
    row("whitespace", QStringLiteral("   \n\t "), ClipboardType::Unknown);

    row("https url", QStringLiteral("https://github.com"), ClipboardType::Url);
    row("url with path", QStringLiteral("https://github.com/user/repo/pull/12"), ClipboardType::Url);
    row("www url", QStringLiteral("www.example.com/docs"), ClipboardType::Url);
    row("scheme url", QStringLiteral("ssh://git@example.com:2222/repo.git"), ClipboardType::Url);

    row("email", QStringLiteral("mithun@example.com"), ClipboardType::Email);
    row("email with plus", QStringLiteral("first.last+tag@sub.example.co.uk"), ClipboardType::Email);

    row("select", QStringLiteral("SELECT * FROM employees;"), ClipboardType::Code);
    row("select lowercase", QStringLiteral("select id, name from users where id = 3"),
        ClipboardType::Code);
    row("create table", QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY)"),
        ClipboardType::Code);
    row("cpp class", QStringLiteral("class Solution {\n    int solve();\n};"), ClipboardType::Code);
    row("python def", QStringLiteral("def solve(n):\n    return n * 2\n"), ClipboardType::Code);
    row("js arrow", QStringLiteral("const add = (a, b) => a + b;"), ClipboardType::Code);

    row("greeting", QStringLiteral("Hello world"), ClipboardType::Text);
    row("question", QStringLiteral("Hello, how are you?"), ClipboardType::Text);
    row("address", QStringLiteral("14 Fake Street, Bengaluru 560001"), ClipboardType::Text);
}

void TestClassifier::classifies()
{
    QFETCH(QString, content);
    QFETCH(int, expected);
    QCOMPARE(static_cast<int>(classify(content)), expected);
}

void TestClassifier::sentence_containing_a_link_stays_text()
{
    // Compared as names so a failure reads clearly
    QCOMPARE(toString(classify(QStringLiteral("see https://github.com for details"))),
             QStringLiteral("Text"));
    QCOMPARE(toString(classify(QStringLiteral("write to mithun@example.com soon"))),
             QStringLiteral("Text"));
}

void TestClassifier::prose_is_not_code()
{
    QVERIFY(!looksLikeCode(QStringLiteral("Remember to return the book; it is due Friday.")));
    QVERIFY(looksLikeCode(QStringLiteral("void run() {\n    doThing();\n}")));
}

QTEST_GUILESS_MAIN(TestClassifier)
#include "test_classifier.moc"
