// Writable user state and one-time import (user_data.hpp).
#include "user_data.hpp"

#include "noxcore/io/file.hpp"
#include "editor/ordered_json.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <iterator>
#include <set>
#include <sys/stdio.h>
#include <fstream>
#include <stdexcept>

namespace noxsdl {

namespace {
namespace fs = std::filesystem;

bool contains(const fs::path& root, fs::path path)
{
    // Compare existing ancestors by identity: symlinks and case aliases on APFS count too.
    path = fs::weakly_canonical(path);
    for (;;) {
        if (fs::exists(path) && fs::equivalent(root, path)) return true;
        const auto parent = path.parent_path();
        if (parent == path || parent.empty()) return false;
        path = parent;
    }
}

void requireSeparate(const fs::path& source, const fs::path& target)
{
    if (contains(source, target) || (fs::exists(target) && contains(target, source))) {
        throw std::runtime_error("Import source and destination overlap: " + source.string() + " and " + target.string());
    }
}

void copyMissing(const fs::path& from, const fs::path& to)
{
    if (fs::is_regular_file(from) && fs::copy_file(from, to, fs::copy_options::skip_existing)) {
        fs::permissions(to, fs::perms::owner_write, fs::perm_options::add);
    }
}

void copyTreeMissing(const fs::path& source, const fs::path& destination)
{
    fs::create_directories(destination);
    for (auto it = fs::recursive_directory_iterator(source); it != fs::recursive_directory_iterator(); ++it) {
        const auto target = destination / it->path().lexically_relative(source);
        if (it->is_directory() && !it->is_symlink()) {
            if (fs::is_symlink(target)) {
                it.disable_recursion_pending(); // An existing user link is not an import destination.
            } else {
                fs::create_directories(target); // New directories stay writable even with read-only originals.
            }
        } else if (it->is_regular_file()) {
            copyMissing(it->path(), target);
        }
    }
}

bool referenceMod(const fs::path& folder)
{
    std::ifstream in(folder / "mode.json");
    if (!in) return false;
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    try {
        const auto manifest = editor::json::parse(text);
        const auto* reference = manifest.find("reference");
        return reference && reference->isBool() && reference->boolean;
    } catch (const editor::json::ParseError&) {
        return false; // A user's incomplete project is never treated as a replaceable reference.
    }
}

bool sameFile(const fs::path& left, const fs::path& right)
{
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    if (!a || !b) return false;
    std::array<char, 16384> x, y;
    do {
        a.read(x.data(), x.size());
        b.read(y.data(), y.size());
        if (a.gcount() != b.gcount() || !std::equal(x.begin(), x.begin() + a.gcount(), y.begin())) return false;
    } while (a && b);
    return a.eof() && b.eof() && !a.bad() && !b.bad();
}

bool sameTree(const fs::path& source, const fs::path& destination)
{
    if (!fs::is_directory(destination) || fs::is_symlink(destination)) return false;
    std::set<fs::path> expected;
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        const auto relative = entry.path().lexically_relative(source);
        expected.insert(relative);
        const auto target = destination / relative;
        if (fs::is_symlink(target)) return false;
        if (entry.is_directory()) {
            if (!fs::is_directory(target)) return false;
        } else {
            if (!fs::is_regular_file(target) || fs::file_size(entry.path()) != fs::file_size(target)) return false;
            if (!sameFile(entry.path(), target)) return false;
        }
    }
    for (const auto& entry : fs::recursive_directory_iterator(destination)) {
        if (!expected.contains(entry.path().lexically_relative(destination))) return false;
    }
    return true;
}

void mark(const fs::path& marker)
{
    std::ofstream done(marker);
    done << "Imported without overwriting existing user files.\n";
    done.close();
    if (!done) throw std::runtime_error("Cannot write user-data import marker: " + marker.string());
}
}

// Keep writable state separate from installed assets, including read-only translocated apps.
void prepareUserData(const std::filesystem::path& assets, const std::filesystem::path& user,
                     bool importSaves)
{
    namespace fs = std::filesystem;
    if (fs::exists(user) && fs::equivalent(assets, user)) return;
    const fs::path savesMarker = user / ".saves-import-complete";
    const fs::path saves = nox::findNoCase(assets, "save");
    if (importSaves && !fs::exists(savesMarker) && fs::is_directory(saves)) {
        requireSeparate(saves, user / "Save"); // Before creating anything the source traversal could see.
    }
    fs::create_directories(user);
    const fs::path settingsMarker = user / ".settings-import-complete";
    if (!fs::exists(settingsMarker)) {
        for (const char* name : {"default.cfg", "nox.cfg", "noxcore.cfg", "noxsdl.cfg"}) {
            copyMissing(nox::findNoCase(assets, name), user / name);
        }
        mark(settingsMarker);
    }
    // Separate markers let an explicit --save-dir skip importing saves until they are needed.
    // A completed import must not resurrect characters that the player later deletes.
    if (importSaves && !fs::exists(savesMarker)) {
        if (fs::is_directory(saves)) {
            copyTreeMissing(saves, user / "Save");
        }
        mark(savesMarker);
    }
    fs::create_directories(user / "Save");
}

// Editable mods are imported once. Reference mods follow the bundle, as in upstream's build;
// the editor opens those as separate editable copies, never in place.
void prepareUserMods(const std::filesystem::path& bundled, const std::filesystem::path& target)
{
    namespace fs = std::filesystem;
    const auto marker = target / ".bundled-import-complete";
    const bool firstImport = !fs::exists(marker);
    if (!fs::is_directory(bundled)) {
        fs::create_directories(target);
        return; // An unbundled launch must not prevent a later bundled import.
    }
    requireSeparate(bundled, target);
    fs::create_directories(target);
    const auto stage = target.parent_path() / (".mods-import-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool staged = false;
    try {
        for (const auto& entry : fs::directory_iterator(bundled)) {
            const auto destination = target / entry.path().filename();
            const bool exists = fs::exists(destination);
            const bool reference = referenceMod(entry.path());
            if (reference) {
                if (exists && (!referenceMod(destination) || sameTree(entry.path(), destination))) continue;
            } else if (!firstImport || exists) {
                continue; // Editable projects, including their deletions, belong to the user.
            }
            if (!staged) {
                if (!fs::create_directory(stage)) throw std::runtime_error("Cannot stage bundled mods: " + stage.string());
                staged = true;
            }
            const auto copy = stage / entry.path().filename();
            if (entry.is_directory()) copyTreeMissing(entry.path(), copy);
            else copyMissing(entry.path(), copy);
            if (exists) {
                // Atomically exchange complete reference trees; a failure keeps the previous one.
                if (renamex_np(copy.c_str(), destination.c_str(), RENAME_SWAP) != 0) {
                    throw fs::filesystem_error("Cannot update bundled reference mod", copy, destination,
                                               std::error_code(errno, std::generic_category()));
                }
            } else {
                fs::rename(copy, destination);
            }
        }
        if (firstImport) mark(marker);
    } catch (...) {
        std::error_code ignored;
        if (staged) fs::remove_all(stage, ignored);
        throw;
    }
    if (staged) fs::remove_all(stage);
}

} // namespace noxsdl
