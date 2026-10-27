#include "thumbnail/appearance.h"
#include <windows.h>
#include <array>
#include <stdexcept>

namespace thumbnail {
namespace {
unsigned hex_digit(wchar_t digit) {
    if (digit >= L'0' && digit <= L'9') { return digit - L'0'; }
    if (digit >= L'a' && digit <= L'f') { return digit - L'a' + 10; }
    if (digit >= L'A' && digit <= L'F') { return digit - L'A' + 10; }
    throw std::invalid_argument("Color must be #RRGGBB (six hexadecimal digits).");
}
}

RgbColor parse_color(std::wstring_view text) {
    if (text.size() != 7 || text.front() != L'#') {
        throw std::invalid_argument("Color must be #RRGGBB (six hexadecimal digits).");
    }
    const auto channel = [text](std::size_t index) {
        return static_cast<std::uint8_t>(hex_digit(text[index]) * 16 + hex_digit(text[index + 1]));
    };
    return {channel(1), channel(3), channel(5)};
}

std::wstring format_color(RgbColor color) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring text = L"#";
    for (const auto channel : {color.red, color.green, color.blue}) {
        text += digits[channel >> 4];
        text += digits[channel & 15];
    }
    return text;
}

std::uint8_t parse_transparency(std::wstring_view text) {
    if (text.empty() || text.size() > 3 || text.find_first_not_of(L"0123456789") != std::wstring_view::npos) {
        throw std::invalid_argument("Background transparency must be a whole number from 0 to 100.");
    }
    unsigned value = 0;
    for (const auto digit : text) { value = value * 10 + digit - L'0'; }
    if (value > 100) { throw std::invalid_argument("Background transparency must be between 0 and 100."); }
    return static_cast<std::uint8_t>(value);
}

Appearance load_appearance(const wchar_t* key) noexcept {
    std::array<BYTE, 8> data{};
    DWORD length = static_cast<DWORD>(data.size());
    auto status = RegGetValueW(HKEY_CURRENT_USER, key, L"AppearanceV2", RRF_RT_REG_BINARY,
        nullptr, data.data(), &length);
    if (status == ERROR_FILE_NOT_FOUND) {
        length = static_cast<DWORD>(data.size());
        status = RegGetValueW(HKEY_CURRENT_USER, key, L"AppearanceV1", RRF_RT_REG_BINARY, nullptr, data.data(), &length);
        if (status != ERROR_SUCCESS || length != 7 || data[0] != 1) { return {}; }
        return {{data[1], data[2], data[3]}, {data[4], data[5], data[6]}, 0};
    }
    if (status != ERROR_SUCCESS || length != data.size() || data[0] != 2 || data[7] > 100) { return {}; }
    return {{data[1], data[2], data[3]}, {data[4], data[5], data[6]}, data[7]};
}

void save_appearance(const Appearance& appearance, const wchar_t* key) {
    if (appearance.background_transparency > 100) { throw std::invalid_argument("Invalid background transparency."); }
    HKEY opened{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &opened, nullptr) != ERROR_SUCCESS) {
        throw std::runtime_error("Cannot open appearance settings for writing.");
    }
    const std::array<BYTE, 8> data{2, appearance.background.red, appearance.background.green,
        appearance.background.blue, appearance.model.red, appearance.model.green, appearance.model.blue, appearance.background_transparency};
    const auto status = RegSetValueExW(opened, L"AppearanceV2", 0, REG_BINARY, data.data(), static_cast<DWORD>(data.size()));
    RegCloseKey(opened);
    if (status != ERROR_SUCCESS) { throw std::runtime_error("Cannot save appearance settings."); }
}
} // namespace thumbnail
