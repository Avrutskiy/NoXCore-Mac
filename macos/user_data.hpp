// Writable settings and saves, separate from installed game assets.
#pragma once

#include <filesystem>

namespace noxsdl {

/// Imports missing settings and, when requested, saves once. Throws on filesystem errors.
void prepareUserData(const std::filesystem::path& assets, const std::filesystem::path& user,
                     bool importSaves);

/// Imports editable mods once; refreshes bundled reference mods, which the editor opens as copies.
void prepareUserMods(const std::filesystem::path& bundled, const std::filesystem::path& target);

} // namespace noxsdl
