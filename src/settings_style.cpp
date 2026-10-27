#include "settings_style.h"
#include "settings_ids.h"
#include <algorithm>

namespace {
constexpr COLORREF kInk = RGB(23,43,58);
constexpr COLORREF kMuted = RGB(96,114,130);
constexpr COLORREF kAccent = RGB(8,127,140);
constexpr COLORREF kBorder = RGB(221,229,234);
RECT dialog_rect(HWND window, RECT rectangle) { MapDialogRect(window, &rectangle); return rectangle; }
void rounded(HDC dc, RECT rectangle, COLORREF fill, COLORREF border, int radius) {
    const auto brush = CreateSolidBrush(fill);
    const auto pen = CreatePen(PS_SOLID, 1, border);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush);
    DeleteObject(pen); DeleteObject(brush);
}
}

SettingsStyle::~SettingsStyle() {
    for (const auto font : fonts_) { DeleteObject(font); }
    DeleteObject(background_); DeleteObject(surface_);
}
void SettingsStyle::initialize(HWND window) {
    const auto font = [&](int points, int weight, const wchar_t* face) {
        const auto result = CreateFontW(-MulDiv(points, GetDpiForWindow(window), 72), 0, 0, 0, weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, face);
        fonts_.push_back(result);
        return result;
    };
    const auto display = font(20, FW_SEMIBOLD, L"Segoe UI Variable Display");
    const auto heading = font(10, FW_BOLD, L"Malgun Gothic");
    const auto utility = font(12, FW_NORMAL, L"Consolas");
    SendDlgItemMessageW(window, IDC_BRAND, WM_SETFONT, reinterpret_cast<WPARAM>(display), TRUE);
    for (const auto id : {IDC_COLOR_TITLE, IDC_PREVIEW_TITLE, IDC_REFRESH_TITLE, IDC_CREATOR}) {
        SendDlgItemMessageW(window, id, WM_SETFONT, reinterpret_cast<WPARAM>(heading), TRUE);
    }
    for (const auto id : {IDC_BACKGROUND, IDC_MODEL}) { SendDlgItemMessageW(window, id, WM_SETFONT, reinterpret_cast<WPARAM>(utility), TRUE); }
}
void SettingsStyle::paint(HWND window) {
    PAINTSTRUCT paint{};
    const auto dc = BeginPaint(window, &paint);
    RECT bounds{}; GetClientRect(window, &bounds);
    FillRect(dc, &bounds, background_);
    for (const auto rectangle : {RECT{16,74,224,250}, RECT{236,74,444,250}, RECT{16,262,444,336}}) {
        rounded(dc, dialog_rect(window, rectangle), RGB(255,255,255), kBorder, 14);
    }
    // An isometric cube marks this model-oriented tool without a raster asset.
    const auto logo = dialog_rect(window, {20,19,40,39});
    const LONG cx = (logo.left + logo.right) / 2;
    const LONG cy = (logo.top + logo.bottom) / 2;
    POINT top[] = {{cx,logo.top},{logo.right,logo.top+5},{cx,cy},{logo.left,logo.top+5}};
    POINT left[] = {{logo.left,logo.top+5},{cx,cy},{cx,logo.bottom},{logo.left,logo.bottom-5}};
    POINT right[] = {{cx,cy},{logo.right,logo.top+5},{logo.right,logo.bottom-5},{cx,logo.bottom}};
    const auto polygon = [dc](POINT* points, COLORREF color) {
        const auto brush = CreateSolidBrush(color);
        const auto old_brush = SelectObject(dc, brush);
        const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, points, 4);
        SelectObject(dc, old_pen); SelectObject(dc, old_brush); DeleteObject(brush);
    };
    polygon(top, RGB(68,170,180)); polygon(left, RGB(8,127,140)); polygon(right, RGB(11,91,108));
    EndPaint(window, &paint);
}
void SettingsStyle::draw_button(HWND window, const DRAWITEMSTRUCT& draw) {
    const bool primary = draw.CtlID == IDC_SAVE;
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const auto fill = disabled ? RGB(244,246,248) : primary ? (pressed ? RGB(6,101,112) : kAccent) : (pressed ? RGB(235,241,244) : RGB(255,255,255));
    rounded(draw.hDC, draw.rcItem, fill, primary && !disabled ? fill : kBorder, 10);
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, disabled ? RGB(150,161,170) : primary ? RGB(255,255,255) : kInk);
    const auto font = reinterpret_cast<HFONT>(SendMessageW(draw.hwndItem, WM_GETFONT, 0, 0));
    const auto old_font = SelectObject(draw.hDC, font);
    wchar_t text[128]{}; GetWindowTextW(draw.hwndItem, text, 128);
    RECT content = draw.rcItem;
    if (draw.CtlID == IDC_PICK_BACKGROUND || draw.CtlID == IDC_PICK_MODEL) {
        const auto color = draw.CtlID == IDC_PICK_BACKGROUND ? colors.background : colors.model;
        const auto inset = MulDiv(7, GetDpiForWindow(window), 96);
        const auto size = (content.bottom - content.top) / 2;
        RECT swatch{content.left + inset, content.top + size / 2, content.left + inset + size, content.top + size / 2 + size};
        rounded(draw.hDC, swatch, RGB(color.red,color.green,color.blue), kBorder, 4);
        content.left += inset + size;
    }
    DrawTextW(draw.hDC, text, -1, &content, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    if ((draw.itemState & ODS_FOCUS) != 0) { RECT focus = draw.rcItem; InflateRect(&focus,-4,-4); DrawFocusRect(draw.hDC,&focus); }
    SelectObject(draw.hDC, old_font);
}
INT_PTR SettingsStyle::control_color(HWND window, HDC dc, HWND control, bool edit) {
    const auto id = GetDlgCtrlID(control);
    SetTextColor(dc, (id == IDC_SUBTITLE || id == IDC_CONTACT || id == IDC_STATUS) ? kMuted : kInk);
    SetBkMode(dc, TRANSPARENT);
    if (edit) { SetBkColor(dc, RGB(255,255,255)); return reinterpret_cast<INT_PTR>(surface_); }
    RECT rectangle{}; GetWindowRect(control, &rectangle);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rectangle), 2);
    const auto top_card = dialog_rect(window, {16,74,444,250});
    const auto lower_card = dialog_rect(window, {16,262,444,336});
    const POINT origin{rectangle.left,rectangle.top};
    return reinterpret_cast<INT_PTR>((PtInRect(&top_card,origin) || PtInRect(&lower_card,origin)) ? surface_ : background_);
}
