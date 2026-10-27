#pragma once
#include <filesystem>
namespace thumbnail {
// Caller initializes COM. Refreshes this item only; never deletes the global cache.
void refresh_thumbnails(const std::filesystem::path& input);
void notify_changed(const std::filesystem::path& input);
}
