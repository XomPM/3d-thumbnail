#include "settings_style.h"
#include "settings_ids.h"
#include <commctrl.h>
#include <algorithm>
#include <stdexcept>

namespace {
// One small palette, derived from the DMIND teal icon. Every surface and text color comes from here.
constexpr COLORREF kCanvas = RGB(242,244,246);
constexpr COLORREF kSurface = RGB(255,255,255);
constexpr COLORREF kHairline = RGB(222,227,232);
constexpr COLORREF kInk = RGB(24,34,44);
constexpr COLORREF kMuted = RGB(104,116,128);
constexpr COLORREF kAccent = RGB(10,124,134);
constexpr COLORREF kAccentPressed = RGB(7,98,107);
constexpr COLORREF kAccentSoft = RGB(230,243,244);
constexpr COLORREF kDisabledText = RGB(160,170,178);

// Layout in dialog units, shared with settings.rc.
constexpr RECT kAppearanceCard{16,52,262,258};
constexpr RECT kPreviewCard{272,52,444,258};
constexpr RECT kRefreshCard{16,268,444,342};
constexpr RECT kDivider{28,176,250,177};
constexpr RECT kPreviewTile{288,80,428,232};
constexpr RECT kLogo{18,11,44,37};

RECT dialog_rect(HWND window, RECT rectangle) { MapDialogRect(window, &rectangle); return rectangle; }
int scale(HWND window, int pixels) { return MulDiv(pixels, static_cast<int>(GetDpiForWindow(window)), 96); }
void rounded(HDC dc, RECT rectangle, COLORREF fill, COLORREF border, int radius) {
    const auto brush = CreateSolidBrush(fill);
    const auto pen = CreatePen(PS_SOLID, 1, border);
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rectangle.left, rectangle.top, rectangle.right, rectangle.bottom, radius, radius);
    SelectObject(dc, old_pen); SelectObject(dc, old_brush);
    DeleteObject(pen); DeleteObject(brush);
}
bool muted(int id) {
    switch (id) {
    case IDC_SUBTITLE: case IDC_VERSION: case IDC_CONTACT: case IDC_STATUS:
    case IDC_HELP_TRANSPARENCY: case IDC_NOTE: case IDC_REFRESH_HELP: return true;
    default: return false;
    }
}
}

SettingsStyle::~SettingsStyle() {
    for (const auto font : fonts_) { DeleteObject(font); }
    for (const auto icon : {small_icon_, large_icon_, brand_icon_}) { if (icon) { DestroyIcon(icon); } }
    DeleteObject(background_); DeleteObject(surface_); DeleteObject(highlight_);
}

