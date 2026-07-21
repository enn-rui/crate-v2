#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <functional>

#include "track/track_decl.h"

namespace mixxx {

class RekordboxXmlExport {
  public:
    using Playlist = QPair<QString, QList<TrackPointer>>;
    // Resolves a track's downbeat offset (0..3) so exported TEMPO Battito values
    // carry the true bar position. Defaults to 0 (grid anchor = downbeat).
    using DownbeatOffsetResolver = std::function<int(const TrackPointer&)>;

    struct Result {
        bool ok{false};
        QString error;
        int tracksWritten{0};
        int tracksMissing{0};
        int cuesWritten{0};
        int tempoAnchorsWritten{0};
    };

    static Result write(const QList<Playlist>& playlists,
            const QString& outputPath,
            const QString& productVersion,
            const DownbeatOffsetResolver& downbeatOffset = {});
};

} // namespace mixxx
