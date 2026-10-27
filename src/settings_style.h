#pragma once
#include "thumbnail/appearance.h"
#include <windows.h>
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
    HBRUSH background() const { return background_; }
private:
    HBRUSH background_ = CreateSolidBrush(RGB(244,246,248));
    HBRUSH surface_ = CreateSolidBrush(RGB(255,255,255));
    HICON small_icon_ = nullptr;
    HICON large_icon_ = nullptr;
    HICON brand_icon_ = nullptr;
    std::vector<HFONT> fonts_;
};
