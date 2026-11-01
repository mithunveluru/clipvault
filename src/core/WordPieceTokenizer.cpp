#include "WordPieceTokenizer.h"

#include <QList>
#include <QStringList>

namespace clipvault {
namespace {

bool isControl(char32_t cp)
{
    if (cp == U'\t' || cp == U'\n' || cp == U'\r')
        return false;
    switch (QChar::category(cp)) {
    case QChar::Other_Control:
    case QChar::Other_Format:
    case QChar::Other_Surrogate:
    case QChar::Other_PrivateUse:
    case QChar::Other_NotAssigned: return true;
    default: return false;
    }
}

bool isWhitespace(char32_t cp)
{
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r'
        || QChar::category(cp) == QChar::Separator_Space;
}

// ascii symbols count as punctuation like the reference even when unicode calls them symbols
bool isPunctuation(char32_t cp)
{
    if ((cp >= 33 && cp <= 47) || (cp >= 58 && cp <= 64) || (cp >= 91 && cp <= 96)
        || (cp >= 123 && cp <= 126))
        return true;
    switch (QChar::category(cp)) {
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other: return true;
    default: return false;
    }
}

bool isCjkIdeograph(char32_t cp)
{
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF)
        || (cp >= 0x20000 && cp <= 0x2A6DF) || (cp >= 0x2A700 && cp <= 0x2B73F)
        || (cp >= 0x2B740 && cp <= 0x2B81F) || (cp >= 0x2B820 && cp <= 0x2CEAF)
        || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x2F800 && cp <= 0x2FA1F);
}

QString fromCodePoints(const std::u32string& chars)
{
    return QString::fromStdU32String(chars);
}

} // namespace

WordPieceTokenizer::WordPieceTokenizer(const QByteArray& vocab)
{
    QList<QByteArray> lines = vocab.split('\n');
    // the trailing newline leaves one empty piece that is not a token
    if (!lines.isEmpty() && lines.constLast().isEmpty())
        lines.removeLast();
    std::int64_t id = 0;
    for (QByteArray& line : lines) {
        if (line.endsWith('\r'))
            line.chop(1);
        m_vocab.insert(QString::fromUtf8(line), id++);
    }
    m_cls = m_vocab.value(QStringLiteral("[CLS]"), -1);
    m_sep = m_vocab.value(QStringLiteral("[SEP]"), -1);
    m_unk = m_vocab.value(QStringLiteral("[UNK]"), -1);
}

QStringList WordPieceTokenizer::basicTokens(const QString& text)
{
    std::u32string cleaned;
    cleaned.reserve(static_cast<std::size_t>(text.size()) + 8);
    for (const char32_t cp : text.toStdU32String()) {
        if (cp == 0 || cp == 0xFFFD || isControl(cp))
            continue;
        if (isWhitespace(cp)) {
            cleaned.push_back(U' ');
        } else if (isCjkIdeograph(cp)) {
            cleaned.push_back(U' ');
            cleaned.push_back(cp);
            cleaned.push_back(U' ');
        } else {
            cleaned.push_back(cp);
        }
    }

    const QString normalized = fromCodePoints(cleaned).normalized(QString::NormalizationForm_C);

    QStringList out;
    for (const QString& word : normalized.split(QChar(u' '), Qt::SkipEmptyParts)) {
        const QString decomposed = word.toLower().normalized(QString::NormalizationForm_D);
        std::u32string current;
        for (const char32_t cp : decomposed.toStdU32String()) {
            if (QChar::category(cp) == QChar::Mark_NonSpacing)
                continue;
            // python split also breaks on line and paragraph separators
            if (QChar::isSpace(cp)) {
                if (!current.empty())
                    out << fromCodePoints(current);
                current.clear();
            } else if (isPunctuation(cp)) {
                if (!current.empty())
                    out << fromCodePoints(current);
                current.clear();
                out << fromCodePoints(std::u32string(1, cp));
            } else {
                current.push_back(cp);
            }
        }
        if (!current.empty())
            out << fromCodePoints(current);
    }
    return out;
}

void WordPieceTokenizer::appendWordPieces(const QString& word, std::vector<std::int64_t>& out) const
{
    const std::u32string chars = word.toStdU32String();
    const std::size_t length = chars.size();
    if (length > kMaxCharsPerWord) {
        out.push_back(m_unk);
        return;
    }

    std::vector<std::int64_t> pieces;
    std::size_t start = 0;
    while (start < length) {
        std::size_t end = length;
        std::int64_t match = -1;
        while (start < end) {
            QString piece = fromCodePoints(chars.substr(start, end - start));
            if (start > 0)
                piece.prepend(QStringLiteral("##"));
            const auto found = m_vocab.constFind(piece);
            if (found != m_vocab.constEnd()) {
                match = found.value();
                break;
            }
            --end;
        }
        if (match < 0) {
            out.push_back(m_unk);
            return;
        }
        pieces.push_back(match);
        start = end;
    }
    out.insert(out.end(), pieces.begin(), pieces.end());
}

std::vector<std::int64_t> WordPieceTokenizer::encode(const QString& text) const
{
    std::vector<std::int64_t> ids;
    ids.push_back(m_cls);
    const std::size_t budget = kMaxLength - 1;
    for (const QString& word : basicTokens(text)) {
        appendWordPieces(word, ids);
        if (ids.size() >= budget)
            break;
    }
    if (ids.size() > budget)
        ids.resize(budget);
    ids.push_back(m_sep);
    return ids;
}

} // namespace clipvault
