#include "thumbnail/appearance.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>

using namespace thumbnail;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
int main() {
    const auto key = L"Software\\3DThumbnailTests\\appearance-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    try {
        require(parse_color(L"#aB012f") == RgbColor{171,1,47}, "Case-insensitive HEX parsing failed.");
        require(format_color({0,16,255}) == L"#0010FF", "HEX round-trip failed.");
        for (const auto* text : {L"", L"#fff", L"ffffff", L"#00000g", L"#000000 ", L"#00000000"}) {
            bool rejected = false;
            try { parse_color(text); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid color accepted.");
        }
        require(load_appearance(key.c_str()) == Appearance{}, "Missing settings must use defaults.");
        require(parse_transparency(L"0") == 0 && parse_transparency(L"50") == 50 && parse_transparency(L"100") == 100, "Transparency endpoints failed.");
        for (const auto* text : {L"", L"-1", L"101", L"1.5", L"1000", L" 50", L"50%"}) {
            bool rejected = false;
            try { parse_transparency(text); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid transparency accepted.");
        }
        const BYTE legacy[] = {1,10,20,30,40,50,60};
        HKEY legacy_key{};
        require(RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &legacy_key, nullptr) == ERROR_SUCCESS, "Cannot create legacy test key.");
        RegCloseKey(legacy_key);
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV1", REG_BINARY, legacy, sizeof(legacy)) == ERROR_SUCCESS, "Cannot set legacy data.");
        require(load_appearance(key.c_str()) == Appearance{{10,20,30},{40,50,60},0}, "Legacy colors were not retained as opaque.");
        const Appearance custom{{0,128,255},{255,64,0},50};
        save_appearance(custom, key.c_str());
        require(load_appearance(key.c_str()) == custom, "Settings were not persisted.");
        const BYTE broken[] = {9,1,2,3,4,5,6,50};
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV2", REG_BINARY, broken, sizeof(broken)) == ERROR_SUCCESS, "Cannot set corrupt test data.");
        require(load_appearance(key.c_str()) == Appearance{}, "Unknown settings version accepted.");
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV2", REG_BINARY, broken, 2) == ERROR_SUCCESS, "Cannot truncate test data.");
        require(load_appearance(key.c_str()) == Appearance{}, "Truncated settings accepted.");
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV2", REG_SZ, L"invalid", 16) == ERROR_SUCCESS, "Cannot change test value type.");
        require(load_appearance(key.c_str()) == Appearance{}, "Wrong registry type accepted.");
        const BYTE invalid_percent[] = {2,1,2,3,4,5,6,101};
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV2", REG_BINARY, invalid_percent, sizeof(invalid_percent)) == ERROR_SUCCESS, "Cannot set invalid percent.");
        require(load_appearance(key.c_str()) == Appearance{}, "Out-of-range stored transparency accepted.");
        for (const auto percent : {0,100}) {
            const Appearance edge{{1,2,3},{4,5,6},static_cast<std::uint8_t>(percent)};
            save_appearance(edge, key.c_str());
            require(load_appearance(key.c_str()) == edge, "Transparency endpoint not persisted.");
        }
        save_appearance({}, key.c_str());
        require(load_appearance(key.c_str()) == Appearance{}, "Default restoration failed.");
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        std::cout << "PASS: HEX validation, persistence, corrupt settings fallback and defaults.\n";
        return 0;
    } catch (const std::exception& error) {
        RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        std::cerr << error.what() << '\n';
        return 1;
    }
}
