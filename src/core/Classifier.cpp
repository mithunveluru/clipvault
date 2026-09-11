#include "Classifier.h"

#include <QRegularExpression>
#include <QStringList>

namespace clipvault {
namespace {

// Anchored so prose mentioning an address stays Text
const QRegularExpression& emailRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$)"));
    return re;
}

const QRegularExpression& urlRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:[A-Za-z][A-Za-z0-9+.\-]*://|www\.)\S+$)"));
    return re;
}

// Requires statement shape not a stray keyword
const QRegularExpression& sqlRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(?:SELECT|INSERT|UPDATE|DELETE|CREATE|ALTER|DROP|WITH|EXPLAIN)\b[\s\S]*\b(?:FROM|INTO|SET|TABLE|VALUES|WHERE|INDEX|VIEW|AS)\b)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression& keywordRe()
{
    static const QRegularExpression re(QStringLiteral(
        R"((?:^|\W)(?:class|struct|enum|interface|namespace|template|typename|public|private|protected|static|void|const|return|import|export|require|function|func|def|fn|let|var|const|async|await|elif|else if|#include|#define|using namespace|package|impl|trait)(?:\W|$))"));
    return re;
}

const QRegularExpression& operatorRe()
{
    static const QRegularExpression re(QStringLiteral(R"((?:=>|->|::|!==|===|\+\+|&&|\|\||</\w|/>))"));
    return re;
}

} // namespace

bool looksLikeCode(const QString& content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty())
        return false;

    if (sqlRe().match(trimmed).hasMatch())
        return true;

    const QStringList lines = trimmed.split(QLatin1Char('\n'));
    int score = 0;

    if (keywordRe().match(trimmed).hasMatch())
        score += 1;
    if (operatorRe().match(trimmed).hasMatch())
        score += 1;

    // Terminating braces or semicolons are strong shape signals
    int braceLines = 0;
    int indentedLines = 0;
    for (const QString& line : lines) {
        const QString t = line.trimmed();
        if (t.endsWith(QLatin1Char('{')) || t == QLatin1String("}") || t.endsWith(QLatin1String("};"))
            || t.endsWith(QLatin1Char(';')))
            ++braceLines;
        if (line.startsWith(QLatin1String("  ")) || line.startsWith(QLatin1Char('\t')))
            ++indentedLines;
    }
    if (braceLines > 0)
        score += 1;
    // A lone indented line is usually a copied paragraph
    if (lines.size() > 1 && indentedLines >= 1)
        score += 1;
    if (braceLines >= 2 || indentedLines >= 2)
        score += 1;

    return score >= 2;
}

ClipboardType classify(const QString& content)
{
    const QString trimmed = content.trimmed();
    if (trimmed.isEmpty())
        return ClipboardType::Unknown;

    // An address or link is never also code
    if (!trimmed.contains(QLatin1Char('\n'))) {
        if (emailRe().match(trimmed).hasMatch())
            return ClipboardType::Email;
        if (urlRe().match(trimmed).hasMatch())
            return ClipboardType::Url;
    }

    if (looksLikeCode(trimmed))
        return ClipboardType::Code;

    return ClipboardType::Text;
}

} // namespace clipvault
