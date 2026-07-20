#include <gtest/gtest.h>

#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include "crate/export/rekordboxxml.h"
#include "track/track.h"

namespace {

using Attributes = QList<QPair<QString, QXmlStreamAttributes>>;

Attributes parse(const QString& path) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    QXmlStreamReader reader(&file);
    Attributes elements;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            elements.append(qMakePair(reader.name().toString(), reader.attributes()));
        }
    }
    EXPECT_FALSE(reader.hasError()) << reader.errorString().toStdString();
    return elements;
}

QList<QXmlStreamAttributes> named(const Attributes& elements, const QString& name) {
    QList<QXmlStreamAttributes> result;
    for (const auto& element : elements) if (element.first == name) result.append(element.second);
    return result;
}

TrackPointer trackAt(const QString& path) {
    auto track = Track::newTemporary(path);
    track->setDuration(125.6);
    return track;
}

TEST(CrateRekordboxXmlTest, FullFidelityAndDump) {
    QTemporaryDir dir;
    const QString source = dir.path() + QStringLiteral("/unicode 测试 & quote\".flac");
    auto track = trackAt(source);
    track->setTitle(QStringLiteral("测试 & <quote\">"));
    track->setArtist(QStringLiteral("Artist & Co"));
    track->setAlbum(QStringLiteral("Album"));
    track->updateGenre(QStringLiteral("House"));
    track->setComposer(QStringLiteral("Composer"));
    track->setGrouping(QStringLiteral("Set"));
    track->setComment(QStringLiteral("A & B"));
    track->setTrackNumber(QStringLiteral("7"));
    track->setYear(QStringLiteral("2026"));
    track->setRating(4);
    track->setColor(mixxx::RgbColor::optional(0xFA0101));
    track->setKeyText(QStringLiteral("Am"));
    const auto sampleRate = mixxx::audio::SampleRate(48000);
    ASSERT_TRUE(track->trySetBeats(mixxx::Beats::fromConstTempo(
            sampleRate, mixxx::audio::FramePos(24000), mixxx::Bpm(120.0))));
    for (int i = 0; i < 4; ++i) {
        auto cue = track->createAndAddCue(mixxx::CueType::HotCue, i,
                mixxx::audio::FramePos(48000.0 * (i + 1)), {},
                mixxx::RgbColor(0x102030 + i));
        cue->setLabel(QStringLiteral("Hot %1").arg(i));
    }
    track->createAndAddCue(mixxx::CueType::HotCue, 4,
            mixxx::audio::FramePos(250000), mixxx::audio::FramePos(300000),
            mixxx::RgbColor(0xABCDEF))->setLabel(QStringLiteral("Saved loop"));
    track->createAndAddCue(mixxx::CueType::MainCue, -1,
            mixxx::audio::FramePos(10000), {});
    track->createAndAddCue(mixxx::CueType::Intro, -1,
            mixxx::audio::FramePos(12000), {});
    const QString output = dir.path() + QStringLiteral("/export.xml");
    const auto result = mixxx::RekordboxXmlExport::write(
            {{QStringLiteral("Crate & One"), {track}}}, output, QStringLiteral("test"));
    ASSERT_TRUE(result.ok) << result.error.toStdString();
    EXPECT_EQ(result.tracksWritten, 1);
    EXPECT_EQ(result.tracksMissing, 1);
    EXPECT_EQ(result.tempoAnchorsWritten, 1);
    EXPECT_EQ(result.cuesWritten, 7);
    const auto elements = parse(output);
    const auto tracks = named(elements, QStringLiteral("TRACK"));
    ASSERT_GE(tracks.size(), 2);
    EXPECT_EQ(tracks[0].value(QStringLiteral("Name")), track->getTitle());
    EXPECT_EQ(tracks[0].value(QStringLiteral("Rating")), QStringLiteral("204"));
    EXPECT_EQ(tracks[0].value(QStringLiteral("Tonality")), QStringLiteral("Am"));
    EXPECT_EQ(tracks[0].value(QStringLiteral("TotalTime")), QStringLiteral("126"));
    const QString location = tracks[0].value(QStringLiteral("Location")).toString();
    EXPECT_TRUE(location.startsWith(QStringLiteral("file://localhost/")));
    EXPECT_EQ(QUrl::fromPercentEncoding(location.mid(17).toLatin1()), QDir::fromNativeSeparators(QFileInfo(source).absoluteFilePath()));
    const auto tempos = named(elements, QStringLiteral("TEMPO"));
    ASSERT_EQ(tempos.size(), 1);
    EXPECT_NEAR(tempos[0].value(QStringLiteral("Inizio")).toDouble(),
            track->getBeats()->firstBeat().value() / sampleRate.value(), 0.001);
    const auto marks = named(elements, QStringLiteral("POSITION_MARK"));
    ASSERT_EQ(marks.size(), 7);
    EXPECT_EQ(marks[0].value(QStringLiteral("Num")), QStringLiteral("0"));
    EXPECT_EQ(marks[0].value(QStringLiteral("Red")), QStringLiteral("16"));
    EXPECT_EQ(marks[0].value(QStringLiteral("Green")), QStringLiteral("32"));
    EXPECT_EQ(marks[0].value(QStringLiteral("Blue")), QStringLiteral("48"));
    EXPECT_GT(marks[4].value(QStringLiteral("End")).toDouble(), marks[4].value(QStringLiteral("Start")).toDouble());
    EXPECT_EQ(marks[5].value(QStringLiteral("Num")), QStringLiteral("-1"));
    const auto nodes = named(elements, QStringLiteral("NODE"));
    ASSERT_EQ(nodes.size(), 2);
    EXPECT_EQ(nodes[1].value(QStringLiteral("Entries")), QStringLiteral("1"));
    EXPECT_EQ(nodes[1].value(QStringLiteral("KeyType")), QStringLiteral("0"));
    EXPECT_EQ(tracks[1].value(QStringLiteral("Key")), tracks[0].value(QStringLiteral("TrackID")));
    QFile raw(output); ASSERT_TRUE(raw.open(QIODevice::ReadOnly));
    const QByteArray bytes = raw.readAll();
    EXPECT_TRUE(bytes.contains("&amp;"));
    EXPECT_FALSE(bytes.contains("Artist & Co"));
    const QString dump = qEnvironmentVariable("CRATE_RB_XML_DUMP");
    if (!dump.isEmpty()) { QFile::remove(dump); EXPECT_TRUE(QFile::copy(output, dump)); }
}

