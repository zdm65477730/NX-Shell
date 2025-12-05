#include <algorithm>
#include <cstring>
#include <string>

#include "config.hpp"
#include "fs.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "language.hpp"
#include "tabs.hpp"
#include "textures.hpp"
#include "utils.hpp"

// Global sort state (0=alpha ascending, 1=alpha descending, 2=size ascending, 3=size descending)
int sort = 0;
// List of accessible Switch storage devices (sdmc=SD card, safe/system/user=internal storage)
std::vector<std::string> devices_list = { "sdmc:", "safe:", "user:", "system:" };
// Mutex for thread-safe access to devices_list (prevents race conditions)
std::recursive_mutex devices_list_mutex;

// Global variable declarations (shared with other NS-Shell modules)
extern std::string cwd;                  // Current working directory (e.g., "/switch/")
extern std::string device;               // Currently selected device (e.g., "sdmc:")
extern FsFileSystem *fs;                 // Active file system handle for selected device

// Texture resource declarations (shared with NS-Shell's texture system)
extern Tex check_icon;                   // Checkmark icon for selected files
extern Tex uncheck_icon;                 // Unchecked icon for unselected files
extern Tex folder_icon;                  // Folder icon for directories
extern std::vector<Tex> file_icons;      // File type icons (text, image, etc.)

namespace FileBrowser {
    // Sort without using ImGuiTableSortSpecs
    bool Sort(const FsDirectoryEntry &entryA, const FsDirectoryEntry &entryB) {
        // Make sure ".." stays at the top regardless of sort direction
        if (strcasecmp(entryA.name, "..") == 0)
            return true;
        
        if (strcasecmp(entryB.name, "..") == 0)
            return false;
        
        // Directories prioritize over files
        if ((entryA.type == FsDirEntryType_Dir) && !(entryB.type == FsDirEntryType_Dir))
            return true;
        else if (!(entryA.type == FsDirEntryType_Dir) && (entryB.type == FsDirEntryType_Dir))
            return false;

        switch(sort) {
            case FS_SORT_ALPHA_ASC:
                return (strcasecmp(entryA.name, entryB.name) < 0);
                break;

            case FS_SORT_ALPHA_DESC:
                return (strcasecmp(entryB.name, entryA.name) < 0);
                break;
            
            case FS_SORT_SIZE_ASC:
                return entryA.file_size < entryB.file_size;
                break;
            
            case FS_SORT_SIZE_DESC:
                return entryA.file_size > entryB.file_size;
                break;

            default:
                return false;
        }
    }

    // Sort using ImGuiTableSortSpecs
    bool TableSort(const FsDirectoryEntry &entryA, const FsDirectoryEntry &entryB) {
        bool descending = false;
        ImGuiTableSortSpecs *table_sort_specs = ImGui::TableGetSortSpecs();
        
        for (int i = 0; i < table_sort_specs->SpecsCount; ++i) {
            const ImGuiTableColumnSortSpecs *column_sort_spec = std::addressof(table_sort_specs->Specs[i]);
            descending = (column_sort_spec->SortDirection == ImGuiSortDirection_Descending);

            // Make sure ".." stays at the top regardless of sort direction
            if (strcasecmp(entryA.name, "..") == 0)
                return true;
            
            if (strcasecmp(entryB.name, "..") == 0)
                return false;
            
            // Directories prioritize over files
            if ((entryA.type == FsDirEntryType_Dir) && !(entryB.type == FsDirEntryType_Dir))
                return true;
            else if (!(entryA.type == FsDirEntryType_Dir) && (entryB.type == FsDirEntryType_Dir))
                return false;
            else {
                switch (column_sort_spec->ColumnIndex) {
                    case 1: // Filename column (primary sort column)
                        sort = descending ? FS_SORT_ALPHA_DESC : FS_SORT_ALPHA_ASC;
                        return descending ? (strcasecmp(entryB.name, entryA.name) < 0) : (strcasecmp(entryA.name, entryB.name) < 0);
                        break;
                        
                    case 2: // File size column (secondary sort column)
                        sort = descending ? FS_SORT_SIZE_DESC : FS_SORT_SIZE_ASC;
                        return descending ? (entryA.file_size > entryB.file_size) : (entryA.file_size < entryB.file_size);
                        break;
                        
                    default:
                        break;
                }
            }
        }
        
        return false;
    }
}

