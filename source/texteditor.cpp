#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <vector>
#include <stack>
#include <chrono>
#include <string>
#include <cctype>
#include <format>

#include "imgui.h"
#include "windows.hpp"
#include "fs.hpp"
#include "keyboard.hpp"
#include "language.hpp"
#include "config.hpp"
#include "log.hpp"

// Forward declaration for GUI functions
namespace GUI {
    void SetTextEditorActive(bool active);
    void RequestTextEditorQuit();
    void ResetTextEditorQuit();
}

// Configurable joystick scroll step (can be adjusted as needed)
const int JOYSTICK_SCROLL_STEP = 10;

// ========== Core Editor Data Structure ==========
struct EditOperation {
    std::string prev_text;   // Text state before operation
    int prev_cursor;         // Cursor position before operation
    bool is_modified;        // Modification state before operation
};

class Editor {
public:
    // Basic editor state
    std::string text;               // Text content of the file
    bool is_modified;               // Whether the file has been modified
    bool overwrite_mode;            // Insert/Overwrite input mode
    int cursor_pos;                 // Current cursor position (byte offset)
    int scroll_line;                // First visible line in viewport
    int visible_lines;              // Number of visible lines in viewport

    // Undo/Redo functionality
    std::stack<EditOperation> undo_stack;  // Stack for undo operations
    std::stack<EditOperation> redo_stack;  // Stack for redo operations
    static const std::size_t MAX_UNDO_STEPS = 50;  // Maximum undo history steps

    // Text selection state
    int start_select_pos;           // Selection start position
    int end_select_pos;             // Selection end position
    bool is_selecting;              // Whether text selection is active

    // Clipboard/Find/Caps Lock state
    std::string clipboard;          // Clipboard content
    std::string find_text;          // Text to find in document
    int find_pos;                   // Current position for find operation
    bool find_active;               // Whether find mode is active
    bool caps_lock;                 // Caps lock state

    // Constructor: Initialize editor with file content
    Editor(const std::string& file_path) : 
        is_modified(false), overwrite_mode(false), cursor_pos(0), 
        scroll_line(1), visible_lines(20),
        start_select_pos(0), end_select_pos(0), is_selecting(false),
        find_pos(0), find_active(false), caps_lock(false) {

        // Read file content
        std::ifstream file(file_path, std::ios::binary);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            text = buffer.str();
            file.close();
    
            // Normalize line breaks (CRLF to LF)
            size_t pos = 0;
            while ((pos = text.find("\r\n", pos)) != std::string::npos) {
                text.replace(pos, 2, "\n");
                pos += 1;
            }
        }

        // Ensure trailing newline
        if (!text.empty() && text.back() != '\n') {
            text.push_back('\n');
        }

