#include "crate/export/rekordboxxml.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QUrl>
#include <QXmlStreamWriter>
#include <array>
#include <cmath>
#include <limits>

#include "track/keyutils.h"
#include "track/track.h"

namespace mixxx {
namespace {

QString locationUri(const QString& location) {
    QString path = QDir::fromNativeSeparators(QFileInfo(location).absoluteFilePath());
    return QStringLiteral("file://localhost/") +
            QString::fromLatin1(QUrl::toPercentEncoding(path, QByteArray("/:")));
}

QString snappedColor(RgbColor color) {
    static constexpr std::array<QRgb, 8> kPalette{{0xFF007F, 0xFF0000, 0xFFA500,
            0xFFFF00, 0x00FF00, 0x25FDE9, 0x0000FF, 0x660099}};
    const QColor source = RgbColor::toQColor(color);
    QRgb best = kPalette.front();
    int bestDistance = std::numeric_limits<int>::max();
    for (const QRgb candidate : kPalette) {
        const QColor c = QColor::fromRgb(candidate);
        const int distance = std::pow(source.red() - c.red(), 2) +
                std::pow(source.green() - c.green(), 2) +
                std::pow(source.blue() - c.blue(), 2);
        if (distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    const QString hex = QStringLiteral("%1").arg(best, 6, 16, QLatin1Char('0')).toUpper();
    return QStringLiteral("0x") + hex;
}

void optionalAttribute(QXmlStreamWriter* writer, const QString& name, const QString& value) {
    if (!value.isEmpty()) {
        writer->writeAttribute(name, value);
    }
}

double seconds(audio::FramePos position, audio::SampleRate sampleRate) {
    return position.value() / sampleRate.value();
}

void writeTempo(QXmlStreamWriter* writer, const BeatsPointer& beats,
        RekordboxXmlExport::Result* result) {
    if (!beats || !beats->getSampleRate().isValid() || !beats->firstBeat().isValid()) {
        return;
    }
    auto writeAnchor = [&](audio::FramePos position, Bpm bpm) {
        if (!position.isValid() || !bpm.isValid()) {
            return;
        }
        double start = seconds(position, beats->getSampleRate());
        const double interval = 60.0 / bpm.value();
        while (start < 0.0) {
            start += interval;
        }
        writer->writeEmptyElement(QStringLiteral("TEMPO"));
        writer->writeAttribute(QStringLiteral("Inizio"), QString::number(start, 'f', 3));
        writer->writeAttribute(QStringLiteral("Bpm"), QString::number(bpm.value(), 'f', 2));
        writer->writeAttribute(QStringLiteral("Metro"), QStringLiteral("4/4"));
        writer->writeAttribute(QStringLiteral("Battito"), QStringLiteral("1"));
        ++result->tempoAnchorsWritten;
    };
    if (beats->hasConstantTempo()) {
        writeAnchor(beats->firstBeat(), beats->getLastMarkerBpm());
        return;
    }
    const auto& markers = beats->getMarkers();
    for (size_t i = 0; i < markers.size(); ++i) {
        const auto& marker = markers[i];
        const auto nextPosition = i + 1 < markers.size()
                ? markers[i + 1].position()
                : beats->getLastMarkerPosition();
        const double sectionFrames = nextPosition.value() - marker.position().value();
        if (sectionFrames > 0.0) {
            writeAnchor(marker.position(), Bpm(60.0 * beats->getSampleRate().value() *
                    marker.beatsTillNextMarker() / sectionFrames));
        }
    }
    writeAnchor(beats->getLastMarkerPosition(), beats->getLastMarkerBpm());
}

void writeCues(QXmlStreamWriter* writer, const TrackPointer& track,
        audio::SampleRate sampleRate, RekordboxXmlExport::Result* result) {
    if (!sampleRate.isValid()) {
        return;
    }
    for (const auto& cue : track->getCuePoints()) {
        const auto type = cue->getType();
        const auto startPosition = cue->getPosition();
        if (!startPosition.isValid()) {
            continue;
        }
        bool loop = false;
        int number = -1;
        QString name = cue->getLabel();
        bool color = false;
        if (type == CueType::HotCue) {
            number = cue->getHotCue();
            loop = cue->getEndPosition().isValid() && cue->getEndPosition() > startPosition;
            color = true;
        } else if (type == CueType::Loop) {
            loop = true;
            number = cue->getHotCue();
            if (!cue->getEndPosition().isValid() || cue->getEndPosition() <= startPosition) {
                continue;
            }
        } else if (type == CueType::Intro) {
            name = QStringLiteral("Intro");
        } else if (type == CueType::Outro) {
            name = QStringLiteral("Outro");
        } else if (type != CueType::MainCue) {
            continue;
        }
        writer->writeEmptyElement(QStringLiteral("POSITION_MARK"));
        optionalAttribute(writer, QStringLiteral("Name"), name);
        writer->writeAttribute(QStringLiteral("Type"), loop ? QStringLiteral("4") : QStringLiteral("0"));
        writer->writeAttribute(QStringLiteral("Start"), QString::number(seconds(startPosition, sampleRate), 'f', 3));
        if (loop) {
            writer->writeAttribute(QStringLiteral("End"), QString::number(seconds(cue->getEndPosition(), sampleRate), 'f', 3));
        }
        writer->writeAttribute(QStringLiteral("Num"), QString::number(number));
        if (color) {
            const QColor c = RgbColor::toQColor(cue->getColor());
            writer->writeAttribute(QStringLiteral("Red"), QString::number(c.red()));
            writer->writeAttribute(QStringLiteral("Green"), QString::number(c.green()));
            writer->writeAttribute(QStringLiteral("Blue"), QString::number(c.blue()));
        }
        ++result->cuesWritten;
    }
}

} // namespace

RekordboxXmlExport::Result RekordboxXmlExport::write(const QList<Playlist>& playlists,
        const QString& outputPath, const QString& productVersion) {
    Result result;
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = file.errorString();
        return result;
    }
    QList<TrackPointer> tracks;
    QHash<QString, int> ids;
    for (const auto& playlist : playlists) {
        for (const auto& track : playlist.second) {
            if (!track || track->getLocation().isEmpty()) {
                continue;
            }
            const QString key = QDir::fromNativeSeparators(QFileInfo(track->getLocation()).absoluteFilePath());
            if (!ids.contains(key)) {
                ids.insert(key, tracks.size() + 1);
                tracks.append(track);
            }
        }
    }
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    writer.writeStartDocument(QStringLiteral("1.0"));
    writer.writeStartElement(QStringLiteral("DJ_PLAYLISTS"));
    writer.writeAttribute(QStringLiteral("Version"), QStringLiteral("1.0.0"));
    writer.writeEmptyElement(QStringLiteral("PRODUCT"));
    writer.writeAttribute(QStringLiteral("Name"), QStringLiteral("Crate"));
    writer.writeAttribute(QStringLiteral("Version"), productVersion);
    writer.writeAttribute(QStringLiteral("Company"), QStringLiteral("crate"));
    writer.writeStartElement(QStringLiteral("COLLECTION"));
    writer.writeAttribute(QStringLiteral("Entries"), QString::number(tracks.size()));
    for (int i = 0; i < tracks.size(); ++i) {
        const auto& track = tracks.at(i);
        const QFileInfo info(track->getLocation());
        if (!info.exists()) {
            ++result.tracksMissing;
        }
        writer.writeStartElement(QStringLiteral("TRACK"));
        writer.writeAttribute(QStringLiteral("TrackID"), QString::number(i + 1));
        writer.writeAttribute(QStringLiteral("Name"), track->getTitle());
        optionalAttribute(&writer, QStringLiteral("Artist"), track->getArtist());
        optionalAttribute(&writer, QStringLiteral("Album"), track->getAlbum());
        optionalAttribute(&writer, QStringLiteral("Genre"), track->getGenre());
        optionalAttribute(&writer, QStringLiteral("Composer"), track->getComposer());
        optionalAttribute(&writer, QStringLiteral("Grouping"), track->getGrouping());
        optionalAttribute(&writer, QStringLiteral("Comments"), track->getComment());
        const QString extension = info.suffix().toUpper();
        optionalAttribute(&writer, QStringLiteral("Kind"), extension.isEmpty() ? QString() : extension + QStringLiteral(" File"));
        if (info.exists()) writer.writeAttribute(QStringLiteral("Size"), QString::number(info.size()));
        writer.writeAttribute(QStringLiteral("TotalTime"), QString::number(track->getDurationSecondsInt()));
        optionalAttribute(&writer, QStringLiteral("TrackNumber"), track->getTrackNumber());
        optionalAttribute(&writer, QStringLiteral("Year"), track->getYear());
        if (track->getBitrate() > 0) writer.writeAttribute(QStringLiteral("BitRate"), QString::number(track->getBitrate()));
        if (track->getSampleRate().isValid()) writer.writeAttribute(QStringLiteral("SampleRate"), QString::number(track->getSampleRate().value()));
        if (track->getBpm() > 0) writer.writeAttribute(QStringLiteral("AverageBpm"), QString::number(track->getBpm(), 'f', 2));
        writer.writeAttribute(QStringLiteral("Rating"), QString::number(track->getRating() * 51));
        optionalAttribute(&writer, QStringLiteral("Tonality"), KeyUtils::keyToString(track->getKey(), KeyUtils::KeyNotation::Traditional));
        if (track->getColor()) writer.writeAttribute(QStringLiteral("Colour"), snappedColor(*track->getColor()));
        writer.writeAttribute(QStringLiteral("Location"), locationUri(track->getLocation()));
        writeTempo(&writer, track->getBeats(), &result);
        const auto sampleRate = track->getSampleRate().isValid() ? track->getSampleRate() :
                (track->getBeats() ? track->getBeats()->getSampleRate() : audio::SampleRate());
        writeCues(&writer, track, sampleRate, &result);
        writer.writeEndElement();
        ++result.tracksWritten;
    }
    writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("PLAYLISTS"));
    writer.writeStartElement(QStringLiteral("NODE"));
    writer.writeAttribute(QStringLiteral("Type"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("Name"), QStringLiteral("ROOT"));
    writer.writeAttribute(QStringLiteral("Count"), QString::number(playlists.size()));
    for (const auto& playlist : playlists) {
        writer.writeStartElement(QStringLiteral("NODE"));
        writer.writeAttribute(QStringLiteral("Name"), playlist.first);
        writer.writeAttribute(QStringLiteral("Type"), QStringLiteral("1"));
        writer.writeAttribute(QStringLiteral("KeyType"), QStringLiteral("0"));
        int entries = 0;
        for (const auto& track : playlist.second) if (track && !track->getLocation().isEmpty()) ++entries;
        writer.writeAttribute(QStringLiteral("Entries"), QString::number(entries));
        for (const auto& track : playlist.second) {
            if (!track || track->getLocation().isEmpty()) continue;
            writer.writeEmptyElement(QStringLiteral("TRACK"));
            writer.writeAttribute(QStringLiteral("Key"), QString::number(ids.value(QDir::fromNativeSeparators(QFileInfo(track->getLocation()).absoluteFilePath()))));
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    result.ok = !writer.hasError();
    if (!result.ok) result.error = QStringLiteral("Failed to write XML");
    return result;
}

} // namespace mixxx