namespace Tabs {
    static const ImVec2 tex_size = ImVec2(21, 21); // Standard size for file/folder/check icons

    void FileBrowser(WindowData &data) {
        if (ImGui::BeginTabItem(strings[cfg.lang][Lang::FileBrowser])) {
            ImGui::Dummy(ImVec2(0.0f, 1.0f)); // Vertical spacing

            // Device selection combo box (sdmc:/safe:/user:/system:)
            ImGui::PushID("device_list"); // Unique ID to avoid ImGui widget conflicts
            ImGui::PushItemWidth(160.f); // Fixed width for combo box
            if (ImGui::BeginCombo("", device.c_str())) {
                std::scoped_lock lock(devices_list_mutex); // Thread-safe access to devices list

                for (std::size_t i = 0; i < devices_list.size(); i++) {
                    const bool is_selected = (device == devices_list[i]);
                    
                    if (ImGui::Selectable(devices_list[i].c_str(), is_selected)) {
                        // Update active device and file system handle
                        device = devices_list[i];
                        fs = std::addressof(devices[i]);
                        
                        // Reset working directory and entry list for new device
                        cwd = "/";
                        data.entries.clear();
                        FS::GetDirList(device, cwd, data.entries);
                        
                        // Reset checkbox state for multi-selection
                        data.checkbox_data.checked.resize(data.entries.size());
                        data.checkbox_data.checked.assign(data.checkbox_data.checked.size(), false);
                        
                        // Update storage usage statistics
                        FS::GetUsedStorageSpace(data.used_storage);
                        FS::GetTotalStorageSpace(data.total_storage);
                        sort = -1; // Trigger re-sort for new directory list
                    }
                        
                    if (is_selected)
                        ImGui::SetItemDefaultFocus(); // Set focus for controller navigation
                }

                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            ImGui::PopID();
            
            ImGui::SameLine(); // Align current path next to device combo

            // Display full current path (device + working directory)
            ImGui::Text("%s%s", device.c_str(), cwd.c_str());
            
            // Storage usage progress bar
            ImGui::Dummy(ImVec2(0.0f, 1.0f)); // Vertical spacing
            char storage_info[64]; // Format storage as "XX MB / XX MB"
            snprintf(storage_info, sizeof(storage_info), "%ld MB / %ld MB", 
                     data.used_storage / 1024 / 1024, data.total_storage / 1024 / 1024);
            ImGui::ProgressBar(static_cast<float>(data.used_storage) / static_cast<float>(data.total_storage), ImVec2(1265.0f, 6.0f), storage_info);
            ImGui::Dummy(ImVec2(0.0f, 1.0f)); // Vertical spacing

            // Directory list table configuration
            ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInner |
                ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
            
            // Render directory table (height fixed to 580px for Switch screen)
            if (ImGui::BeginTable(strings[cfg.lang][Lang::DirectoryList], 2, tableFlags, ImVec2(0.0f, 580.0f))) {
                // Freeze header row (always visible when scrolling)
                ImGui::TableSetupScrollFreeze(0, 1);

                // Configure table columns:
                // Column 0: Checkbox/icon column (fixed width, no sorting)
                // Column 1: Filename column (resizable, default sort)
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_WidthFixed, 30.0f);
                ImGui::TableSetupColumn(strings[cfg.lang][Lang::FileName], ImGuiTableColumnFlags_DefaultSort);
                
                ImGui::TableHeadersRow(); // Render column headers

                // Handle table sorting (re-sort entries when sort specs change)
                if (ImGuiTableSortSpecs *sorts_specs = ImGui::TableGetSortSpecs()) {
                    if (sort == -1)
                        sorts_specs->SpecsDirty = true; // Force re-sort on device change
                    
                    if (sorts_specs->SpecsDirty) {
                        std::sort(data.entries.begin(), data.entries.end(), FileBrowser::TableSort);
                        sorts_specs->SpecsDirty = false; // Mark sort as complete
                    }
                }

                // Render each directory entry row
                for (u64 i = 0; i < data.entries.size(); i++) {
                    ImGui::TableNextRow();

                    // Column 0: Checkbox icon (for multi-selection)
                    ImGui::TableNextColumn();
                    ImGui::PushID(i); // Unique ID for each row's widgets
                    
                    // Show check/uncheck icon based on selection state
                    if ((data.checkbox_data.checked[i]) && (data.checkbox_data.cwd.compare(cwd) == 0) && (data.checkbox_data.device.compare(device) == 0))
                        ImGui::Image(reinterpret_cast<ImTextureID>(check_icon.id), tex_size);
                    else
                        ImGui::Image(reinterpret_cast<ImTextureID>(uncheck_icon.id), tex_size);
                    
                    ImGui::PopID();

                    // Column 1: File/folder icon + name (selectable item)
                    ImGui::TableNextColumn();
                    FileType file_type = FS::GetFileType(data.entries[i].name); // Detect file type (text/image/etc.)
                    
                    // Render folder/file icon
                    if (data.entries[i].type == FsDirEntryType_Dir)
                        ImGui::Image(reinterpret_cast<ImTextureID>(folder_icon.id), tex_size);
                    else
                        ImGui::Image(reinterpret_cast<ImTextureID>(file_icons[file_type].id), tex_size);
                    
                    ImGui::SameLine(); // Align filename next to icon

                    // Render selectable filename (core interaction element)
                    if (ImGui::Selectable(data.entries[i].name, (data.selected == i), 
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                        data.selected = i; // Update selected index
                        
                        // Handle directory navigation
                        if (data.entries[i].type == FsDirEntryType_Dir) {
                            if (std::strncmp(data.entries[i].name, "..", 2) == 0) {
                                // Navigate to parent directory
                                if (FS::ChangeDirPrev(data.entries)) {
                                    // Preserve multi-selection state
                                    if ((data.checkbox_data.count > 1) && (data.checkbox_data.checked_copy.empty()))
                                        data.checkbox_data.checked_copy = data.checkbox_data.checked;
                                        
                                    // Reset checkbox state for new directory
                                    data.checkbox_data.checked.resize(data.entries.size());
                                    data.checkbox_data.checked.assign(data.checkbox_data.checked.size(), false);
                                }
                            }
                            else {
                                // Navigate to child directory
                                if (FS::ChangeDirNext(data.entries[i].name, data.entries)) {
                                    // Preserve multi-selection state
                                    if ((data.checkbox_data.count > 1) && (data.checkbox_data.checked_copy.empty()))
                                        data.checkbox_data.checked_copy = data.checkbox_data.checked;
                                    
                                    // Reset checkbox state for new directory
                                    data.checkbox_data.checked.resize(data.entries.size());
                                    data.checkbox_data.checked.assign(data.checkbox_data.checked.size(), false);
                                }
                            }

                            // Reset navigation ID (scroll table to top for new directory)
                            ImGuiContext& g = *GImGui;
                            ImGui::SetNavID(ImGui::GetID(data.entries[0].name, 0), g.NavLayer, 0, ImRect());

                            // Force re-sort for new directory list
                            ImGuiTableSortSpecs *sorts_specs = ImGui::TableGetSortSpecs();
                            sorts_specs->SpecsDirty = true;
                        }
                        // Handle file opening (image/text editors)
                        else {
                            std::string full_path = FS::BuildPath(data.entries[i]);

                            // Open file with appropriate editor
                            switch (file_type) {
                                case FileTypeImage:
                                    // Load image and switch to image viewer state
                                    if (Textures::LoadImageFile(full_path, data.textures)) {
                                        data.state = WINDOW_STATE_IMAGEVIEWER;
                                    }
                                    break;
                                case FileTypeText:
                                    // Initialize text editor and switch to editor state
                                    TextEditor::Initialize(full_path);
                                    data.state = WINDOW_STATE_TEXTEDITOR;
                                    break;
                                default:
                                    // Unsupported file type (no action)
                                    break;
                            }
                        }
                    }

                    // Update selected index on hover (critical for controller navigation)
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                        data.selected = i;
                    }
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }
    }
}