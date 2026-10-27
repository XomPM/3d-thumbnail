#include "thumbnail/renderer.h"
#include "thumbnail/shell_cache.h"
#include "windows_helpers.h"
#include "settings_ids.h"
#include "settings_style.h"
#include <commdlg.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <cwctype>
#include <cmath>
#include <memory>
#include <thread>

namespace {
using namespace thumbnail;
using Microsoft::WRL::ComPtr;
constexpr UINT kRefreshFinished = WM_APP + 1;
constexpr UINT kRefreshProgress = WM_APP + 2;
constexpr UINT kRefreshDetails = WM_APP + 3;
struct SettingsWindow {
    SettingsStyle style;
    Image preview;
    std::jthread refresh_thread;
    bool refreshing = false;
    std::wstring refresh_errors;
};

Mesh preview_mesh() {
    const Vec3 points[] = {{-1,-.75,0},{1,-.75,0},{1,.75,0},{-1,.75,0},{-1,-.75,.2},{1,-.75,.2},{1,.75,.2},{-1,.75,.2}};
    const unsigned faces[][3] = {{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},
        {1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}};
    Mesh mesh;
    for (const auto& face : faces) { mesh.triangles.push_back({points[face[0]],points[face[1]],points[face[2]]}); }
    for (int i = 0; i < 48; ++i) {
        const double angle = i * 6.283185307179586 / 48;
        const double next = (i + 1) * 6.283185307179586 / 48;
        const auto point = [](double a, double r, double z) { return Vec3{std::cos(a)*r,std::sin(a)*r,z}; };
        const auto quad = [&mesh](Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
            mesh.triangles.push_back({a,b,c}); mesh.triangles.push_back({a,c,d});
        };
        quad(point(angle,.58,.2),point(next,.58,.2),point(next,.58,1.05),point(angle,.58,1.05));
        quad(point(next,.33,.2),point(angle,.33,.2),point(angle,.33,1.05),point(next,.33,1.05));
        quad(point(angle,.58,1.05),point(next,.58,1.05),point(next,.33,1.05),point(angle,.33,1.05));
    }
    return mesh;
}

RgbColor read_color(HWND window, int control) {
    wchar_t text[16]{};
    GetDlgItemTextW(window, control, text, 16);
    return parse_color(text);
}
Appearance read_colors(HWND window) {
    wchar_t text[8]{};
    GetDlgItemTextW(window, IDC_TRANSPARENCY, text, 8);
    return {read_color(window, IDC_BACKGROUND), read_color(window, IDC_MODEL), parse_transparency(text)};
}
void set_colors(HWND window, const Appearance& colors) {
    SetDlgItemTextW(window, IDC_BACKGROUND, format_color(colors.background).c_str());
    SetDlgItemTextW(window, IDC_MODEL, format_color(colors.model).c_str());
    SetDlgItemInt(window, IDC_TRANSPARENCY, colors.background_transparency, FALSE);
    SendDlgItemMessageW(window, IDC_TRANSPARENCY_SLIDER, TBM_SETPOS, TRUE, colors.background_transparency);
}
void update_preview(HWND window, SettingsWindow& state) {
    state.style.colors = read_colors(window);
    SendDlgItemMessageW(window, IDC_TRANSPARENCY_SLIDER, TBM_SETPOS, TRUE, state.style.colors.background_transparency);
    state.preview = render_thumbnail(preview_mesh(), 512, state.style.colors);
    for (std::size_t i = 0; i < state.preview.rgba.size(); i += 4) {
        const auto pixel = i / 4;
        const unsigned checker = ((pixel % state.preview.width / 12 + pixel / state.preview.width / 12) % 2) ? 224 : 248;
        const unsigned alpha = state.preview.rgba[i + 3];
        for (unsigned channel = 0; channel < 3; ++channel) {
            state.preview.rgba[i + channel] = static_cast<std::uint8_t>((state.preview.rgba[i + channel] * alpha + checker * (255 - alpha) + 127) / 255);
        }
        state.preview.rgba[i + 3] = 255; // Checkerboard belongs only to this preview, never the PNG.
        std::swap(state.preview.rgba[i], state.preview.rgba[i + 2]);
    }
    InvalidateRect(GetDlgItem(window, IDC_PREVIEW), nullptr, FALSE);
    InvalidateRect(GetDlgItem(window, IDC_PICK_BACKGROUND), nullptr, FALSE);
    InvalidateRect(GetDlgItem(window, IDC_PICK_MODEL), nullptr, FALSE);
}
void draw_preview(const DRAWITEMSTRUCT& draw, const Image& image) {
    if (image.rgba.empty()) { return; }
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), static_cast<LONG>(image.width), -static_cast<LONG>(image.height), 1, 32, BI_RGB};
    SetStretchBltMode(draw.hDC, HALFTONE);
    const auto brush = CreateSolidBrush(RGB(image.rgba[2], image.rgba[1], image.rgba[0]));
    FillRect(draw.hDC, &draw.rcItem, brush);
    DeleteObject(brush);
    const auto width = draw.rcItem.right - draw.rcItem.left;
    const auto height = draw.rcItem.bottom - draw.rcItem.top;
    const auto size = std::min(width, height);
    StretchDIBits(draw.hDC, draw.rcItem.left + (width - size) / 2, draw.rcItem.top + (height - size) / 2,
        size, size, 0, 0, image.width, image.height, image.rgba.data(), &info, DIB_RGB_COLORS, SRCCOPY);
}
void choose_color(HWND window, int control) {
    static COLORREF custom[16]{};
    const auto color = read_color(window, control);
    CHOOSECOLORW dialog{sizeof(dialog)};
    dialog.hwndOwner = window;
    dialog.rgbResult = RGB(color.red, color.green, color.blue);
    dialog.lpCustColors = custom;
    dialog.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (ChooseColorW(&dialog)) {
        SetDlgItemTextW(window, control, format_color({GetRValue(dialog.rgbResult), GetGValue(dialog.rgbResult), GetBValue(dialog.rgbResult)}).c_str());
    }
}
std::filesystem::path choose_path(HWND window, bool folder) {
    ComPtr<IFileOpenDialog> dialog;
    check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)), "Open file picker");
    DWORD options{};
    check_hresult(dialog->GetOptions(&options), "Read picker options");
    check_hresult(dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST |
        (folder ? FOS_PICKFOLDERS : 0)), "Set picker options");
    if (!folder) {
        const COMDLG_FILTERSPEC filter{L"3D 모델", L"*.stl;*.obj;*.3mf;*.step;*.stp;*.ply;*.glb;*.gltf;*.iges;*.igs;*.fbx"};
        check_hresult(dialog->SetFileTypes(1, &filter), "Set file types");
    }
    const auto result = dialog->Show(window);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) { return {}; }
    check_hresult(result, "Show file picker");
    ComPtr<IShellItem> item;
    check_hresult(dialog->GetResult(&item), "Read selected item");
    PWSTR path{};
    check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path), "Read selected path");
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(path, CoTaskMemFree);
    return std::filesystem::path(path);
}
bool supported(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), std::towlower);
    for (const auto* value : {L".stl",L".obj",L".3mf",L".step",L".stp",L".ply",L".glb",L".gltf",L".iges",L".igs",L".fbx"}) {
        if (extension == value) { return true; }
    }
    return false;
}
void post_status(HWND window, UINT message, std::wstring text) {
    auto owned = std::make_unique<std::wstring>(std::move(text));
    if (PostMessageW(window, message, 0, reinterpret_cast<LPARAM>(owned.get()))) { owned.release(); }
}
void set_busy(HWND window, SettingsWindow& state, bool busy) {
    state.refreshing = busy;
    for (const int control : {IDC_SAVE,IDC_DEFAULTS,IDC_REFRESH_FILE,IDC_REFRESH_FOLDER,IDC_BACKGROUND,IDC_MODEL,IDC_PICK_BACKGROUND,IDC_PICK_MODEL,IDC_TRANSPARENCY,IDC_TRANSPARENCY_SLIDER}) {
        EnableWindow(GetDlgItem(window, control), !busy);
    }
    EnableWindow(GetDlgItem(window, IDC_CANCEL_REFRESH), busy);
    EnableWindow(GetDlgItem(window, IDC_REFRESH_DETAILS), !busy && !state.refresh_errors.empty());
}
std::wstring error_text(const char* message) {
    const std::string text(message);
    const auto length = static_cast<int>(std::min<std::size_t>(text.size(), 2048));
    const auto count = MultiByteToWideChar(CP_UTF8, 0, text.data(), length, nullptr, 0);
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), count);
    return result;
}
// One line for the status bar: prefer the worker's "Error:" line over the generic wrapper text.
std::wstring short_error(const std::wstring& text) {
    std::size_t start = text.find(L"Error: ");
    start = start == std::wstring::npos ? 0 : start + 7;
    auto line = text.substr(start, text.find_first_of(L"\r\n", start) - start);
    if (line.size() > 160) { line = line.substr(0, 157) + L"..."; }
    return line;
}
void refresh_selection(HWND window, SettingsWindow& state, bool folder) {
    const auto path = choose_path(window, folder);
    if (path.empty()) { return; }
    save_appearance(read_colors(window));
    state.refresh_errors.clear();
    state.refresh_thread = std::jthread([window, path, folder](std::stop_token stop) {
        unsigned done = 0, failed = 0;
        std::wstring detail, first_failure;
        try {
            const ComApartment apartment;
            std::vector<std::filesystem::path> files;
            if (folder) {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (stop.stop_requested()) { break; }
                    if (entry.is_regular_file() && supported(entry.path())) {
                        if (files.size() == 1000) { throw std::runtime_error("Folder contains more than 1,000 supported files. Select a smaller folder."); }
                        files.push_back(entry.path());
                    }
                }
            } else { files.push_back(path); }
            for (const auto& file : files) {
                if (stop.stop_requested()) { break; }
                try { refresh_thumbnails(file); ++done; }
                catch (const std::exception& error) {
                    ++failed;
                    const auto message = error_text(error.what());
                    if (first_failure.empty()) { first_failure = file.filename().wstring() + L": " + short_error(message); }
                    if (failed <= 10) { detail += file.filename().wstring() + L"\r\n" + message + L"\r\n\r\n"; }
                }
                post_status(window, kRefreshProgress, L"갱신 " + std::to_wstring(done) + L" / 실패 " + std::to_wstring(failed) + L" — " + file.filename().wstring());
            }
        } catch (const std::exception& error) {
            ++failed;
            const auto message = error_text(error.what());
            if (first_failure.empty()) { first_failure = path.filename().wstring() + L": " + short_error(message); }
            detail += path.filename().wstring() + L"\r\n" + message;
        }
        if (failed > 10) { detail += L"처음 10개의 실패만 표시합니다.\r\n"; }
        post_status(window, kRefreshDetails, std::move(detail));
        post_status(window, kRefreshFinished, (stop.stop_requested() ? L"중단됨. " : L"완료. ") +
            std::wstring(L"갱신 ") + std::to_wstring(done) + L"개 / 실패 " + std::to_wstring(failed) + L"개." +
            (failed ? L" 첫 실패 — " + first_failure + L" (실패 상세 확인)" : std::wstring()));
    });
    set_busy(window, state, true);
    SetDlgItemTextW(window, IDC_STATUS, L"색상을 저장했습니다. 썸네일 갱신 중…");
}

