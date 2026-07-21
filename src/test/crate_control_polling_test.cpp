#include <gtest/gtest.h>

#include <QMutex>
#include <QUuid>

#include "control/controlobject.h"

namespace {

QMutex s_messageMutex;
QStringList s_messages;

void captureMessages(QtMsgType, const QMessageLogContext&, const QString& message) {
    const QMutexLocker locker(&s_messageMutex);
    s_messages.append(message);
}

TEST(CrateControlPollingTest, MissingControlWarnsOnlyOncePerKey) {
    const ConfigKey key(QStringLiteral("[CrateMissing_%1]")
                                .arg(QUuid::createUuid().toString()),
            QStringLiteral("meta"));
    s_messages.clear();
    const auto oldHandler = qInstallMessageHandler(captureMessages);
    ControlObject::getControl(key, ControlFlag::NoAssertIfMissing);
    ControlObject::getControl(key, ControlFlag::NoAssertIfMissing);
    ControlObject::getControl(key, ControlFlag::NoAssertIfMissing);
    qInstallMessageHandler(oldHandler);

    int matchingWarnings = 0;
    for (const QString& message : std::as_const(s_messages)) {
        matchingWarnings += message.contains(key.group) && message.contains(key.item);
    }
    EXPECT_EQ(matchingWarnings, 1);
}

} // namespace
