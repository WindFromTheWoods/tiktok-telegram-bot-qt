/******************************************************************************
 * @file    ScheduleCalculator.h
 * @brief   Declares deterministic publication-time calculation.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#ifndef TIKTOK_TELEGRAM_BOT_APP_SCHEDULE_CALCULATOR_H
#define TIKTOK_TELEGRAM_BOT_APP_SCHEDULE_CALCULATOR_H

#include "storage/AppDatabase.h"

#include <QDateTime>
#include <QList>

#include <chrono>

/** @brief Calculates the next publication instant from slots or a fixed interval. */
class ScheduleCalculator final
{
public:
    /**
     * @brief Finds the earliest enabled slot after the current queue boundary.
     *
     * Day zero means every day; values one through seven follow Qt's Monday-through-
     * Sunday numbering. Returned timestamps are normalized to UTC.
     *
     * @param[in] nowUtc Current time, interpreted and normalized as UTC.
     * @param[in] latestPlannedUtc Latest queued time for the destination channel, if any.
     * @param[in] scheduleSlots Candidate daily or weekday-specific slots.
     * @param[in] fallbackInterval Interval used when no valid slot is configured.
     * @param[in] timeZoneId Channel time zone; empty uses the system time zone.
     * @return The next publication timestamp in UTC.
     */
    [[nodiscard]] static QDateTime nextPublication(const QDateTime& nowUtc,
                                                   const QDateTime& latestPlannedUtc,
                                                   const QList<ScheduleSlotRecord>& scheduleSlots,
                                                   std::chrono::minutes fallbackInterval,
                                                   const QString& timeZoneId = {});
};

#endif // TIKTOK_TELEGRAM_BOT_APP_SCHEDULE_CALCULATOR_H
