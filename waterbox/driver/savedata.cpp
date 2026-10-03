// Save data in and out: see savedata.h.
// SPDX-License-Identifier: MIT
#include "savedata.h"
#include "memfs.h"

#include <miniz.h>

#include <cctype>

namespace chimera_vita3k::savedata {

namespace {

bool starts_with(const std::string &s, const char *p) {
    return s.rfind(p, 0) == 0;
}

// a title id: four capitals, five digits (PCSE00123)
bool is_title_id(const std::string &s) {
    if (s.size() != 9)
        return false;
    for (size_t i = 0; i < 9; i++)
        if (i < 4 ? !std::isupper(static_cast<unsigned char>(s[i])) : !std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

// The path under ux0 an entry goes to ("" when it has none).
std::string place(std::string name, const std::string &user_id) {
    for (char &c : name)
        if (c == '\\')
            c = '/';
    while (starts_with(name, "/"))
        name.erase(0, 1);
    for (const char *p : { "ux0:/", "ux0:", "ux0/" })
        if (starts_with(name, p)) {
            name.erase(0, std::char_traits<char>::length(p));
            break;
        }
    if (starts_with(name, "user/")) {
        const size_t slash = name.find('/', 5);
        if (slash == std::string::npos)
            return "";
        name.erase(0, slash + 1);
    }
    // no way out of the folder it lands in
    for (size_t at = 0; at <= name.size();) {
        const size_t end = std::min(name.find('/', at), name.size());
        const std::string part = name.substr(at, end - at);
        if (part == ".." || part == ".")
            return "";
        at = end + 1;
    }
    if (starts_with(name, "savedata/") && name.size() > 9)
        return "user/" + user_id + "/" + name;
    if (starts_with(name, "data/") && name.size() > 5)
        return name;
    const size_t slash = name.find('/');
    if (slash != std::string::npos && slash + 1 < name.size() && is_title_id(name.substr(0, slash)))
        return "user/" + user_id + "/savedata/" + name;
    return "";
}

} // namespace

bool seed(const std::vector<uint8_t> &zip, const std::string &ux0, const std::string &user_id, std::string &error) {
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0)) {
        error = "the save data is not a zip";
        return false;
    }
    bool ok = true;
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    // every entry is placed before anything is written
    std::vector<std::pair<mz_uint, std::string>> files;
    for (mz_uint i = 0; i < count && ok; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&archive, i, &st)) {
            error = "the save data zip cannot be read";
            ok = false;
        } else if (!mz_zip_reader_is_file_a_directory(&archive, i)) {
            const std::string where = place(st.m_filename, user_id);
            if (where.empty()) {
                error = std::string("the save data holds ") + st.m_filename
                    + ", which is not a Vita save: entries are savedata/<TITLE ID>/..., data/..., or a <TITLE ID>/ folder";
                ok = false;
            } else
                files.emplace_back(i, ux0 + "/" + where);
        }
    }
    for (const auto &[i, path] : files) {
        if (!ok)
            break;
        size_t size = 0;
        void *bytes = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        if (!bytes) {
            error = "the save data zip cannot be unpacked: " + path;
            ok = false;
            break;
        }
        const auto *p = static_cast<const uint8_t *>(bytes);
        ok = chimera::memfs::mkdirs(path.substr(0, path.rfind('/'))) && chimera::memfs::put(path, std::vector<uint8_t>(p, p + size));
        mz_free(bytes);
        if (!ok)
            error = "the save data cannot be written: " + path;
    }
    mz_zip_reader_end(&archive);
    return ok;
}

std::vector<File> snapshot(const std::string &ux0, const std::string &user_id) {
    std::vector<File> out;
    const std::pair<std::string, std::string> places[] = {
        { ux0 + "/user/" + user_id + "/savedata", "savedata/" },
        { ux0 + "/data", "data/" },
    };
    for (const auto &[dir, prefix] : places)
        for (const std::string &rel : chimera::memfs::list(dir)) {
            File f;
            f.name = prefix + rel;
            if (chimera::memfs::get(dir + "/" + rel, f.data))
                out.push_back(std::move(f));
        }
    return out;
}

} // namespace chimera_vita3k::savedata
