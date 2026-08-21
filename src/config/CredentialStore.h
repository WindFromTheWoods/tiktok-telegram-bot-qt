#pragma once

#include <QString>

class CredentialStore final
{
public:
    [[nodiscard]] static QString readTelegramBotToken(QString *error = nullptr);
    [[nodiscard]] static bool writeTelegramBotToken(const QString &token,
                                                    QString *error = nullptr);
    [[nodiscard]] static bool usesSecureStorage() noexcept;
    [[nodiscard]] static QString storageDescription();
};
