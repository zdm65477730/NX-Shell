#pragma once

namespace Lang {
    typedef enum {
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
        SettingsUSBTitle,
        SettingsUSBUnmount,
        SettingsImageViewTitle,
        SettingsDevOptsTitle,
        SettingsMultiLangTitle,
        SettingsFullCharsetTitle,
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
        CommonOverwrite,
        CommonInsert,
        CommonOn,
        CommonOff,

        // Text Editor
        TextEditorStatusLine,
        TextEditorStatusCol,
        TextEditorStatusView,
        TextEditorStatusModified,
        TextEditorStatusMode,
        TextEditorStatusSelect,
        TextEditorStatusCaps,
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

        // Max
        Max
    } StringID;
}

extern const char **strings[Lang::Max];