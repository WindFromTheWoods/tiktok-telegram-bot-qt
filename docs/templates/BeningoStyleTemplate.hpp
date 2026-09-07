/******************************************************************************
 * @file    BeningoStyleTemplate.hpp
 * @brief   Declares [briefly describe the module responsibility].
 *
 * @author  Zheludchenko Yehor
 * @date    YYYY-MM-DD
 * @version 1.0.0
 *
 * @copyright Copyright (c) YYYY TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef BENINGO_STYLE_TEMPLATE_HPP
#define BENINGO_STYLE_TEMPLATE_HPP

#include <QObject>

/**
 * @brief [Describe the class responsibility in one precise sentence].
 *
 * [Add details only when they clarify ownership, lifetime, threading,
 * invariants, or interaction with other components.]
 */
class BeningoStyleTemplate final : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs the component.
     *
     * @param[in] parent Parent object that owns this instance; may be nullptr.
     */
    explicit BeningoStyleTemplate(QObject* parent = nullptr);

    /**
     * @brief [Describe the operation and its observable result].
     *
     * @param[in] value [Describe meaning, units, and valid range if relevant].
     *
     * @return true if [state the exact success condition]; otherwise false.
     */
    [[nodiscard]] bool performOperation(int value);

private:
    /**
     * @brief [Document a non-trivial private helper's responsibility].
     *
     * @param[in] value [Describe what the helper expects].
     *
     * @return true if [state the exact condition]; otherwise false.
     */
    [[nodiscard]] bool validateValue(int value) const;
};

#endif // BENINGO_STYLE_TEMPLATE_HPP
