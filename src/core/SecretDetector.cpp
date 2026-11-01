#include "SecretDetector.h"

#include <QRegularExpression>

#include <array>
#include <cmath>

namespace clipvault {
namespace {

struct Rule {
    const char* kind;
    // cheap substring test that must hold before the regex runs or null for none
    const char* needle;
    Qt::CaseSensitivity needleCase;
    QRegularExpression pattern;
    // the tail of a generic match must look random and not like words
    bool generic;
};

QRegularExpression compile(const char* pattern, bool caseInsensitive = false)
{
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (caseInsensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(QString::fromLatin1(pattern), options);
}

// ordered from the most specific format to the most generic
const std::array<Rule, 15>& knownRules()
{
    static const std::array<Rule, 15> rules { {
        { "private_key", "PRIVATE KEY", Qt::CaseSensitive,
            compile(R"(-----BEGIN ((RSA|EC|DSA|OPENSSH|PGP|ENCRYPTED) )?PRIVATE KEY( BLOCK)?-----)"), false },
        { "aws_access_key", "A", Qt::CaseSensitive, compile(R"(\b(AKIA|ASIA|ABIA|ACCA)[A-Z0-9]{16}\b)"),
            false },
        { "github_token", "gh", Qt::CaseSensitive, compile(R"(\bgh[pousr]_[A-Za-z0-9]{36,255}\b)"), false },
        { "github_token", "github_pat_", Qt::CaseSensitive, compile(R"(\bgithub_pat_[A-Za-z0-9_]{50,255}\b)"),
            false },
        { "gitlab_token", "glpat-", Qt::CaseSensitive, compile(R"(\bglpat-[A-Za-z0-9_\-]{20,}\b)"), false },
        { "slack_webhook", "hooks.slack.com", Qt::CaseSensitive,
            compile(R"(https://hooks\.slack\.com/(services|workflows)/[A-Za-z0-9+/_\-]{20,})"), false },
        { "slack_token", "xox", Qt::CaseSensitive, compile(R"(\bxox[abposre]-[A-Za-z0-9\-]{10,})"), false },
        { "stripe_key", "_", Qt::CaseSensitive, compile(R"(\b[rs]k_(live|test)_[A-Za-z0-9]{20,}\b)"), false },
        { "google_api_key", "AIza", Qt::CaseSensitive,
            compile(R"(\bAIza[0-9A-Za-z_\-]{35}(?![0-9A-Za-z_\-]))"), false },
        { "npm_token", "npm_", Qt::CaseSensitive, compile(R"(\bnpm_[A-Za-z0-9]{36}\b)"), false },
        { "jwt", "eyJ", Qt::CaseSensitive,
            compile(R"(\beyJ[A-Za-z0-9_\-]{8,}\.eyJ[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,})"), false },
        { "database_url", "://", Qt::CaseSensitive,
            compile(
                R"(\b(postgres|postgresql|mysql|mariadb|mongodb|mongodb\+srv|redis|rediss|amqp|amqps|mssql|sqlserver)://[^\s:/@]+:[^\s/@]+@[^\s/@]+)",
                true),
            false },
        { "api_key", "sk-", Qt::CaseSensitive, compile(R"(\bsk-[A-Za-z0-9_\-]{20,})"), true },
        { "bearer_token", "bearer", Qt::CaseInsensitive,
            compile(R"(\bbearer\s+([A-Za-z0-9\-._~+/]{20,}=*))", true), true },
        { "password_assignment", nullptr, Qt::CaseSensitive,
            compile(
                R"((?<![A-Za-z])(password|passwd|pwd|secret|token|api_?key|access_?token|client_?secret)["']?\s*[:=]\s*["']?([^\s"'`(){}\[\]<>$;,]{6,}))",
                true),
            true },
    } };
    return rules;
}

bool hasDigit(QStringView text)
{
    for (const QChar ch : text) {
        if (ch.isDigit())
            return true;
    }
    return false;
}

bool hasPasswordSymbol(QStringView text)
{
    static const QString symbols = QStringLiteral("!@#%^&*+?~");
    for (const QChar ch : text) {
        if (symbols.contains(ch))
            return true;
    }
    return false;
}

// generic matches need something random looking in the captured value
bool tailLooksRandom(const Rule& rule, const QRegularExpressionMatch& match)
{
    if (!rule.generic)
        return true;
    const QStringView value = match.capturedView(match.lastCapturedIndex());
    return hasDigit(value) || hasPasswordSymbol(value);
}

SecretVerdict matchKnown(QStringView text, bool anchored)
{
    const auto matchOption
        = anchored ? QRegularExpression::AnchorAtOffsetMatchOption : QRegularExpression::NoMatchOption;
    for (const Rule& rule : knownRules()) {
        if (rule.needle && !text.contains(QLatin1String(rule.needle), rule.needleCase))
            continue;
        auto it = rule.pattern.globalMatch(text, 0, QRegularExpression::NormalMatch, matchOption);
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            if (tailLooksRandom(rule, match))
                return { QString::fromLatin1(rule.kind),
                    rule.generic ? SecretConfidence::Low : SecretConfidence::High };
            if (anchored)
                break;
        }
    }
    return { };
}

double shannonEntropy(QStringView text)
{
    std::array<int, 128> counts { };
    for (const QChar ch : text)
        ++counts[ch.unicode() & 0x7fU];
    double entropy = 0.0;
    const double length = static_cast<double>(text.size());
    for (const int count : counts) {
        if (count == 0)
            continue;
        const double p = count / length;
        entropy -= p * std::log2(p);
    }
    return entropy;
}

bool looksLikeLocation(QStringView token)
{
    return token.contains(QLatin1String("://")) || token.startsWith(QLatin1Char('/'))
        || token.startsWith(QLatin1String("~/")) || token.startsWith(QLatin1String("./"))
        || token.startsWith(QLatin1String("../")) || token.contains(QLatin1Char('\\'))
        || token.startsWith(QLatin1String("www."));
}

bool looksLikeEmail(QStringView token)
{
    static const QRegularExpression email(QStringLiteral(R"(^[^@\s]+@[^@\s]+\.[A-Za-z]{2,}$)"));
    return email.match(token).hasMatch();
}

struct CharClasses {
    int lower = 0;
    int upper = 0;
    int digit = 0;
    int symbol = 0;
    int other = 0;
};

CharClasses charClasses(QStringView token)
{
    // brackets commas and quotes are left out because code is full of them
    static const QString symbols = QStringLiteral("!@#$%^&*+=?~|");
    CharClasses classes;
    for (const QChar ch : token) {
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            ++classes.lower;
        else if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            ++classes.upper;
        else if (ch.isDigit())
            ++classes.digit;
        else if (symbols.contains(ch))
            ++classes.symbol;
        else
            ++classes.other;
    }
    return classes;
}

// four character classes in a short token reads as a typed password
bool looksLikePassword(QStringView token, const CharClasses& classes)
{
    if (token.size() < 8 || token.size() > 64)
        return false;
    if (classes.lower == 0 || classes.upper == 0 || classes.digit == 0 || classes.symbol == 0)
        return false;
    // code fragments and config lines such as key equals value carry symbols too
    static const QRegularExpression code(QStringLiteral(
        R"(::|->|\(\)|<[A-Za-z0-9_]+>|^[A-Za-z_][A-Za-z0-9_.]*\(|^#?[A-Za-z_][A-Za-z0-9_.\-]*\+?=)"));
    if (code.match(token).hasMatch())
        return false;
    // slashes and semicolons mean paths and config entries far more often than passwords
    if (token.contains(QLatin1Char('/')) || token.contains(QLatin1Char(';')))
        return false;
    return !looksLikeLocation(token) && !looksLikeEmail(token);
}

bool isHex(QStringView token)
{
    for (const QChar ch : token) {
        if (!ch.isDigit() && !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))
            && !(ch >= QLatin1Char('A') && ch <= QLatin1Char('F')))
            return false;
    }
    return true;
}

// random base62 or base64 tokens such as api keys with no known prefix
bool looksHighEntropy(QStringView token, const CharClasses& classes)
{
    constexpr qsizetype kMinLength = 24;
    constexpr qsizetype kMaxLength = 256;
    if (token.size() < kMinLength || token.size() > kMaxLength)
        return false;
    static const QRegularExpression charset(QStringLiteral(R"(^[A-Za-z0-9+/_\-]+={0,2}$)"));
    if (!charset.match(token).hasMatch())
        return false;
    // commit ids and digests are public by design
    if (isHex(token))
        return false;
    if (classes.lower < 2 || classes.upper < 2 || classes.digit < 2)
        return false;
    // identifiers glue whole words so they have few case changes per letter
    qsizetype transitions = 0;
    for (qsizetype i = 1; i < token.size(); ++i) {
        const bool before = token[i - 1].isUpper() || token[i - 1].isDigit();
        const bool now = token[i].isUpper() || token[i].isDigit();
        if (before != now)
            ++transitions;
    }
    if (transitions * 4 < token.size())
        return false;
    // a uniform alphabet over the token length sets the ceiling so ask for most of it
    const double ceiling = std::log2(static_cast<double>(qMin<qsizetype>(token.size(), 64)));
    return shannonEntropy(token) >= ceiling * 0.82;
}

bool containsSpace(QStringView text)
{
    for (const QChar ch : text) {
        if (ch.isSpace())
            return true;
    }
    return false;
}

} // namespace

SecretVerdict detectSecret(QStringView text)
{
    if (text.size() > kSecretMaxScanChars)
        return { };

    const QStringView trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return { };

    // large clips only count when they start with a known secret
    const bool large = trimmed.size() > kSecretEmbeddedScanChars;
    if (SecretVerdict known = matchKnown(trimmed, large); known.isSecret())
        return known;

    if (containsSpace(trimmed))
        return { };

    // integrity hashes from lockfiles carry symbols and look random
    static const QRegularExpression hashPrefix(
        QStringLiteral(R"(^(sha1|sha256|sha384|sha512|md5)[-_])"), QRegularExpression::CaseInsensitiveOption);
    if (hashPrefix.match(trimmed).hasMatch())
        return { };

    const CharClasses classes = charClasses(trimmed);
    if (looksLikePassword(trimmed, classes))
        return { QStringLiteral("possible_password"), SecretConfidence::Low };
    if (looksHighEntropy(trimmed, classes))
        return { QStringLiteral("high_entropy"), SecretConfidence::Low };
    return { };
}

} // namespace clipvault
