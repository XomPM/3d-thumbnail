#include "thumbnail/renderer.h"
#include "thumbnail/shell_cache.h"
#include "windows_helpers.h"
#include "settings_ids.h"
#include "settings_style.h"
#include <commdlg.h>
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
struct SettingsWindow {
    SettingsStyle style;
    Image preview;
    std::jthread refresh_thread;
    bool refreshing = false;
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
    return {read_color(window, IDC_BACKGROUND), read_color(window, IDC_MODEL)};
}
void set_colors(HWND window, const Appearance& colors) {
    SetDlgItemTextW(window, IDC_BACKGROUND, format_color(colors.background).c_str());
    SetDlgItemTextW(window, IDC_MODEL, format_color(colors.model).c_str());
}
void update_preview(HWND window, SettingsWindow& state) {
    state.style.colors = read_colors(window);
    state.preview = render_thumbnail(preview_mesh(), 256, state.style.colors);
    for (std::size_t i = 0; i < state.preview.rgba.size(); i += 4) {
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
    for (const int control : {IDC_SAVE,IDC_DEFAULTS,IDC_REFRESH_FILE,IDC_REFRESH_FOLDER,IDC_BACKGROUND,IDC_MODEL,IDC_PICK_BACKGROUND,IDC_PICK_MODEL}) {
        EnableWindow(GetDlgItem(window, control), !busy);
    }
    EnableWindow(GetDlgItem(window, IDC_CANCEL_REFRESH), busy);
}
void refresh_selection(HWND window, SettingsWindow& state, bool folder) {
    const auto path = choose_path(window, folder);
    if (path.empty()) { return; }
    save_appearance(read_colors(window));
    set_busy(window, state, true);
    SetDlgItemTextW(window, IDC_STATUS, L"색상을 저장했습니다. 썸네일 갱신 중…");
    state.refresh_thread = std::jthread([window, path, folder](std::stop_token stop) {
        unsigned done = 0, failed = 0;
        std::wstring detail;
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
                catch (const std::exception&) { ++failed; }
                post_status(window, kRefreshProgress, L"갱신 " + std::to_wstring(done) + L" / 실패 " + std::to_wstring(failed) + L" — " + file.filename().wstring());
            }
        } catch (const std::exception& error) {
            ++failed;
            const std::string text(error.what());
            detail.assign(text.begin(), text.end());
        }
        post_status(window, kRefreshFinished, (stop.stop_requested() ? L"중단됨. " : L"완료. ") +
            std::wstring(L"갱신 ") + std::to_wstring(done) + L"개 / 실패 " + std::to_wstring(failed) + L"개. " + detail);
    });
}

INT_PTR CALLBACK settings_dialog(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, DWLP_USER));
    try {
        if (message == WM_INITDIALOG) {
            state = reinterpret_cast<SettingsWindow*>(lparam);
            SetWindowLongPtrW(window, DWLP_USER, lparam);
            state->style.initialize(window);
            for (const auto control : {IDC_BACKGROUND, IDC_MODEL}) { SendDlgItemMessageW(window, control, EM_SETLIMITTEXT, 7, 0); }
            set_colors(window, load_appearance());
            update_preview(window, *state);
            return TRUE;
        }
        if (!state) { return FALSE; }
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
            catch (const std::invalid_argument&) { SetDlgItemTextW(window, IDC_STATUS, L"색상은 #RRGGBB 형식으로 입력하세요. 예: #2B2B2B"); }
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
        if (HIWORD(wparam) == EN_CHANGE) { SetTimer(window, 1, 250, nullptr); return TRUE; }
        switch (LOWORD(wparam)) {
        case IDC_PICK_BACKGROUND: choose_color(window, IDC_BACKGROUND); break;
        case IDC_PICK_MODEL: choose_color(window, IDC_MODEL); break;
        case IDC_SAVE:
            save_appearance(read_colors(window));
            update_preview(window, *state);
            SetDlgItemTextW(window, IDC_STATUS, L"저장했습니다. 기존 파일은 ‘파일 선택 후 갱신’ 또는 ‘폴더 갱신’을 사용하세요.");
            break;
        case IDC_DEFAULTS: set_colors(window, {}); SetDlgItemTextW(window, IDC_STATUS, L"기본 색상을 불러왔습니다. ‘색상 저장’을 누르면 적용됩니다."); break;
        case IDC_REFRESH_FILE: refresh_selection(window, *state, false); break;
        case IDC_REFRESH_FOLDER: refresh_selection(window, *state, true); break;
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
