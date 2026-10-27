#pragma once
#include "thumbnail/appearance.h"
#include <windows.h>
#include <commctrl.h>
#include <utility>
#include <vector>

class SettingsStyle {
public:
    thumbnail::Appearance colors;
    SettingsStyle() = default;
    ~SettingsStyle();
    SettingsStyle(const SettingsStyle&) = delete;
    SettingsStyle& operator=(const SettingsStyle&) = delete;
    void initialize(HWND window);
    void paint(HWND window);
    void draw_button(HWND window, const DRAWITEMSTRUCT& draw);
    INT_PTR control_color(HWND window, HDC dc, HWND control, bool edit);
    // Repaints the drawn frame around an edit so its focus ring follows the caret.
    void invalidate_field(HWND window, int control) const;
    // Trackbar NM_CUSTOMDRAW: thin palette track and a round thumb instead of the system accent color.
    LRESULT draw_slider(HWND window, const NMCUSTOMDRAW& draw);
    HBRUSH background() const { return background_; }
private:
    HBRUSH background_ = CreateSolidBrush(RGB(242,244,246));
    HBRUSH surface_ = CreateSolidBrush(RGB(255,255,255));
    HBRUSH highlight_ = CreateSolidBrush(RGB(230,243,244));
    HICON small_icon_ = nullptr;
    HICON large_icon_ = nullptr;
    HICON brand_icon_ = nullptr;
    HFONT label_font_ = nullptr;
    HFONT strong_font_ = nullptr;
    std::vector<HFONT> fonts_;
    std::vector<std::pair<int, RECT>> fields_; // Edit control ID and its drawn frame in client pixels.
};