        // Initialize undo stack with initial state
        PushUndoState();
    }

    // ========== Basic Editor Operations ==========
    void PushUndoState() {
        if (undo_stack.size() >= MAX_UNDO_STEPS) {
            std::stack<EditOperation> temp;
            const std::size_t keep = MAX_UNDO_STEPS - 1;
            while (!undo_stack.empty()) {
                if (undo_stack.size() > keep) {
                    undo_stack.pop();
                } else {
                    temp.push(undo_stack.top());
                    undo_stack.pop();
                }
            }
            while (!temp.empty()) {
                undo_stack.push(temp.top());
                temp.pop();
            }
        }
        undo_stack.push({text, cursor_pos, is_modified});
        while (!redo_stack.empty()) redo_stack.pop();
    }

    bool Undo() {
        if (undo_stack.size() <= 1) return false;

        redo_stack.push({text, cursor_pos, is_modified});
        EditOperation op = undo_stack.top();
        undo_stack.pop();
        text = op.prev_text;
        cursor_pos = op.prev_cursor;
        is_modified = op.is_modified;

        SyncScroll(true); // Force immediate sync
        return true;
    }

    bool Redo() {
        if (redo_stack.empty()) return false;

        undo_stack.push({text, cursor_pos, is_modified});
        EditOperation op = redo_stack.top();
        redo_stack.pop();
        text = op.prev_text;
        cursor_pos = op.prev_cursor;
        is_modified = op.is_modified;

        SyncScroll(true); // Force immediate sync
        return true;
    }

    // ========== Cursor/Scroll Control (100% Sync Guarantee) ==========
    void GetCursorPosition(int& line, int& col) {
        line = 1;
        col = 1;
        for (int i = 0; i < cursor_pos && i < (int)text.length(); i++) {
            if (text[i] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
        }
    }

    bool SetCursorPosition(int target_line, int target_col) {
        // 1. Boundary check: Ensure valid target line/column (unified logic for all input)
        int total_lines = GetTotalLines();
        target_line = std::max(1, std::min(target_line, total_lines));

        // 2. Calculate start position of target line
        int current_line = 1;
        int pos = 0;
        while (pos < (int)text.length() && current_line < target_line) {
            if (text[pos] == '\n') current_line++;
            pos++;
        }

        // 3. Calculate target column position (not exceed line end)
        int line_start = pos;
        int line_end = line_start;
        while (line_end < (int)text.length() && text[line_end] != '\n') line_end++;
        int max_col = line_end - line_start + 1;
        target_col = std::max(1, std::min(target_col, max_col));

        // 4. Locate to target column
        pos = line_start + (target_col - 1);

        // 5. Update cursor position and force viewport sync
        cursor_pos = pos;
        SyncScroll(true);
        return true;
    }

    int GetLineStartPos(int line) {
        int current_line = 1;
        int pos = 0;
        while (pos < (int)text.length() && current_line < line) {
            if (text[pos] == '\n') current_line++;
            pos++;
        }
        return pos;
    }

    int GetLineEndPos(int line) {
        int start = GetLineStartPos(line);
        int end = start;
        while (end < (int)text.length() && text[end] != '\n') end++;
        return end;
    }

    // Ensure cursor is always visible in viewport (auto-adjust viewport)
    void SyncScroll(bool force = false) {
        int cursor_line, cursor_col;
        GetCursorPosition(cursor_line, cursor_col);
        int total_lines = GetTotalLines();

        // Auto-adjust viewport to keep cursor visible
        int view_end = scroll_line + visible_lines - 1;
        if (cursor_line < scroll_line) {
            // Cursor above viewport: move viewport up
            scroll_line = cursor_line;
        } else if (cursor_line > view_end) {
            // Cursor below viewport: move viewport down
            scroll_line = cursor_line - visible_lines + 1;
        }

        // Boundary protection for viewport
        int max_scroll = std::max(1, total_lines - visible_lines + 1);
        scroll_line = std::max(1, std::min(scroll_line, max_scroll));
        if (total_lines <= visible_lines) scroll_line = 1;

        // Force refresh ImGui scroll position
        if (force) {
            ImGui::SetScrollY((scroll_line - 1) * ImGui::GetTextLineHeight());
        }
    }

    int GetTotalLines() {
        int lines = 1;
        for (char c : text) if (c == '\n') lines++;
        return lines;
    }

    void MoveToLine(int target_line) {
        int total_lines = GetTotalLines();
        target_line = std::max(1, std::min(target_line, total_lines));

        int current_line = 1;
        cursor_pos = 0;
        for (int i = 0; i < (int)text.length() && current_line < target_line; i++) {
            if (text[i] == '\n') current_line++;
            cursor_pos = i + 1;
        }
        SyncScroll(true);
    }

    // ========== Direction Key Movement (Step = ±1) ==========
    void MoveUp() {
        int line, col;
        GetCursorPosition(line, col);
        if (line > 1) {
            SetCursorPosition(line - 1, col); // Unified position logic
        }
        SyncScroll(true);
        UpdateStatusBar();
    }

    void MoveDown() {
        int line, col;
        GetCursorPosition(line, col);
        int total_lines = GetTotalLines();
        if (line < total_lines) {
            SetCursorPosition(line + 1, col); // Unified position logic
        }
        SyncScroll(true);
        UpdateStatusBar();
    }

    void MoveLeft() { 
        if (cursor_pos > 0) { 
            cursor_pos--; 
            SyncScroll(true); 
        }
        UpdateStatusBar();
    }

    void MoveRight() { 
        if (cursor_pos < (int)text.length()) { 
            cursor_pos++; 
            SyncScroll(true); 
        }
        UpdateStatusBar();
    }

    void UpdateStatusBar() {
        int line, col;
        GetCursorPosition(line, col);
        std::string status = "Line: " + std::to_string(line) + 
                            " Col: " + std::to_string(col) + 
                            " | View: " + std::to_string(scroll_line) + "-" + std::to_string(scroll_line + visible_lines - 1) +
                            " | Modified: " + (is_modified ? "Yes" : "No") +
                            " | Mode: " + (overwrite_mode ? "Overwrite" : "Insert") +
                            " | Select: " + (is_selecting ? "ON" : "OFF") +
                            " | Caps: " + (caps_lock ? "ON" : "OFF");
        TextEditor::SetStatus(status, true);
    }

    void MoveToDocumentStart() { 
        cursor_pos = 0; 
        SyncScroll(true); 
        UpdateStatusBar();
    }

    void MoveToDocumentEnd() { 
        int total_lines = GetTotalLines();
        SetCursorPosition(total_lines, 1);
        SyncScroll(true); 
        UpdateStatusBar();
    }

    void MovePageUp() {
        int line, col; GetCursorPosition(line, col);
        int target_line = std::max(1, line - visible_lines + 1);
        SetCursorPosition(target_line, col);
        UpdateStatusBar();
    }

    void MovePageDown() {
        int line, col; GetCursorPosition(line, col);
        int total_lines = GetTotalLines();
        int target_line = std::min(total_lines, line + visible_lines - 1);
        SetCursorPosition(target_line, col);
        UpdateStatusBar();
    }

    void MoveToStartOfLine() {
        int line, col; GetCursorPosition(line, col);
        cursor_pos = GetLineStartPos(line);
        SyncScroll(true);
        UpdateStatusBar();
    }

    void MoveToEndOfLine() {
        int line, col; GetCursorPosition(line, col);
        int total_lines = GetTotalLines();
        if (line <= total_lines) {
            cursor_pos = GetLineEndPos(line);
            SyncScroll(true);
        }
        UpdateStatusBar();
    }

    // ========== Text Editing Functions ==========
    void InsertTextWithCaps(const std::string& insert_text) {
        if (is_selecting && start_select_pos != end_select_pos) {
            DeleteSelectedText();
        }

        PushUndoState();
        std::string final_text = insert_text;

        if (caps_lock) {
            for (char& c : final_text) {
                if (islower(c)) c = toupper(c);
            }
        }

        if (overwrite_mode) {
            for (char c : final_text) {
                if (cursor_pos < (int)text.length()) {
                    text[cursor_pos] = c;
                    cursor_pos++;
                } else {
                    text.push_back(c);
                    cursor_pos++;
                }
            }
        } else {
            text.insert(cursor_pos, final_text);
            cursor_pos += final_text.length();
        }

        is_modified = true;
        SyncScroll(true);
        UpdateStatusBar();
    }

    void InsertText(const std::string& insert_text) {
        InsertTextWithCaps(insert_text);
    }

    bool ReplaceCurrentLine(int line_num, const std::string& new_content) {
        int line_start = GetLineStartPos(line_num);
        int line_end = GetLineEndPos(line_num);

        std::string current_content = text.substr(line_start, line_end - line_start);
        if (current_content == new_content) {
            return false;
        }

        PushUndoState();
        text.erase(line_start, line_end - line_start);
        text.insert(line_start, new_content);
        cursor_pos = line_start + new_content.length();
        is_modified = true;

        SyncScroll(true); // Immediate sync after edit
        UpdateStatusBar();
        return true;
    }

    void DeleteBackward() {
        if (cursor_pos > 0) {
            PushUndoState();
            text.erase(cursor_pos - 1, 1);
            cursor_pos--;
            is_modified = true;
            SyncScroll(true);
            UpdateStatusBar();
        }
    }

    void DeleteForward() {
        if (cursor_pos < (int)text.length()) {
            PushUndoState();
            text.erase(cursor_pos, 1);
            is_modified = true;
            SyncScroll(true);
            UpdateStatusBar();
        }
    }

    void DeleteLine() {
        int line, col; GetCursorPosition(line, col);
        int start = GetLineStartPos(line);
        int end = GetLineEndPos(line);
        if (start < (int)text.length()) {
            PushUndoState();
            text.erase(start, end - start + (text[end] == '\n' ? 1 : 0));
            cursor_pos = start;
            is_modified = true;
            SyncScroll(true);
            UpdateStatusBar();
        }
    }

    void InsertNewLine() {
        PushUndoState();
        int line, col; GetCursorPosition(line, col);
        int start = GetLineStartPos(line);
        std::string indent;

        for (int i = start; i < cursor_pos; i++) {
            if (text[i] == ' ' || text[i] == '\t') indent += text[i];
            else break;
        }

        text.insert(cursor_pos, "\n" + indent);
        cursor_pos += 1 + indent.length();
        is_modified = true;
        SyncScroll(true);
        UpdateStatusBar();
    }

    // ========== Selection/Copy/Paste/Find Functions ==========
    void ToggleSelectMode() {
        is_selecting = !is_selecting;
        if (is_selecting) {
            start_select_pos = cursor_pos;
            end_select_pos = cursor_pos;
        } else {
            if (start_select_pos > end_select_pos) {
                std::swap(start_select_pos, end_select_pos);
            }
        }
        UpdateStatusBar();
    }

    void UpdateSelectRange() {
        if (is_selecting) {
            end_select_pos = cursor_pos;
        }
    }

    std::string GetSelectedText() {
        if (!is_selecting || start_select_pos == end_select_pos) return "";
        int s = std::min(start_select_pos, end_select_pos);
        int e = std::max(start_select_pos, end_select_pos);
        return text.substr(s, e - s);
    }

    void DeleteSelectedText() {
        if (!is_selecting || start_select_pos == end_select_pos) return;
        PushUndoState();
        int s = std::min(start_select_pos, end_select_pos);
        int e = std::max(start_select_pos, end_select_pos);
        text.erase(s, e - s);
        cursor_pos = s;
        is_selecting = false;
        is_modified = true;
        SyncScroll(true);
        UpdateStatusBar();
    }

    void CopySelectedText() {
        clipboard = GetSelectedText();
        UpdateStatusBar();
    }

    void PasteFromClipboard() {
        if (clipboard.empty()) return;
        InsertTextWithCaps(clipboard);
    }

    bool FindNext() {
        if (find_text.empty()) return false;
        size_t pos = text.find(find_text, find_pos);
        if (pos != std::string::npos) {
            cursor_pos = pos;
            find_pos = pos + find_text.length();
            SyncScroll(true);
            UpdateStatusBar();
            return true;
        } else {
            find_pos = 0;
            pos = text.find(find_text, 0);
            if (pos != std::string::npos) {
                cursor_pos = pos;
                find_pos = pos + find_text.length();
                SyncScroll(true);
                UpdateStatusBar();
                return true;
            }
            return false;
        }
    }

    bool Save(const std::string& file_path) {
        std::string save_text = text;
        size_t pos = 0;
        while ((pos = save_text.find("\n", pos)) != std::string::npos) {
            save_text.replace(pos, 1, "\r\n");
            pos += 2;
        }

        std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
        if (file.is_open()) {
            file << save_text;
            file.close();
            is_modified = false;
            PushUndoState();
            UpdateStatusBar();
            return true;
        }
        return false;
    }
};

