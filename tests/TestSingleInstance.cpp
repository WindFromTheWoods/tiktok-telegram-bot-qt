#include "app/SingleInstance.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestSingleInstance : public QObject
{
    Q_OBJECT
private slots:
    void secondInstanceActivatesFirst()
    {
        QTemporaryDir dir;
        SingleInstance first(dir.path());
        QVERIFY(first.acquire());
        SingleInstance second(dir.path());
        QVERIFY(!second.acquire());
        QSignalSpy activated(&first, &SingleInstance::activationRequested);
        second.activateExisting();
        QTRY_COMPARE(activated.size(), 1);
    }
    void releaseAllowsNextOwner()
    {
        QTemporaryDir dir;
        {
            SingleInstance first(dir.path());
            QVERIFY(first.acquire());
        }
        SingleInstance next(dir.path());
        QVERIFY(next.acquire());
    }
};
QTEST_GUILESS_MAIN(TestSingleInstance)
#include "TestSingleInstance.moc"
