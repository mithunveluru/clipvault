#pragma once

#include <QString>
#include <QStringView>

namespace clipvault {

enum class SecretConfidence {
    None,
    // generic entropy or password shaped single token
    Low,
    // a known token format matched
    High,
};

struct SecretVerdict {
    // stable rule id such as github-token and never any part of the text
    QString kind;
    SecretConfidence confidence = SecretConfidence::None;

    [[nodiscard]] bool isSecret() const { return confidence != SecretConfidence::None; }
};

// pure function with no state and no logging
[[nodiscard]] SecretVerdict detectSecret(QStringView text);

// known prefix rules look inside clips up to this size
constexpr qsizetype kSecretEmbeddedScanChars = qsizetype { 4 } * 1024;
// nothing larger is scanned at all
constexpr qsizetype kSecretMaxScanChars = qsizetype { 64 } * 1024;

} // namespace clipvault
