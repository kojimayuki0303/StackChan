/* SPDX-License-Identifier: MIT */
#include <cstdlib>
#include <iostream>
#include <string>

#include "../main/hal/board/media_track_identity.h"

namespace {

void expect(const std::string& actual, const std::string& expected, const char* label)
{
    if (actual != expected) {
        std::cerr << label << ": expected '" << expected << "', got '" << actual << "'\n";
        std::exit(1);
    }
}

void testStableIdentityWins()
{
    expect(media_track::Key("spotify:track:one", "Same title", "Artist"), "spotify:track:one",
           "stable identity");
    // Position/status changes are not part of this key, so a repeated poll
    // does not create another artwork generation.
    expect(media_track::Key("spotify:track:one", "Localized title", "別アーティスト"), "spotify:track:one",
           "stable identity across display changes");
}

void testLegacyFallback()
{
    expect(media_track::Key(nullptr, "Title", "Artist"), "Title\nArtist", "legacy fallback");
    expect(media_track::Key("", "Title", "Artist"), "Title\nArtist", "empty identity fallback");
}

}  // namespace

int main()
{
    testStableIdentityWins();
    testLegacyFallback();
    return 0;
}
