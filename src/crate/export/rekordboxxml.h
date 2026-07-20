#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include "track/track_decl.h"

namespace mixxx {

class RekordboxXmlExport {
  public:
    using Playlist = QPair<QString, QList<TrackPointer>>;

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
            const QString& productVersion);
};

} // namespace mixxx
