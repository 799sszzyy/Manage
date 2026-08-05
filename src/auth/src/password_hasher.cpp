#include "manage/auth/password_hasher.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

#include <algorithm>
#include <stdexcept>

namespace manage::auth {
namespace {

constexpr auto kAlgorithm = "pbkdf2-sha256";
constexpr qsizetype kSha256BlockSize = 64;
constexpr qsizetype kSaltSize = 16;

QByteArray randomBytes(qsizetype size) {
    QByteArray result(size, Qt::Uninitialized);
    auto* generator = QRandomGenerator::system();
    for (qsizetype index = 0; index < size; ++index) {
        result[index] = static_cast<char>(generator->generate() & 0xffU);
    }
    return result;
}

QByteArray hmacSha256(QByteArray key, const QByteArray& message) {
    if (key.size() > kSha256BlockSize) {
        key = QCryptographicHash::hash(key, QCryptographicHash::Sha256);
    }
    key.resize(kSha256BlockSize, '\0');

    QByteArray outerPad(kSha256BlockSize, static_cast<char>(0x5c));
    QByteArray innerPad(kSha256BlockSize, static_cast<char>(0x36));
    for (qsizetype index = 0; index < kSha256BlockSize; ++index) {
        outerPad[index] = static_cast<char>(
            static_cast<unsigned char>(outerPad.at(index)) ^
            static_cast<unsigned char>(key.at(index))
        );
        innerPad[index] = static_cast<char>(
            static_cast<unsigned char>(innerPad.at(index)) ^
            static_cast<unsigned char>(key.at(index))
        );
    }

    const auto inner = QCryptographicHash::hash(
        innerPad + message,
        QCryptographicHash::Sha256
    );
    auto result = QCryptographicHash::hash(
        outerPad + inner,
        QCryptographicHash::Sha256
    );
    key.fill('\0');
    return result;
}

} // namespace

PasswordHasher::PasswordHasher(int iterations)
    : iterations_(iterations) {
    if (iterations_ < kMinimumIterations) {
        throw std::invalid_argument("PBKDF2 iterations are below the safe minimum");
    }
}

PasswordCredential PasswordHasher::create(const QString& password) const {
    const auto salt = randomBytes(kSaltSize);
    auto passwordBytes = password.toUtf8();
    const auto hash = derivePbkdf2Sha256(passwordBytes, salt, iterations_);
    passwordBytes.fill('\0');
    return {
        QString::fromLatin1(kAlgorithm),
        hash,
        salt,
        iterations_,
    };
}

bool PasswordHasher::verify(
    const QString& password,
    const PasswordCredential& credential
) const {
    if (credential.algorithm != QString::fromLatin1(kAlgorithm) ||
        credential.iterations < kMinimumIterations ||
        credential.salt.size() < kSaltSize || credential.hash.size() != 32) {
        return false;
    }

    auto passwordBytes = password.toUtf8();
    const auto actual = derivePbkdf2Sha256(
        passwordBytes,
        credential.salt,
        credential.iterations
    );
    passwordBytes.fill('\0');
    return constantTimeEquals(actual, credential.hash);
}

QString PasswordHasher::passwordPolicyError(const QString& password) {
    if (password.size() < 12) {
        return QStringLiteral("password must contain at least 12 characters");
    }
    if (password.toUtf8().size() > 1024) {
        return QStringLiteral("password is too long");
    }
    if (password.trimmed().isEmpty()) {
        return QStringLiteral("password must not contain only whitespace");
    }
    return {};
}

QByteArray PasswordHasher::derivePbkdf2Sha256(
    const QByteArray& password,
    const QByteArray& salt,
    int iterations
) {
    if (iterations <= 0) {
        return {};
    }

    QByteArray block = salt;
    block.append('\0');
    block.append('\0');
    block.append('\0');
    block.append('\1');

    auto current = hmacSha256(password, block);
    auto result = current;
    for (int iteration = 1; iteration < iterations; ++iteration) {
        current = hmacSha256(password, current);
        for (qsizetype index = 0; index < result.size(); ++index) {
            result[index] = static_cast<char>(
                static_cast<unsigned char>(result.at(index)) ^
                static_cast<unsigned char>(current.at(index))
            );
        }
    }
    current.fill('\0');
    return result;
}

bool PasswordHasher::constantTimeEquals(
    const QByteArray& left,
    const QByteArray& right
) {
    const auto maximumSize = std::max(left.size(), right.size());
    unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
    for (qsizetype index = 0; index < maximumSize; ++index) {
        const auto leftByte = index < left.size()
                                  ? static_cast<unsigned char>(left.at(index))
                                  : 0U;
        const auto rightByte = index < right.size()
                                   ? static_cast<unsigned char>(right.at(index))
                                   : 0U;
        difference |= leftByte ^ rightByte;
    }
    return difference == 0;
}

} // namespace manage::auth
