/******************************************************************************
 * @file    ScheduleCalculator.cpp
 * @brief   Implements deterministic publication-time calculation.
 *
 * @author  Zheludchenko Yehor
 * @date    2026-08-30
 * @version 2.1.0
 *
 * @copyright Copyright (c) 2026 TikTok Telegram Bot.
 *
 * Target: Qt 6.11.2 (MSVC 2022 64-bit, C++23)
 ******************************************************************************/

#include "app/ScheduleCalculator.h"

#include <QDate>
#include <QTime>
#include <QTimeZone>

#include <algorithm>

QDateTime ScheduleCalculator::nextPublication(const QDateTime& nowUtc,
                                              const QDateTime& latestPlannedUtc,
                                              const QList<ScheduleSlotRecord>& scheduleSlots,
                                              const std::chrono::minutes fallbackInterval,
                                              const QString& timeZoneId)
{
    const QDateTime normalizedNow = nowUtc.toUTC();
    const QDateTime searchAfter = latestPlannedUtc.isValid()
                                      ? std::max(normalizedNow, latestPlannedUtc.toUTC())
                                      : normalizedNow;

    QList<ScheduleSlotRecord> enabledSlots;
    std::copy_if(scheduleSlots.cbegin(), scheduleSlots.cend(), std::back_inserter(enabledSlots),
                 [](const ScheduleSlotRecord& slot) { return slot.enabled; });
    if (enabledSlots.isEmpty())
    {
        return latestPlannedUtc.isValid()
                   ? searchAfter.addSecs(
                         std::chrono::duration_cast<std::chrono::seconds>(fallbackInterval).count())
                   : normalizedNow;
    }

    const QTimeZone requestedZone(timeZoneId.toUtf8());
    const QTimeZone zone = requestedZone.isValid() ? requestedZone : QTimeZone::systemTimeZone();
    const QDateTime localStart = searchAfter.toTimeZone(zone);
    QDateTime bestCandidate;
    for (int dayOffset = 0; dayOffset <= 14; ++dayOffset)
    {
        const QDate date = localStart.date().addDays(dayOffset);
        for (const ScheduleSlotRecord& slot : enabledSlots)
        {
            if (slot.dayOfWeek != 0 && slot.dayOfWeek != date.dayOfWeek())
            {
                continue;
            }
            const QTime time = QTime::fromString(slot.time, QStringLiteral("HH:mm"));
            if (!time.isValid())
            {
                continue;
            }
            const QDateTime candidate(date, time, zone, QDateTime::TransitionResolution::Reject);
            if (!candidate.isValid() || candidate <= localStart)
            {
                continue;
            }
            if (!bestCandidate.isValid() || candidate < bestCandidate)
            {
                bestCandidate = candidate;
            }
        }
        if (bestCandidate.isValid())
        {
            return bestCandidate.toUTC();
        }
    }

    return searchAfter.addSecs(
        std::chrono::duration_cast<std::chrono::seconds>(fallbackInterval).count());
}
