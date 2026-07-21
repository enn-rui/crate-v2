#include "crate/cull/culledtracks.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>

namespace {

const ConfigKey kCulledRelpaths("[Crate]", "galaxy_culled_relpaths");

QString normalized(const QString& path) {
    QString value = QDir::fromNativeSeparators(path).toCaseFolded();
    while (value.startsWith(QLatin1Char('/'))) {
        value.remove(0, 1);
    }
    return value;
}

bool suffixMatches(const QString& first, const QString& second) {
    const QString a = normalized(first);
    const QString b = normalized(second);
    return a == b || a.endsWith(QLatin1Char('/') + b) ||
            b.endsWith(QLatin1Char('/') + a);
}

void save(const UserSettingsPointer& pConfig, const QSet<QString>& relpaths) {
    QJsonArray values;
    QStringList sorted(relpaths.begin(), relpaths.end());
    sorted.sort();
    for (const QString& relpath : std::as_const(sorted)) {
        values.append(relpath);
    }
    if (values.isEmpty()) {
        pConfig->remove(kCulledRelpaths);
    } else {
        pConfig->setValue(kCulledRelpaths,
                QString::fromUtf8(QJsonDocument(values).toJson(QJsonDocument::Compact)));
    }
}

} // namespace

namespace crate {

QSet<QString> CulledTracks::load(const UserSettingsPointer& pConfig) {
    QSet<QString> result;
    if (!pConfig) {
        return result;
    }
    const QJsonDocument document = QJsonDocument::fromJson(
            pConfig->getValue(kCulledRelpaths, QString()).toUtf8());
    for (const QJsonValue& value : document.array()) {
        const QString relpath = normalized(value.toString());
        if (!relpath.isEmpty()) {
            result.insert(relpath);
        }
    }
    return result;
}

void CulledTracks::record(
        const UserSettingsPointer& pConfig, const QString& relpath) {
    if (!pConfig || relpath.isEmpty()) {
        return;
    }
    QSet<QString> relpaths = load(pConfig);
    relpaths.insert(normalized(relpath));
    save(pConfig, relpaths);
}

void CulledTracks::excludeAndPrune(
        const UserSettingsPointer& pConfig, QVector<GalaxyNode>* pNodes) {
    QSet<QString> pending = load(pConfig);
    if (pending.isEmpty()) {
        return;
    }
    QSet<QString> stillPresent;
    QVector<GalaxyNode> kept;
    kept.reserve(pNodes->size());
    for (const GalaxyNode& node : std::as_const(*pNodes)) {
        bool excluded = false;
        for (const QString& relpath : std::as_const(pending)) {
            if (suffixMatches(node.relpath, relpath)) {
                stillPresent.insert(relpath);
                excluded = true;
            }
        }
        if (!excluded) {
            kept.append(node);
        }
    }
    *pNodes = kept;
    if (stillPresent != pending) {
        save(pConfig, stillPresent);
    }
}

} // namespace crate
