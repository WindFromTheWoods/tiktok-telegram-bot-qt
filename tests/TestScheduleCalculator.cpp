/******************************************************************************
 * @file    TestScheduleCalculator.cpp
 * @brief   Tests daily, weekly, and fallback publication-time calculation.
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

#include <QTest>

#include <chrono>

using namespace std::chrono_literals;

class TestScheduleCalculator final : public QObject
{
    Q_OBJECT

private slots:
    void usesCurrentTimeForFirstFallbackPublication();
    void appliesFallbackIntervalAfterLatestTask();
    void usesCurrentBoundaryWhenLatestTaskIsPast();
    void selectsNextDailySlot();
    void selectsEarliestSlotRegardlessOfInputOrder();
    void skipsSlotAtCurrentMinute();
    void selectsNextWeeklySlot();
    void ignoresDisabledAndInvalidSlots();
};

void TestScheduleCalculator::usesCurrentTimeForFirstFallbackPublication()
{
    const QDateTime now(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::UTC);

    QCOMPARE(ScheduleCalculator::nextPublication(now, {}, {}, 120min), now);
}

void TestScheduleCalculator::usesCurrentBoundaryWhenLatestTaskIsPast()
{
    const QDateTime now(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::UTC);
    const QDateTime latest = now.addSecs(-60 * 60);

    QCOMPARE(ScheduleCalculator::nextPublication(now, latest, {}, 45min), now.addSecs(45 * 60));
}

void TestScheduleCalculator::appliesFallbackIntervalAfterLatestTask()
{
    const QDateTime now(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::UTC);
    const QDateTime latest = now.addSecs(30 * 60);

    QCOMPARE(ScheduleCalculator::nextPublication(now, latest, {}, 120min),
             latest.addSecs(2 * 60 * 60));
}

void TestScheduleCalculator::selectsEarliestSlotRegardlessOfInputOrder()
{
    const QDateTime localNow(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::LocalTime);
    const QList<ScheduleSlotRecord> scheduleSlots{
        {1, 1, 0, QStringLiteral("21:00"), true},
        {2, 1, 0, QStringLiteral("10:15"), true},
        {3, 1, 0, QStringLiteral("12:00"), true},
    };

    const QDateTime expected(localNow.date(), QTime(10, 15), QTimeZone::LocalTime);
    QCOMPARE(ScheduleCalculator::nextPublication(localNow.toUTC(), {}, scheduleSlots, 120min),
             expected.toUTC());
}

void TestScheduleCalculator::skipsSlotAtCurrentMinute()
{
    const QDateTime localNow(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::LocalTime);
    const QList<ScheduleSlotRecord> scheduleSlots{
        {1, 1, 0, QStringLiteral("10:00"), true},
    };
    const QDateTime expected(localNow.date().addDays(1), QTime(10, 0), QTimeZone::LocalTime);

    QCOMPARE(ScheduleCalculator::nextPublication(localNow.toUTC(), {}, scheduleSlots, 120min),
             expected.toUTC());
}

void TestScheduleCalculator::selectsNextDailySlot()
{
    const QDateTime localNow(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::LocalTime);
    const QList<ScheduleSlotRecord> scheduleSlots{
        {1, 1, 0, QStringLiteral("09:00"), true},
        {2, 1, 0, QStringLiteral("12:30"), true},
        {3, 1, 0, QStringLiteral("18:00"), true},
    };

    const QDateTime expected(localNow.date(), QTime(12, 30), QTimeZone::LocalTime);
    QCOMPARE(ScheduleCalculator::nextPublication(localNow.toUTC(), {}, scheduleSlots, 120min),
             expected.toUTC());
}

void TestScheduleCalculator::selectsNextWeeklySlot()
{
    const QDateTime localMonday(QDate(2026, 8, 24), QTime(18, 0), QTimeZone::LocalTime);
    const QList<ScheduleSlotRecord> scheduleSlots{
        {1, 1, 1, QStringLiteral("09:00"), true},
        {2, 1, 3, QStringLiteral("08:15"), true},
    };

    const QDateTime expected(QDate(2026, 8, 26), QTime(8, 15), QTimeZone::LocalTime);
    QCOMPARE(ScheduleCalculator::nextPublication(localMonday.toUTC(), {}, scheduleSlots, 120min),
             expected.toUTC());
}

void TestScheduleCalculator::ignoresDisabledAndInvalidSlots()
{
    const QDateTime now(QDate(2026, 8, 25), QTime(10, 0), QTimeZone::UTC);
    const QList<ScheduleSlotRecord> scheduleSlots{
        {1, 1, 0, QStringLiteral("11:00"), false},
        {2, 1, 0, QStringLiteral("not-a-time"), true},
    };

    QCOMPARE(ScheduleCalculator::nextPublication(now, now, scheduleSlots, 90min),
             now.addSecs(90 * 60));
}

QTEST_APPLESS_MAIN(TestScheduleCalculator)

#include "TestScheduleCalculator.moc"
