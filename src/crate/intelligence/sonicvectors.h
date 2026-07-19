#pragma once

#include <array>
#include <optional>

#include <QByteArray>
#include <QHash>
#include <QString>

namespace crate {

class SonicVectors {
  public:
    static constexpr int kDimensions = 512;
    using Vector = std::array<float, kDimensions>;

    bool load(const QString& vectorsPath);

    std::optional<double> cosine(const QString& relA, const QString& relB) const;
    std::optional<double> transition(const QString& relA, const QString& relB) const;
    const Vector* centered(const QString& relpath) const;
    QString lastError() const {
        return m_lastError;
    }

  private:
    struct Sections {
        std::optional<Vector> intro;
        std::optional<Vector> outro;
    };

    static std::optional<Vector> centerAndNormalize(
            const QByteArray& blob, const std::optional<Vector>& mean);
    static std::optional<double> dot(const Vector* a, const Vector* b);

    QHash<QString, Vector> m_whole;
    QHash<QString, Sections> m_sections;
    QString m_lastError;
};

} // namespace crate
