/******************************************************************************
 * @file    BeningoStyleTemplate.cpp
 * @brief   Implements [briefly describe the module responsibility].
 *
 * @author  Zheludchenko Yehor
 * @date    YYYY-MM-DD
 * @version 1.0.0
 *
 * @copyright Copyright (c) YYYY TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "BeningoStyleTemplate.hpp"

BeningoStyleTemplate::BeningoStyleTemplate(QObject* parent)
    : QObject(parent)
{
}

bool BeningoStyleTemplate::performOperation(const int value)
{
    if (!validateValue(value))
    {
        return false;
    }

    // Document only non-obvious implementation decisions here.
    return true;
}

bool BeningoStyleTemplate::validateValue(const int value) const
{
    constexpr int minimumValue = 0;
    constexpr int maximumValue = 100;

    return value >= minimumValue && value <= maximumValue;
}
