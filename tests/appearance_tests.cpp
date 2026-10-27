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
        const Appearance custom{{0,128,255},{255,64,0}};
        save_appearance(custom, key.c_str());
        require(load_appearance(key.c_str()) == custom, "Settings were not persisted.");
        const BYTE broken[] = {9,1,2,3,4,5,6};
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV1", REG_BINARY, broken, sizeof(broken)) == ERROR_SUCCESS, "Cannot set corrupt test data.");
        require(load_appearance(key.c_str()) == Appearance{}, "Unknown settings version accepted.");
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV1", REG_BINARY, broken, 2) == ERROR_SUCCESS, "Cannot truncate test data.");
        require(load_appearance(key.c_str()) == Appearance{}, "Truncated settings accepted.");
        require(RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), L"AppearanceV1", REG_SZ, L"invalid", 16) == ERROR_SUCCESS, "Cannot change test value type.");
        require(load_appearance(key.c_str()) == Appearance{}, "Wrong registry type accepted.");
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