void SettingsStyle::initialize(HWND window) {
    const auto dpi = GetDpiForWindow(window);
    const auto load_icon = [](int size) {
        const auto icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, 0));
        if (!icon) { throw std::runtime_error("Cannot load application icon."); }
        return icon;
    };
    small_icon_ = load_icon(GetSystemMetricsForDpi(SM_CXSMICON, dpi));
    large_icon_ = load_icon(GetSystemMetricsForDpi(SM_CXICON, dpi));
    const auto logo = dialog_rect(window, kLogo);
    brand_icon_ = load_icon(std::min(logo.right - logo.left, logo.bottom - logo.top));
    SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon_));
    SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon_));
    const auto font = [&](int points, int weight, const wchar_t* face) {
        const auto result = CreateFontW(-MulDiv(points, static_cast<int>(dpi), 72), 0, 0, 0, weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, face);
        if (!result) { throw std::runtime_error("Cannot create settings font."); }
        fonts_.push_back(result);
        return result;
    };
    const auto display = font(17, FW_SEMIBOLD, L"Segoe UI Variable Display");
    const auto heading = font(10, FW_BOLD, L"Malgun Gothic");
    const auto numbers = font(10, FW_NORMAL, L"Consolas");
    label_font_ = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    strong_font_ = font(9, FW_BOLD, L"Malgun Gothic");
    SendDlgItemMessageW(window, IDC_BRAND, WM_SETFONT, reinterpret_cast<WPARAM>(display), TRUE);
    for (const auto id : {IDC_COLOR_TITLE, IDC_PREVIEW_TITLE, IDC_REFRESH_TITLE}) {
        SendDlgItemMessageW(window, id, WM_SETFONT, reinterpret_cast<WPARAM>(heading), TRUE);
    }
    // Borderless edits sit centered inside a frame drawn by paint(), so every field matches the buttons' height.
    for (const auto id : {IDC_BACKGROUND, IDC_MODEL, IDC_TRANSPARENCY}) {
        const auto edit = GetDlgItem(window, id);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(numbers), TRUE);
        RECT frame{}; GetWindowRect(edit, &frame);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&frame), 2);
        fields_.emplace_back(id, frame);
        const auto dc = GetDC(edit);
        const auto old = SelectObject(dc, numbers);
        TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
        SelectObject(dc, old); ReleaseDC(edit, dc);
        const auto text_height = metrics.tmHeight;
        const auto left = scale(window, 8);
        const auto right = scale(window, id == IDC_TRANSPARENCY ? 22 : 8); // Room for the drawn "%" unit.
        SetWindowPos(edit, nullptr, frame.left + left, frame.top + (frame.bottom - frame.top - text_height) / 2,
            frame.right - frame.left - left - right, text_height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SettingsStyle::paint(HWND window) {
    PAINTSTRUCT paint{};
    const auto dc = BeginPaint(window, &paint);
    RECT bounds{}; GetClientRect(window, &bounds);
    FillRect(dc, &bounds, background_);
    const auto radius = scale(window, 12);
    for (const auto card : {kAppearanceCard, kPreviewCard, kRefreshCard}) {
        rounded(dc, dialog_rect(window, card), kSurface, kHairline, radius);
    }
    auto divider = dialog_rect(window, kDivider);
    divider.bottom = divider.top + 1; // A true hairline at every DPI.
    const auto line = CreateSolidBrush(kHairline);
    FillRect(dc, &divider, line);
    DeleteObject(line);
    // Signature: the preview sits on a selected-item highlight, the way the thumbnail appears in Explorer.
    rounded(dc, dialog_rect(window, kPreviewTile), kAccentSoft, kAccentSoft, scale(window, 8));
    const auto focused = GetFocus();
    for (const auto& [id, frame] : fields_) {
        const bool active = focused == GetDlgItem(window, id);
        rounded(dc, frame, kSurface, active ? kAccent : kHairline, scale(window, 6));
        if (active) {
            RECT inner = frame; InflateRect(&inner, -1, -1);
            rounded(dc, inner, kSurface, kAccent, scale(window, 6));
        }
        if (id == IDC_TRANSPARENCY) {
            RECT unit = frame; unit.right -= scale(window, 8);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kMuted);
            const auto old = SelectObject(dc, label_font_);
            DrawTextW(dc, L"%", 1, &unit, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
            SelectObject(dc, old);
        }
    }
    const auto logo = dialog_rect(window, kLogo);
    const auto size = std::min(logo.right - logo.left, logo.bottom - logo.top);
    DrawIconEx(dc, logo.left, logo.top, brand_icon_, size, size, 0, nullptr, DI_NORMAL);
    EndPaint(window, &paint);
}

void SettingsStyle::invalidate_field(HWND window, int control) const {
    for (const auto& [id, frame] : fields_) {
        if (id == control) { RECT area = frame; InflateRect(&area, 2, 2); InvalidateRect(window, &area, TRUE); }
    }
}

void SettingsStyle::draw_button(HWND window, const DRAWITEMSTRUCT& draw) {
    const bool disabled = (draw.itemState & ODS_DISABLED) != 0;
    const bool pressed = (draw.itemState & ODS_SELECTED) != 0;
    const bool focused = (draw.itemState & ODS_FOCUS) != 0;
    const auto radius = scale(window, 6);
    // Clear the corners outside the rounded shape with the card surface.
    FillRect(draw.hDC, &draw.rcItem, surface_);
    if (draw.CtlID == IDCANCEL) { FillRect(draw.hDC, &draw.rcItem, background_); }
    if (draw.CtlID == IDC_PICK_BACKGROUND || draw.CtlID == IDC_PICK_MODEL) {
        const auto color = draw.CtlID == IDC_PICK_BACKGROUND ? colors.background : colors.model;
        rounded(draw.hDC, draw.rcItem, kSurface, focused ? kAccent : kHairline, radius);
        RECT swatch = draw.rcItem;
        InflateRect(&swatch, -scale(window, pressed ? 4 : 3), -scale(window, pressed ? 4 : 3));
        rounded(draw.hDC, swatch, RGB(color.red, color.green, color.blue), RGB(color.red, color.green, color.blue), scale(window, 4));
        return;
    }
    const bool primary = draw.CtlID == IDC_SAVE;
    const auto fill = disabled ? kCanvas : primary ? (pressed ? kAccentPressed : kAccent) : (pressed ? kCanvas : kSurface);
    const auto border = disabled ? kHairline : primary ? fill : (focused ? kAccent : kHairline);
    rounded(draw.hDC, draw.rcItem, fill, border, radius);
    if (focused && primary) {
        RECT ring = draw.rcItem; InflateRect(&ring, -scale(window, 2), -scale(window, 2));
        rounded(draw.hDC, ring, fill, RGB(255,255,255), radius);
    }
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, disabled ? kDisabledText : primary ? RGB(255,255,255) : kInk);
    const auto old_font = SelectObject(draw.hDC, primary ? strong_font_ : label_font_);
    wchar_t text[128]{}; GetWindowTextW(draw.hwndItem, text, 128);
    RECT content = draw.rcItem;
    DrawTextW(draw.hDC, text, -1, &content, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(draw.hDC, old_font);
}

INT_PTR SettingsStyle::control_color(HWND window, HDC dc, HWND control, bool edit) {
    const auto id = GetDlgCtrlID(control);
    SetTextColor(dc, muted(id) ? kMuted : kInk);
    SetBkMode(dc, TRANSPARENT);
    if (edit) { SetBkColor(dc, kSurface); return reinterpret_cast<INT_PTR>(surface_); }
    RECT rectangle{}; GetWindowRect(control, &rectangle);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rectangle), 2);
    const POINT origin{rectangle.left, rectangle.top};
    if (id == IDC_PREVIEW_NAME) {
        SetBkColor(dc, kAccentSoft);
        return reinterpret_cast<INT_PTR>(highlight_);
    }
    for (const auto card : {kAppearanceCard, kPreviewCard, kRefreshCard}) {
        const auto area = dialog_rect(window, card);
        if (PtInRect(&area, origin)) { SetBkColor(dc, kSurface); return reinterpret_cast<INT_PTR>(surface_); }
    }
    SetBkColor(dc, kCanvas);
    return reinterpret_cast<INT_PTR>(background_);
}

