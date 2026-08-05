#pragma once

#include "manage/auth/auth_types.h"

#include <QByteArray>
#include <QString>

namespace manage::auth {

class PasswordHasher final {
public:
    static constexpr int kDefaultIterations = 210'000;
    static constexpr int kMinimumIterations = 100'000;

    explicit PasswordHasher(int iterations = kDefaultIterations);

    PasswordCredential create(const QString& password) const;
    bool verify(
        const QString& password,
        const PasswordCredential& credential
    ) const;

    static QString passwordPolicyError(const QString& password);
    static QByteArray derivePbkdf2Sha256(
        const QByteArray& password,
        const QByteArray& salt,
        int iterations
    );
    static bool constantTimeEquals(
        const QByteArray& left,
        const QByteArray& right
    );

private:
    int iterations_;
};

} // namespace manage::auth
