#pragma once
#include <string>

// User-facing text of the settings app. Worker/Shell diagnostics stay in English for both languages.
enum class Language { Korean, English };

enum class Text {
    Caption, Subtitle, Version, Language,
    StyleTitle, BackgroundLabel, TransparencyLabel, TransparencyHelp, ModelLabel,
    PickBackground, PickModel, CreatorNote, Defaults, Save,
    PreviewTitle, RefreshTitle, RefreshHelp, RefreshHint, ChooseFile, ChooseFolder, FailureDetails, StopRefresh, Close,
    Saved, DefaultsLoaded, InvalidInput, Stopping, StoppingBeforeClose, Refreshing,
    ProgressFormat, FinishedFormat, StoppedFormat, FirstFailureFormat, OnlyFirstFailures, FileFilter,
    DetailsCaption, DetailsIntro, CopyAll,
    Count
};

const wchar_t* text(Language language, Text id);
// Replaces {0}, {1}, {2} in order.
std::wstring format_text(Language language, Text id, const std::wstring& first,
    const std::wstring& second = {}, const std::wstring& third = {});
// Saved choice, else the Windows display language (Korean for Korean Windows, English otherwise).
Language load_language() noexcept;
void save_language(Language language);