// ========== Global Editor Instance Management ==========
namespace TextEditor {
    static Editor* current_editor = nullptr;          // Global editor instance
    static std::string file_path = "";                // Current file path
    static std::string status_message = "";           // Status bar message
    static bool custom_status = false;                // Custom status message flag
    static std::chrono::steady_clock::time_point status_timeout;  // Status message timeout

    static bool is_keyboard_popup = false;            // Virtual keyboard active flag
    static bool first_load = true;                    // First load initialization flag
    static int saved_scroll_line = 1;                 // Saved scroll position for edit mode
    static int saved_cursor_line = 1;                 // Saved cursor line for edit mode
    static bool a_key_held = false;                   // A button hold state (prevent repeat)

    // Direction key repeat tracking for accelerated movement

    // Initialize editor with specified file
    void Initialize(const std::string& path) {
        if (current_editor) delete current_editor;
        file_path = path;
        current_editor = new Editor(path);
        status_message = "Ready - Loaded: " + path;
        custom_status = false;
        status_timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);

        is_keyboard_popup = false;
        a_key_held = false;
        saved_scroll_line = 1;
        saved_cursor_line = 1;
        first_load = true;
        
        // Notify GUI that text editor is now active
        GUI::SetTextEditorActive(true);
    }

    void Shutdown() {
        if (current_editor) delete current_editor;
        current_editor = nullptr;
        file_path = "";
        status_message = "";
        custom_status = false;
        
        // Notify GUI that text editor is no longer active
        GUI::SetTextEditorActive(false);
    }

    void SetStatus(const std::string& msg, bool custom) {
        status_message = msg;
        custom_status = custom;
        if (custom) status_timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }

    // ========== Input Handling (Unified Cursor Logic for All Input) ==========
    void HandleInput(u64& key) {
        if (!current_editor) return;

        if (first_load) {
            first_load = false;
            key = 0;
            return;
        }

        // A button handling
        bool a_key_pressed = (key & HidNpadButton_A) && !a_key_held;
        a_key_held = (key & HidNpadButton_A) ? true : false;

        if (a_key_pressed && !is_keyboard_popup) {
            saved_scroll_line = current_editor->scroll_line;
            int cursor_line, cursor_col;
            current_editor->GetCursorPosition(cursor_line, cursor_col);
            saved_cursor_line = cursor_line;
    
            is_keyboard_popup = true;
    
            int line, col;
            current_editor->GetCursorPosition(line, col);
            int line_start = current_editor->GetLineStartPos(line);
            int line_end = current_editor->GetLineEndPos(line);
            std::string initial_text = current_editor->text.substr(line_start, line_end - line_start);
    
            std::string input = Keyboard::GetText("Edit line " + std::to_string(line), initial_text);
    
            is_keyboard_popup = false;
    
            current_editor->MoveToLine(saved_cursor_line);
            current_editor->scroll_line = saved_scroll_line;
            current_editor->SyncScroll(true);
    
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            contentSize.y -= 40;
            float line_height = ImGui::GetTextLineHeight();
            current_editor->visible_lines = std::max(1, (int)(contentSize.y / line_height));
    
            if (!input.empty() && input != strings[cfg.lang][Lang::KeyboardEmpty]) {
                current_editor->ReplaceCurrentLine(line, input);
                SetStatus("Updated line " + std::to_string(line), true);
            } else {
                SetStatus("Cancel edit", true);
            }
    
            key = 0;
            return;
        }

        if (!is_keyboard_popup) {
            // Standard input handling
            if (key & HidNpadButton_Minus) {
                if (current_editor->is_modified) {
                    SetStatus("Unsaved changes! Press Minus again to exit", true);
                    static bool confirm_exit = false;
                    if (confirm_exit) {
                        Shutdown();
                        data.state = WINDOW_STATE_FILEBROWSER;
                        confirm_exit = false;
                        // Request GUI to quit text editor mode
                        GUI::RequestTextEditorQuit();
                    } else {
                        confirm_exit = true;
                        return;
                    }
                } else {
                    Shutdown();
                    data.state = WINDOW_STATE_FILEBROWSER;
                    // Request GUI to quit text editor mode
                    GUI::RequestTextEditorQuit();
                }
            }

            if (key & HidNpadButton_Plus) {
                if (current_editor->is_modified) {
                    if (current_editor->Save(file_path)) {
                        SetStatus("Saved successfully: " + file_path);
                    } else {
                        SetStatus("ERROR: Failed to save file!", true);
                    }
                } else {
                    SetStatus("No changes to save");
                }
            }

            if ((key & HidNpadButton_X) && (key & HidNpadButton_L)) {
                std::string selected = current_editor->GetSelectedText();
                if (!selected.empty()) {
                    current_editor->CopySelectedText();
                    SetStatus("Copied " + std::to_string(selected.length()) + " characters");
                } else {
                    SetStatus("No text selected to copy", true);
                }
            }

            if ((key & HidNpadButton_Y) && (key & HidNpadButton_L)) {
                if (!current_editor->clipboard.empty()) {
                    current_editor->PasteFromClipboard();
                    SetStatus("Pasted from clipboard");
                } else {
                    SetStatus("Clipboard is empty", true);
                }
            }

            if (key & HidNpadButton_ZL) {
                if (current_editor->Undo()) {
                    SetStatus("Undo successful");
                } else {
                    SetStatus("Nothing to undo");
                }
            }

            if (key & HidNpadButton_ZR) {
                if (current_editor->Redo()) {
                    SetStatus("Redo successful");
                } else {
                    SetStatus("Nothing to redo");
                }
            }

            if (key & HidNpadButton_L && !(key & HidNpadButton_X) && !(key & HidNpadButton_Y)) {
                std::string input = Keyboard::GetText("Find text:", current_editor->find_text);
                if (!input.empty()) {
                    current_editor->find_text = input;
                    current_editor->find_pos = current_editor->cursor_pos;
                    current_editor->find_active = true;
                    if (current_editor->FindNext()) {
                        SetStatus("Found: \"" + input + "\"");
                    } else {
                        SetStatus("\"" + input + "\" not found", true);
                    }
                }
            }

            if (key & HidNpadButton_R) {
                current_editor->ToggleSelectMode();
                SetStatus(current_editor->is_selecting ? "Select mode ON" : "Select mode OFF");
            }

            if (key & HidNpadButton_B) {
                if (current_editor->is_selecting && current_editor->start_select_pos != current_editor->end_select_pos) {
                    current_editor->DeleteSelectedText();
                    SetStatus("Deleted selected text");
                } else {
                    current_editor->DeleteBackward();
                    SetStatus("Backspace (deleted previous character)");
                }
            }

            if ((key & HidNpadButton_B) && (key & HidNpadButton_ZR)) {
                current_editor->DeleteForward();
                SetStatus("Deleted forward character");
            }

            if ((key & HidNpadButton_Down) && (key & HidNpadButton_A)) {
                current_editor->InsertNewLine();
                SetStatus("Inserted new line");
            }

            // Direction keys (Step = ±1) - Basic implementation without acceleration
            if (key & HidNpadButton_Up) {
                current_editor->MoveUp();
            }

            if (key & HidNpadButton_Down) {
                current_editor->MoveDown();
            }

            if (key & HidNpadButton_Left) {
                current_editor->MoveLeft();
            }

            if (key & HidNpadButton_Right) {
                current_editor->MoveRight();
            }

            // Fix: Joystick Up Scroll - Boundary check first, then position, show actual cursor position
            if (key & HidNpadButton_StickLUp) {
                int cursor_line, cursor_col;
                current_editor->GetCursorPosition(cursor_line, cursor_col);
                int total_lines = current_editor->GetTotalLines();

                // Limit target line boundary first (1 ~ total_lines)
                int new_cursor_line = cursor_line - JOYSTICK_SCROLL_STEP;
                new_cursor_line = std::max(1, std::min(new_cursor_line, total_lines));

                // Unified positioning logic
                current_editor->SetCursorPosition(new_cursor_line, cursor_col);

                // Get actual cursor position for display (ensure status bar accuracy)
                int actual_line, actual_col;
                current_editor->GetCursorPosition(actual_line, actual_col);

                current_editor->SyncScroll(true);
                current_editor->UpdateStatusBar();
                SetStatus("Scroll up to line: " + std::to_string(actual_line), false);
            }

            // Fix: Joystick Down Scroll - Boundary check first, then position, show actual cursor position
            if (key & HidNpadButton_StickLDown) {
                int cursor_line, cursor_col;
                current_editor->GetCursorPosition(cursor_line, cursor_col);
                int total_lines = current_editor->GetTotalLines();

                // Limit target line boundary first (1 ~ total_lines)
                int new_cursor_line = cursor_line + JOYSTICK_SCROLL_STEP;
                new_cursor_line = std::max(1, std::min(new_cursor_line, total_lines));

                // Unified positioning logic
                current_editor->SetCursorPosition(new_cursor_line, cursor_col);

                // Get actual cursor position for display (ensure status bar accuracy)
                int actual_line, actual_col;
                current_editor->GetCursorPosition(actual_line, actual_col);

                current_editor->SyncScroll(true);
                current_editor->UpdateStatusBar();
                SetStatus("Scroll down to line: " + std::to_string(actual_line), false);
            }

            if (custom_status && std::chrono::steady_clock::now() > status_timeout) {
                current_editor->UpdateStatusBar();
                custom_status = false;
            }
        }

        key = 0;
    }

    void HandleMouseClick(const ImVec2& click_pos, const ImVec2& scroll_pos, float line_height) {
        if (!current_editor) return;

        int click_line = current_editor->scroll_line + (int)((click_pos.y - scroll_pos.y) / line_height);
        int total_lines = current_editor->GetTotalLines();
        click_line = std::clamp(click_line, 1, total_lines);

        float char_width = ImGui::CalcTextSize(" ").x;
        int click_col = (int)((click_pos.x - ImGui::GetWindowPos().x - 40) / char_width) + 1;
        click_col = std::max(click_col, 1);

        current_editor->SetCursorPosition(click_line, click_col);
        if (current_editor->is_selecting) current_editor->UpdateSelectRange();

        current_editor->UpdateStatusBar();
    }
}