INT_PTR CALLBACK error_dialog(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_INITDIALOG) {
        SetDlgItemTextW(window, IDC_ERROR_TEXT, reinterpret_cast<const wchar_t*>(lparam));
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wparam) == IDC_COPY_ERRORS) {
        SendDlgItemMessageW(window, IDC_ERROR_TEXT, EM_SETSEL, 0, -1);
        SendDlgItemMessageW(window, IDC_ERROR_TEXT, WM_COPY, 0, 0);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && (LOWORD(wparam) == IDCANCEL || LOWORD(wparam) == IDOK))) {
        EndDialog(window, 0);
        return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK settings_dialog(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            state = reinterpret_cast<SettingsWindow*>(lparam);
            SetWindowLongPtrW(window, DWLP_USER, lparam);
            state->style.initialize(window);
            for (const auto control : {IDC_BACKGROUND, IDC_MODEL}) { SendDlgItemMessageW(window, control, EM_SETLIMITTEXT, 7, 0); }
            SendDlgItemMessageW(window, IDC_TRANSPARENCY, EM_SETLIMITTEXT, 3, 0);
            SendDlgItemMessageW(window, IDC_TRANSPARENCY_SLIDER, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
            SendDlgItemMessageW(window, IDC_TRANSPARENCY_SLIDER, TBM_SETPAGESIZE, 0, 10);
            set_colors(window, load_appearance());
            update_preview(window, *state);
            return TRUE;
        }
        if (!state) { return FALSE; }
        if (message == kRefreshDetails) {
            const std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
            state->refresh_errors = *text;
            return TRUE;
        }
        if (message == WM_HSCROLL && reinterpret_cast<HWND>(lparam) == GetDlgItem(window, IDC_TRANSPARENCY_SLIDER)) {
            SetDlgItemInt(window, IDC_TRANSPARENCY, static_cast<UINT>(SendDlgItemMessageW(window, IDC_TRANSPARENCY_SLIDER, TBM_GETPOS, 0, 0)), FALSE);
            return TRUE;
        }
        if (message == WM_NOTIFY) {
            const auto* header = reinterpret_cast<const NMHDR*>(lparam);
            if (header->idFrom != IDC_TRANSPARENCY_SLIDER || header->code != NM_CUSTOMDRAW) { return FALSE; }
            SetWindowLongPtrW(window, DWLP_MSGRESULT, state->style.draw_slider(window, *reinterpret_cast<const NMCUSTOMDRAW*>(lparam)));
            return TRUE;
        }
        if (message == WM_PAINT) { state->style.paint(window); return TRUE; }
        if (message == WM_CTLCOLORDLG) { return reinterpret_cast<INT_PTR>(state->style.background()); }
        if (message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
            return state->style.control_color(window, reinterpret_cast<HDC>(wparam), reinterpret_cast<HWND>(lparam), message == WM_CTLCOLOREDIT);
        }
        if (message == kRefreshFinished || message == kRefreshProgress) {
            const std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
            SetDlgItemTextW(window, IDC_STATUS, text->c_str());
            if (message == kRefreshFinished) { state->refresh_thread.join(); set_busy(window, *state, false); }
            return TRUE;
        }
        if (message == WM_DRAWITEM && wparam == IDC_PREVIEW) {
            draw_preview(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam), state->preview);
            return TRUE;
        }
        if (message == WM_DRAWITEM) { state->style.draw_button(window, *reinterpret_cast<DRAWITEMSTRUCT*>(lparam)); return TRUE; }
        if (message == WM_TIMER) {
            KillTimer(window, 1);
            try { update_preview(window, *state); }
            catch (const std::invalid_argument&) { SetDlgItemTextW(window, IDC_STATUS, L"색상은 #RRGGBB, 배경 투명도는 0~100으로 입력하세요."); }
            return TRUE;
        }
        if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wparam) == IDCANCEL)) {
            if (state->refreshing) {
                state->refresh_thread.request_stop();
                SetDlgItemTextW(window, IDC_STATUS, L"현재 파일의 갱신이 끝나면 중단합니다. 완료 후 닫아 주세요.");
            } else { EndDialog(window, 0); }
            return TRUE;
        }
        if (message != WM_COMMAND) { return FALSE; }
        if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) {
            state->style.invalidate_field(window, LOWORD(wparam));
            return TRUE;
        }
        if (HIWORD(wparam) == EN_CHANGE) { SetTimer(window, 1, 250, nullptr); return TRUE; }
        switch (LOWORD(wparam)) {
        case IDC_PICK_BACKGROUND: choose_color(window, IDC_BACKGROUND); break;
        case IDC_PICK_MODEL: choose_color(window, IDC_MODEL); break;
        case IDC_SAVE:
            save_appearance(read_colors(window));
            update_preview(window, *state);
            SetDlgItemTextW(window, IDC_STATUS, L"저장했습니다. 이미 표시된 썸네일은 파일이나 폴더를 선택해 갱신하세요.");
            break;
        case IDC_DEFAULTS: set_colors(window, {}); SetDlgItemTextW(window, IDC_STATUS, L"기본값을 불러왔습니다. 저장을 눌러야 적용됩니다."); break;
        case IDC_REFRESH_FILE: refresh_selection(window, *state, false); break;
        case IDC_REFRESH_FOLDER: refresh_selection(window, *state, true); break;
        case IDC_REFRESH_DETAILS:
            DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_REFRESH_DETAILS), window,
                error_dialog, reinterpret_cast<LPARAM>(state->refresh_errors.c_str()));
            break;
        case IDC_CANCEL_REFRESH: state->refresh_thread.request_stop(); SetDlgItemTextW(window, IDC_STATUS, L"현재 파일의 갱신이 끝나면 중단합니다."); break;
        default: return FALSE;
        }
        return TRUE;
    } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "3D Thumbnail", MB_ICONERROR | MB_OK);
        return TRUE;
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
        if (!InitCommonControlsEx(&controls)) { throw std::runtime_error("Cannot initialize settings controls."); }
        const thumbnail::ComApartment apartment(COINIT_APARTMENTTHREADED);
        SettingsWindow state;
        if (DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), nullptr, settings_dialog, reinterpret_cast<LPARAM>(&state)) == -1) {
            throw std::runtime_error("Cannot open settings window.");
        }
        return 0;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "3D Thumbnail", MB_ICONERROR | MB_OK);
        return 1;
    }
}
