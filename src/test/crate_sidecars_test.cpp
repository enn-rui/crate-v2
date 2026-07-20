#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include "crate/data/cratesidecars.h"

namespace {

void writeCoords(const QString& path, int count) {
    QFile::remove(path);
    const QString connectionName = QStringLiteral("CrateSidecarsTest_%1")
                                           .arg(QUuid::createUuid().toString());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(path);
        ASSERT_TRUE(db.open());
        QSqlQuery query(db);
        ASSERT_TRUE(query.exec(QStringLiteral(
                "CREATE TABLE coords(relpath TEXT PRIMARY KEY, x REAL, y REAL)")));
        query.prepare(QStringLiteral("INSERT INTO coords VALUES(?, ?, ?)"));
        for (int i = 0; i < count; ++i) {
            query.bindValue(0, QStringLiteral("music/dj/Artist/Track %1.flac").arg(i));
            query.bindValue(1, i * 0.1);
            query.bindValue(2, i * 0.2);
            ASSERT_TRUE(query.exec());
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(count),
            QFileDevice::FileModificationTime));
}

TEST(CrateSidecarsSnapshot, RefreshesAndFallsBackUntilSourceIsHealthy) {
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const QString source = QDir(temp.path()).filePath(QStringLiteral("umap.sqlite"));

    writeCoords(source, 2);
    crate::CrateSidecars first(temp.path());
    ASSERT_TRUE(first.load()) << first.lastError().toStdString();
    EXPECT_EQ(first.nodes().size(), 2);

    writeCoords(source, 3);
    crate::CrateSidecars refreshed(temp.path());
    ASSERT_TRUE(refreshed.load()) << refreshed.lastError().toStdString();
    EXPECT_EQ(refreshed.nodes().size(), 3);

    QFile corrupt(source);
    ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(corrupt.write("not sqlite"), 10);
    corrupt.close();
    ASSERT_TRUE(corrupt.open(QIODevice::ReadWrite));
    ASSERT_TRUE(corrupt.setFileTime(QDateTime::currentDateTime().addSecs(10),
            QFileDevice::FileModificationTime));
    corrupt.close();
    crate::CrateSidecars fallback(temp.path());
    ASSERT_TRUE(fallback.load()) << fallback.lastError().toStdString();
    EXPECT_EQ(fallback.nodes().size(), 3);

    writeCoords(source, 4);
    crate::CrateSidecars recovered(temp.path());
    ASSERT_TRUE(recovered.load()) << recovered.lastError().toStdString();
    EXPECT_EQ(recovered.nodes().size(), 4);
}

} // namespace
