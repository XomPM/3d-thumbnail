#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace thumbnail {

struct RgbColor {
    std::uint8_t red{}, green{}, blue{};
    bool operator==(const RgbColor&) const = default;
};

struct Appearance {
    RgbColor background{43, 43, 43};
    RgbColor model{214, 214, 214};
    bool operator==(const Appearance&) const = default;
};

inline constexpr wchar_t kAppearanceKey[] = L"Software\\3DThumbnail\\Settings";
RgbColor parse_color(std::wstring_view text);
std::wstring format_color(RgbColor color);
// Invalid/missing settings fall back to defaults. A single value stores both colors atomically.
Appearance load_appearance(const wchar_t* key = kAppearanceKey) noexcept;
void save_appearance(const Appearance& appearance, const wchar_t* key = kAppearanceKey);

} // namespace thumbnail
