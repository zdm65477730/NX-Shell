#pragma once

#include <unordered_map>

enum class Locale {
    Japanese = 0,
    English,
    French,
    German,
    Italian,
    Spanish,
    SimplifiedChinese,
    Korean,
    Dutch,
    Portuguese,
    Russian,
    TraditionalChinese,

    MaxCount
};

enum class Lang {
    // Prompt/Message buttons
    ButtonOK = 0,
    ButtonCancel,

    // File Browser
    FileBrowser,
    DirectoryList,
    FileName,
    FileSize,

    // Options dialog
    OptionsTitle,
    OptionsSelectAll,
    OptionsClearAll,
    OptionsProperties,
    OptionsRename,
    OptionsNewFolder,
    OptionsNewFile,
    OptionsCopy,
    OptionsMove,
    OptionsPaste,
    OptionsDelete,
    OptionsSetArchiveBit,
    OptionsRenamePrompt,
    OptionsFolderPrompt,
    OptionsFilePrompt,
    OptionsCopying,

    // Properties dialog
    PropertiesName,
    PropertiesSize,
    PropertiesCreated,
    PropertiesModified,
    PropertiesAccessed,
    PropertiesWidth,
    PropertiesHeight,

    // Delete dialog
    DeleteMessage,
    DeleteMultiplePrompt,
    DeletePrompt,

    // Archive dialog
    ArchiveTitle,
    ArchiveMessage,
    ArchivePrompt,
    ArchiveExtracting,

    // SettingsWindow
    SettingsTitle,
    SettingsSortTitle,
    SettingsLanguageTitle,
    LanguageJapanese,
    LanguageEnglish,
    LanguageFrench,
    LanguageGerman,
    LanguageItalian,
    LanguageSpanish,
    LanguageSimplifiedChinese,
    LanguageKorean,
    LanguageDutch,
    LanguagePortuguese,
    LanguageRussian,
    LanguageTraditionalChinese,
    SettingsUSBTitle,
    SettingsUSBUnmount,
    SettingsImageViewTitle,
    SettingsDevOptsTitle,
    SettingsMultiLangTitle,
    SettingsAboutTitle,
    SettingsCheckForUpdates,
    SettingsImageViewFilenameToggle,
    SettingsDevOptsLogsToggle,
    SettingsMultiLangLogsToggle,
    SettingsFullCharsetLogsToggle,
    SettingsAboutVersion,
    SettingsAboutAuthor,
    SettingsAboutBanner,

    // Updates Dialog
    UpdateTitle,
    UpdateNetworkError,
    UpdateAvailable,
    UpdatePrompt,
    UpdateSuccess,
    UpdateRestart,
    UpdateNotAvailable,

    // USB Dialog
    USBUnmountPrompt,
    USBUnmountSuccess,

    // Keyboard
    KeyboardEmpty,

    // Common words
    CommonYes,
    CommonNo,
    CommonOn,
    CommonOff,

    // Text Editor
    TextEditorStatusLine,
    TextEditorStatusCol,
    TextEditorStatusView,
    TextEditorStatusModified,
    TextEditorStatusSelect,
    TextEditorStatusFileSize,

    TextEditorStatusFileOpened,
    TextEditorEditLine,
    TextEditorUpdatedLine,
    TextEditorCancelEdit,
    TextEditorStatusChangesUnsaved,
    TextEditorStatusSaved,
    TextEditorStatusSaveFailed,
    TextEditorStatusNoChangesToSave,
    TextEditorStatusCopiedCharacters,
    TextEditorStatusNoTextToCopy,
    TextEditorStatusPasted,
    TextEditorStatusClipboardEmpty,
    TextEditorStatusUndoSuccessful,
    TextEditorStatusNothingToUndo,
    TextEditorStatusRedoSuccessful,
    TextEditorStatusNothingToRedo,
    TextEditorStatusFindText,
    TextEditorStatusTextFound,
    TextEditorStatusTextNotFound,
    TextEditorStatusSelectModeOn,
    TextEditorStatusSelectModeOff,
    TextEditorStatusDeletedSelectedText,
    TextEditorStatusDeletedPreCharacter,
    TextEditorStatusDeleteForwardCharacter,
    TextEditorStatusInsertNewLine,
    TextEditorControls,
    TextEditorExitPrompt,

    MaxCount
};

extern std::unordered_map<Locale, std::unordered_map<Lang, const char *>> strings;