TEST(CrateRekordboxXmlTest, NonConstantTempoSections) {
    QTemporaryDir dir;
    auto track = trackAt(dir.path() + QStringLiteral("/variable.wav"));
    const auto rate = mixxx::audio::SampleRate(48000);
    std::vector<mixxx::BeatMarker> markers{{mixxx::audio::FramePos(0), 4}, {mixxx::audio::FramePos(96000), 4}};
    ASSERT_TRUE(track->trySetBeats(mixxx::Beats::fromBeatMarkers(rate, markers,
            mixxx::audio::FramePos(180000), mixxx::Bpm(128.0))));
    const QString output = dir.path() + QStringLiteral("/out.xml");
    ASSERT_TRUE(mixxx::RekordboxXmlExport::write({{"Variable", {track}}}, output, "test").ok);
    const auto tempos = named(parse(output), QStringLiteral("TEMPO"));
    ASSERT_EQ(tempos.size(), 3);
    EXPECT_EQ(tempos[0].value(QStringLiteral("Bpm")), QStringLiteral("120.00"));
    EXPECT_EQ(tempos[1].value(QStringLiteral("Bpm")), QStringLiteral("137.14"));
    double previous = -1;
    for (const auto& tempo : tempos) { EXPECT_GE(tempo.value("Inizio").toDouble(), previous); previous = tempo.value("Inizio").toDouble(); }
    EXPECT_EQ(tempos.last().value(QStringLiteral("Bpm")), QStringLiteral("128.00"));
}

TEST(CrateRekordboxXmlTest, DeduplicatesAcrossCrates) {
    QTemporaryDir dir; auto track = trackAt(dir.path() + "/same.mp3"); const QString out = dir.path() + "/out.xml";
    ASSERT_TRUE(mixxx::RekordboxXmlExport::write({{"A", {track}}, {"B", {track}}}, out, "test").ok);
    const auto elements = parse(out); const auto collections = named(elements, "COLLECTION"); ASSERT_EQ(collections.size(), 1);
    EXPECT_EQ(collections[0].value("Entries"), QStringLiteral("1"));
    const auto tracks = named(elements, "TRACK"); ASSERT_EQ(tracks.size(), 3); EXPECT_EQ(tracks[1].value("Key"), tracks[2].value("Key"));
}

TEST(CrateRekordboxXmlTest, LocaleIndependentNumbers) {
    QTemporaryDir dir; auto track = trackAt(dir.path() + "/locale.mp3"); track->trySetBeats(mixxx::Beats::fromConstTempo(mixxx::audio::SampleRate(44100), mixxx::audio::FramePos(11025), mixxx::Bpm(123.45)));
    const QLocale old; QLocale::setDefault(QLocale(QLocale::German)); const QString out = dir.path() + "/out.xml";
    const auto result = mixxx::RekordboxXmlExport::write({{"Locale", {track}}}, out, "test"); QLocale::setDefault(old); ASSERT_TRUE(result.ok);
    QFile file(out); ASSERT_TRUE(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); EXPECT_TRUE(bytes.contains("123.45")); EXPECT_FALSE(bytes.contains("123,45"));
}

TEST(CrateRekordboxXmlTest, EmptyCrateIsValid) {
    QTemporaryDir dir; const QString out = dir.path() + "/empty.xml"; ASSERT_TRUE(mixxx::RekordboxXmlExport::write({{"Empty", {}}}, out, "test").ok);
    const auto elements = parse(out); EXPECT_EQ(named(elements, "COLLECTION")[0].value("Entries"), QStringLiteral("0")); EXPECT_EQ(named(elements, "NODE").last().value("Entries"), QStringLiteral("0"));
}

TEST(CrateRekordboxXmlTest, OmitsUnknownOptionalData) {
    QTemporaryDir dir; auto track = trackAt(dir.path() + "/plain"); const QString out = dir.path() + "/plain.xml"; ASSERT_TRUE(mixxx::RekordboxXmlExport::write({{"Plain", {track}}}, out, "test").ok);
    const auto elements = parse(out); const auto attrs = named(elements, "TRACK")[0]; EXPECT_FALSE(attrs.hasAttribute("Tonality")); EXPECT_FALSE(attrs.hasAttribute("AverageBpm")); EXPECT_TRUE(named(elements, "TEMPO").isEmpty()); EXPECT_TRUE(named(elements, "POSITION_MARK").isEmpty());
}

} // namespace
