#include "settings_text.h"
#include "thumbnail/appearance.h"
#include <windows.h>
#include <array>
#include <stdexcept>

namespace {
struct Entry { const wchar_t* korean; const wchar_t* english; };

constexpr std::array<Entry, static_cast<std::size_t>(Text::Count)> kText{{
    {L"3D Thumbnail 설정", L"3D Thumbnail Settings"},
    {L"탐색기 썸네일의 배경과 모델 색을 정합니다.", L"Choose the background and model colors for Explorer thumbnails."},
    {L"버전 1.0.3", L"Version 1.0.3"},
    {L"언어", L"Language"},
    {L"썸네일 모양", L"Thumbnail style"},
    {L"배경 색", L"Background"},
    {L"배경 투명도", L"Transparency"},
    {L"0%는 불투명, 100%는 배경 없이 모델만 보입니다.", L"0% is opaque. 100% shows only the model."},
    {L"모델 색", L"Model"},
    {L"배경 색 선택", L"Choose background color"},
    {L"모델 색 선택", L"Choose model color"},
    {L"3MF에 제작자가 넣은 썸네일이 있으면 그 이미지를 그대로 보여 줍니다. 없을 때만 이 설정으로 새로 만듭니다.",
     L"If a 3MF file includes its creator's thumbnail, that image is shown as is. Otherwise a thumbnail is made with these settings."},
    {L"기본값", L"Defaults"},
    {L"저장", L"Save"},
    {L"미리보기", L"Preview"},
    {L"기존 썸네일 갱신", L"Refresh existing thumbnails"},
    {L"이미 표시된 썸네일은 저장 후 여기서 다시 만들어야 바뀝니다.", L"Thumbnails already shown change after you refresh them here."},
    {L"하위 폴더는 포함하지 않으며, 폴더당 최대 1,000개까지 갱신합니다.", L"Subfolders are skipped. Up to 1,000 files per folder."},
    {L"파일 선택…", L"Choose file…"},
    {L"폴더 선택…", L"Choose folder…"},
    {L"실패 상세…", L"Failure details…"},
    {L"갱신 중단", L"Stop"},
    {L"닫기", L"Close"},
    {L"저장했습니다. 이미 표시된 썸네일은 파일이나 폴더를 선택해 갱신하세요.", L"Saved. To update thumbnails already shown, choose a file or folder to refresh."},
    {L"기본값을 불러왔습니다. 저장을 눌러야 적용됩니다.", L"Defaults loaded. Select Save to apply them."},
    {L"색상은 #RRGGBB, 배경 투명도는 0~100으로 입력하세요.", L"Enter colors as #RRGGBB and transparency from 0 to 100."},
    {L"현재 파일의 갱신이 끝나면 중단합니다.", L"Stopping after the current file."},
    {L"현재 파일의 갱신이 끝나면 중단합니다. 완료 후 닫아 주세요.", L"Stopping after the current file. Close the window when it finishes."},
    {L"색상을 저장했습니다. 썸네일 갱신 중…", L"Colors saved. Refreshing thumbnails…"},
    {L"갱신 {0} / 실패 {1} — {2}", L"Refreshed {0} / failed {1} — {2}"},
    {L"완료. 갱신 {0}개 / 실패 {1}개.", L"Done. Refreshed {0}, failed {1}."},
    {L"중단됨. 갱신 {0}개 / 실패 {1}개.", L"Stopped. Refreshed {0}, failed {1}."},
    {L" 첫 실패 — {0} (실패 상세 확인)", L" First failure — {0} (see Failure details)"},
    {L"처음 10개의 실패만 표시합니다.", L"Only the first 10 failures are shown."},
    {L"3D 모델", L"3D models"},
    {L"썸네일 갱신 실패 상세", L"Thumbnail refresh failures"},
    {L"실패 파일과 오류 내용입니다. 아래 내용을 복사해 전달할 수 있습니다.", L"Files that failed and their errors. You can copy this text to share it."},
    {L"모두 복사", L"Copy all"},
}};

constexpr wchar_t kLanguageValue[] = L"Language";
}

const wchar_t* text(Language language, Text id) {
    const auto& entry = kText.at(static_cast<std::size_t>(id));
    return language == Language::Korean ? entry.korean : entry.english;
}

std::wstring format_text(Language language, Text id, const std::wstring& first, const std::wstring& second, const std::wstring& third) {
    std::wstring result = text(language, id);
    const std::wstring values[] = {first, second, third};
    for (int index = 0; index < 3; ++index) {
        const std::wstring marker = L"{" + std::to_wstring(index) + L"}";
        if (const auto position = result.find(marker); position != std::wstring::npos) { result.replace(position, marker.size(), values[index]); }
    }
    return result;
}

Language load_language() noexcept {
    wchar_t value[8]{};
    DWORD bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, thumbnail::kAppearanceKey, kLanguageValue, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS) {
        if (std::wstring(value) == L"ko") { return Language::Korean; }
        if (std::wstring(value) == L"en") { return Language::English; }
    }
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN ? Language::Korean : Language::English;
}

void save_language(Language language) {
    const wchar_t* value = language == Language::Korean ? L"ko" : L"en";
    const auto status = RegSetKeyValueW(HKEY_CURRENT_USER, thumbnail::kAppearanceKey, kLanguageValue, REG_SZ,
        value, static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    if (status != ERROR_SUCCESS) { throw std::runtime_error("Cannot save the language setting."); }
}
