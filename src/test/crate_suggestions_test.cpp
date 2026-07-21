#include <gtest/gtest.h>

#include <cmath>

#include "crate/intelligence/suggestions.h"

namespace {

crate::SuggestTrack track(const QString& path,
        double x,
        double y,
        const QString& key = QStringLiteral("8A"),
        double bpm = 128.0,
        bool demoted = false) {
    crate::SuggestTrack value;
    value.relpath = path;
    const double norm = std::hypot(x, y);
    value.vector[0] = static_cast<float>(x / norm);
    value.vector[1] = static_cast<float>(y / norm);
    value.keyCamelot = key;
    value.bpm = bpm;
    value.demoted = demoted;
    return value;
}

QStringList paths(const QVector<crate::Suggestion>& suggestions) {
    QStringList result;
    for (const auto& suggestion : suggestions) {
        result.append(suggestion.relpath);
    }
    return result;
}

} // namespace

TEST(CrateSuggestionsTest, ModesHaveHandCheckableOrdering) {
    const QVector<crate::SuggestTrack> members{
            track(QStringLiteral("member-a"), 1.0, 0.0, QStringLiteral("8A"), 128.0),
            track(QStringLiteral("member-b"), 0.0, 1.0, QStringLiteral("9A"), 130.0)};
    const QVector<crate::SuggestTrack> universe{
            members[0], members[1],
            track(QStringLiteral("center"), 1.0, 1.0, QStringLiteral("1A"), 90.0),
            track(QStringLiteral("playable"), 0.8, 0.6, QStringLiteral("8A"), 128.0),
            track(QStringLiteral("novel"), -0.15, 1.0, QStringLiteral("9A"), 130.0)};

    EXPECT_EQ(paths(crate::Suggestions::rank(
                      members, universe, crate::SuggestMode::Sound)),
            QStringList({QStringLiteral("center"), QStringLiteral("playable"),
                    QStringLiteral("novel")}));
    EXPECT_EQ(paths(crate::Suggestions::rank(
                      members, universe, crate::SuggestMode::Mix)),
            QStringList({QStringLiteral("playable"), QStringLiteral("novel"),
                    QStringLiteral("center")}));
    const auto gap = paths(crate::Suggestions::rank(
            members, universe, crate::SuggestMode::Gap));
    EXPECT_EQ(gap.first(), QStringLiteral("center"));
}

TEST(CrateSuggestionsTest, ExcludesMembersDemotedAndAbsentVectors) {
    const auto member = track(QStringLiteral("member"), 1.0, 0.0);
    const QVector<crate::SuggestTrack> universe{
            member,
            track(QStringLiteral("eligible"), 1.0, 0.1),
            track(QStringLiteral("demoted"), 1.0, 0.2, QStringLiteral("8A"), 128.0, true)};
    // A missing-vector library track cannot enter the vector universe at all.
    EXPECT_EQ(paths(crate::Suggestions::rank(
                      {member}, universe, crate::SuggestMode::Sound)),
            QStringList({QStringLiteral("eligible")}));
}

TEST(CrateSuggestionsTest, GapPenalizesNearDuplicates) {
    const QVector<crate::SuggestTrack> members{
            track(QStringLiteral("member-a"), 1.0, 0.0),
            track(QStringLiteral("member-b"), 0.0, 1.0)};
    const QVector<crate::SuggestTrack> universe{
            members[0], members[1],
            track(QStringLiteral("near-duplicate"), 1.0, 0.01),
            track(QStringLiteral("fills-gap"), 1.0, 1.0)};
    EXPECT_EQ(paths(crate::Suggestions::rank(
                      members, universe, crate::SuggestMode::Gap)),
            QStringList({QStringLiteral("fills-gap"),
                    QStringLiteral("near-duplicate")}));
}

TEST(CrateSuggestionsTest, TiesUseRelpathAndInputOrderDoesNotMatter) {
    const auto member = track(QStringLiteral("member"), 1.0, 0.0);
    auto a = track(QStringLiteral("a/path"), 0.8, 0.6);
    auto z = track(QStringLiteral("z/path"), 0.8, -0.6);
    EXPECT_EQ(paths(crate::Suggestions::rank(
                      {member}, {z, a}, crate::SuggestMode::Sound)),
            QStringList({QStringLiteral("a/path"), QStringLiteral("z/path")}));
}
