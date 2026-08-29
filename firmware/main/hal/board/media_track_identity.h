/* SPDX-License-Identifier: MIT */
#pragma once

#include <string>

namespace media_track {

// Prefer the sender's stable identity (normally spotify:track:<id>) over
// presentation text.  The fallback keeps older firmware/producers usable
// until they begin including track.uri or track.artwork_revision.
inline std::string Key(const char* identity, const char* title, const char* subtitle)
{
    if (identity != nullptr && identity[0] != '\0') {
        return identity;
    }
    return std::string(title != nullptr ? title : "") + "\n" + (subtitle != nullptr ? subtitle : "");
}

}  // namespace media_track
