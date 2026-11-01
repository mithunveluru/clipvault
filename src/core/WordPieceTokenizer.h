#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include <cstdint>
#include <vector>

namespace clipvault {

// bert uncased basic tokenisation followed by wordpiece matching the reference python tokenizer
class WordPieceTokenizer {
public:
    // one token per line and the line number is the id
    explicit WordPieceTokenizer(const QByteArray& vocab);

    [[nodiscard]] bool isValid() const { return m_cls >= 0 && m_sep >= 0 && m_unk >= 0; }
    [[nodiscard]] int vocabSize() const { return static_cast<int>(m_vocab.size()); }

    // ids wrapped in cls and sep and truncated to maxLength
    [[nodiscard]] std::vector<std::int64_t> encode(const QString& text) const;

    // lowercased accent free word and punctuation tokens before wordpiece
    [[nodiscard]] static QStringList basicTokens(const QString& text);

    static constexpr int kMaxLength = 128;
    static constexpr int kMaxCharsPerWord = 100;

private:
    void appendWordPieces(const QString& word, std::vector<std::int64_t>& out) const;

    QHash<QString, std::int64_t> m_vocab;
    std::int64_t m_cls = -1;
    std::int64_t m_sep = -1;
    std::int64_t m_unk = -1;
};

} // namespace clipvault