// ========== UI Rendering ==========
namespace Windows {
    void TextEditor() {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_Once);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        if (ImGui::Begin("TiTleBar", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
            if (!TextEditor::file_path.empty() && TextEditor::current_editor) {
                float window_width = ImGui::GetContentRegionAvail().x;
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), TextEditor::file_path.c_str());
                ImGui::SameLine(window_width * 0.5f);
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), 
                    "| \uE0EF Save | \uE0F0 Exit | \uE0E0 Edit | \uE0E1 Delete | \uE0E6 Undo | \uE0E7 Redo |");
            }

            ImGui::Separator();
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            contentSize.y -= 40; 
            ImGui::BeginChild("TextScrollView", contentSize, true, ImGuiWindowFlags_HorizontalScrollbar);

            if (TextEditor::current_editor) {
                Editor* editor = TextEditor::current_editor;
                float line_height = ImGui::GetTextLineHeight();
                int total_lines = editor->GetTotalLines();
                editor->visible_lines = std::max(1, (int)(contentSize.y / line_height));

                // Force scroll position sync every frame (cursor always visible)
                ImGui::SetScrollY((editor->scroll_line - 1) * line_height);

                int start_line = editor->scroll_line;
                int end_line = std::min(start_line + editor->visible_lines, total_lines);
                std::istringstream iss(editor->text);
                std::string line;
                int line_num = 1;

                while (line_num < start_line && std::getline(iss, line)) line_num++;

                while (line_num <= end_line && std::getline(iss, line)) {
                    std::string line_num_str = std::format("{:6d}", line_num);

                    int cursor_line, cursor_col;
                    editor->GetCursorPosition(cursor_line, cursor_col);

                    ImGui::TextColored(
                        line_num == cursor_line ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                        line_num_str.c_str()
                    );

                    ImGui::SameLine(75.0f);

                    ImVec2 line_pos = ImGui::GetCursorScreenPos();
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();

                    if (editor->is_selecting) {
                        int s = std::min(editor->start_select_pos, editor->end_select_pos);
                        int e = std::max(editor->start_select_pos, editor->end_select_pos);
                        int line_start = editor->GetLineStartPos(line_num);
                        int line_end = editor->GetLineEndPos(line_num);

                        int select_start = std::max(s, line_start);
                        int select_end = std::min(e, line_end);
                        if (select_start < select_end) {
                            std::string line_sub = line.substr(0, select_start - line_start);
                            float select_x_start = line_pos.x + ImGui::CalcTextSize(line_sub.c_str()).x;
                            line_sub = line.substr(0, select_end - line_start);
                            float select_x_end = line_pos.x + ImGui::CalcTextSize(line_sub.c_str()).x;

                            draw_list->AddRectFilled(
                                ImVec2(select_x_start, line_pos.y),
                                ImVec2(select_x_end, line_pos.y + line_height),
                                ImColor(0x33, 0x66, 0xFF, 0x80)
                            );
                        }
                    }

                    ImGui::Text("%s", line.c_str());

                    if (line_num == cursor_line) {
                        int cursor_col_draw = cursor_col - 1;
                        float cursor_x = line_pos.x + ImGui::CalcTextSize(line.substr(0, cursor_col_draw).c_str()).x;
                        float cursor_y = line_pos.y;

                        static float cursor_flash = 0.0f;
                        cursor_flash += ImGui::GetIO().DeltaTime * 6.0f;
                        float alpha = sin(cursor_flash) > 0 ? 1.0f : 0.3f;

                        draw_list->AddRectFilled(
                            ImVec2(cursor_x, cursor_y),
                            ImVec2(cursor_x + 2, cursor_y + line_height),
                            ImColor(0.2f, 0.4f, 0.9f, alpha)
                        );
                    }

                    line_num++;
                }

                if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) {
                    ImVec2 click_pos = ImGui::GetMousePos();
                    ImVec2 scroll_pos;
                    scroll_pos.x = ImGui::GetScrollX();
                    scroll_pos.y = ImGui::GetScrollY();
                    click_pos.y -= ImGui::GetWindowPos().y + ImGui::GetFrameHeightWithSpacing();
                    TextEditor::HandleMouseClick(click_pos, scroll_pos, line_height);
                }
            }

            ImGui::EndChild();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", TextEditor::status_message.c_str());
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }
}