LRESULT SettingsStyle::draw_slider(HWND window, const NMCUSTOMDRAW& draw) {
    if (draw.dwDrawStage == CDDS_PREPAINT) { return CDRF_NOTIFYITEMDRAW; }
    if (draw.dwDrawStage != CDDS_ITEMPREPAINT) { return CDRF_DODEFAULT; }
    const auto slider = draw.hdr.hwndFrom;
    RECT client{}; GetClientRect(slider, &client);
    RECT thumb{}; SendMessageW(slider, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    const auto middle = (client.top + client.bottom) / 2;
    const auto thumb_x = (thumb.left + thumb.right) / 2;
    if (draw.dwItemSpec == TBCD_CHANNEL) {
        FillRect(draw.hdc, &client, surface_);
        const auto half = scale(window, 2);
        RECT track{draw.rc.left, middle - half, draw.rc.right, middle + half};
        rounded(draw.hdc, track, kHairline, kHairline, half * 2);
        RECT filled{draw.rc.left, track.top, std::max(draw.rc.left + half * 2, thumb_x), track.bottom};
        rounded(draw.hdc, filled, kAccent, kAccent, half * 2);
        return CDRF_SKIPDEFAULT;
    }
    if (draw.dwItemSpec == TBCD_THUMB) {
        const auto radius = scale(window, 8);
        RECT knob{thumb_x - radius, middle - radius, thumb_x + radius, middle + radius};
        if (GetFocus() == slider) {
            RECT halo = knob; InflateRect(&halo, scale(window, 3), scale(window, 3));
            rounded(draw.hdc, halo, kAccentSoft, kAccentSoft, radius * 3);
        }
        rounded(draw.hdc, knob, kSurface, kAccent, radius * 2);
        RECT core = knob; InflateRect(&core, -scale(window, 2), -scale(window, 2));
        rounded(draw.hdc, core, kSurface, kAccent, radius * 2);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_SKIPDEFAULT; // No tick marks.
}
