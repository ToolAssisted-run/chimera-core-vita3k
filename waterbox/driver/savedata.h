// Save data in and out. What a Vita keeps of a game - ux0:user/<id>/savedata
// (the save data system's own) and ux0:data (where homebrew keeps its) -
// lives in the machine's filesystem, in guest memory, so a state carries it.
// It goes out as files named savedata/<TITLE ID>/... and data/..., and comes
// back in under the same names, from a zip, before the machine starts.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chimera_vita3k::savedata {

// Unpack `zip` into the machine's ux0 (`ux0`, a path in the machine's
// filesystem; `user_id` the user's folder, "00"). An entry is
// savedata/<TITLE ID>/..., data/..., or a title's own folder <TITLE ID>/...;
// a leading "ux0:/" or "ux0/" and "user/<id>/" are taken off first. An entry
// that is none of these refuses the whole zip: a save that would not be read
// must not be ignored in silence.
bool seed(const std::vector<uint8_t> &zip, const std::string &ux0, const std::string &user_id, std::string &error);

struct File {
    std::string name;
    std::vector<uint8_t> data;
};

// Every file of both places now, in name order, named as seed takes them.
std::vector<File> snapshot(const std::string &ux0, const std::string &user_id);

} // namespace chimera_vita3k::savedata
