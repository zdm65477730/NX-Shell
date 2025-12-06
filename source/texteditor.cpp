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
#include <unordered_map>
#include <functional>

#include "imgui.h"
#include "imgui_impl_switch.hpp"
#include "windows.hpp"
#include "fs.hpp"
#include "keyboard.hpp"
#include "language.hpp"
#include "config.hpp"
#include "log.hpp"

// Forward declaration for GUI state management
namespace GUI {
    void SetTextEditorActive(bool active);
}

// ========== Key State Tracker (Fixed Repeat & Hold Logic) ==========
class KeyInputHandler {
public:
    // Configuration for key hold/repeat behavior (Reduced initial repeat delay)
    struct KeyConfig {
        unsigned int hold_threshold = 10;          // Frames required to trigger long press
        unsigned int repeat_threshold = 40;        // Frames before key repeat starts
        unsigned int repeat_step_interval = 20;    // Interval between repeat triggers (frames)
        unsigned int repeat_max_extra = 2;         // Maximum extra repeat steps per frame
        unsigned int single_click_frames = 2;      // Frames for single click trigger (2 frames)
    };

    // Direction enum for input handling
    enum class Direction { Up, Down, Left, Right, None };

    // Constructor - Initialize with default config and ImGui backend
    KeyInputHandler() : m_config(KeyConfig()), m_last_key_state(0), m_current_key_state(0) {        
        // Initialize direction repeat tracking arrays
        for (int i = 0; i < (int)Direction::None; ++i) {
            m_dir_repeat_counts[i] = 0;
            m_dir_single_clicked[i] = false;
        }
        m_last_pressed_dir = Direction::None;
    }

    // Update key state from official ImGui Switch backend Pad state
    void UpdateKeyState() {
        m_last_key_state = m_current_key_state;
        // Get raw pad button state from backend (ImGui Switch implementation)
        m_current_key_state = padGetButtons(ImGui_ImplSwitch_GetBackendPadState());
        UpdateKeyHoldCounters();
    }

    // Check if a key was just released (pressed in last frame, not pressed now)
    bool IsKeyReleased(HidNpadButton key) const {
        u64 key_val = static_cast<u64>(key);
        return (m_last_key_state & key_val) != 0 && (m_current_key_state & key_val) == 0;
    }

    // Check if a key is being held long enough to trigger long press actions
    bool IsKeyHeld(HidNpadButton key) const {
        auto it = m_key_hold_counts.find(key);
        return (it != m_key_hold_counts.end()) && (it->second >= m_config.hold_threshold);
    }

    // Check if a key was just pressed (not pressed in last frame, pressed now)
    bool IsKeyDown(HidNpadButton key) const {
        u64 key_val = static_cast<u64>(key);
        return (m_current_key_state & key_val) != 0 && (m_last_key_state & key_val) == 0;
    }

    // Check if a key is CURRENTLY being pressed (level trigger - for long press detection)
    bool IsKeyCurrentlyHeld(HidNpadButton key) const {
        return (m_current_key_state & static_cast<u64>(key)) != 0;
    }

    // Check combo: long hold + trigger key press (hold key + single press trigger key)
    bool IsHeldKeyCombo(HidNpadButton hold_key, HidNpadButton trigger_key) const {
        return IsKeyHeld(hold_key) && IsKeyDown(trigger_key);
    }

    // Check combo: both keys currently held (level trigger combo)
    bool IsDualHeldCombo(HidNpadButton hold_key, HidNpadButton trigger_key) const {
        return IsKeyCurrentlyHeld(hold_key) && IsKeyCurrentlyHeld(trigger_key);
    }

    // Handle direction key repeat behavior (uses official ImGui key mapping)
    void HandleDirectionRepeat(Direction dir, ImGuiKey imgui_key, const std::function<void()>& on_trigger) {
        if (!on_trigger || dir == Direction::None) return;

        ImGuiIO& io = ImGui::GetIO();
        unsigned int& count = m_dir_repeat_counts[(int)dir];
        bool& is_single_clicked = m_dir_single_clicked[(int)dir];

        // Use ImGui's key state (continuous press state - critical for long press)
        bool is_key_held = io.KeysDown[imgui_key];
        if (!is_key_held) {
            count = 0;
            is_single_clicked = false;
            if (m_last_pressed_dir == dir) m_last_pressed_dir = Direction::None;
            return;
        }

        // Track active direction to prevent cross-direction interference
        if (m_last_pressed_dir != dir) {
            if (m_last_pressed_dir != Direction::None) {
                m_dir_repeat_counts[(int)m_last_pressed_dir] = 0;
                m_dir_single_clicked[(int)m_last_pressed_dir] = false;
            }
            m_last_pressed_dir = dir;
        }

        // Increment repeat counter and trigger actions
        count++;
        if (count < m_config.single_click_frames) return;

        unsigned int steps = 0;
        if (count <= m_config.repeat_threshold) {
            if (!is_single_clicked) {
                steps = 1;
                is_single_clicked = true;
            }
        } else {
            unsigned int extra = (count - m_config.repeat_threshold) / m_config.repeat_step_interval;
            extra = std::min(extra, m_config.repeat_max_extra);
            steps = 1 + extra;
        }

        // Limit maximum steps per frame to prevent input spamming
        const unsigned int max_steps_per_frame = 10;
        steps = std::min(steps, max_steps_per_frame);
        for (size_t i = 0; i < steps; ++i) {
            on_trigger(); // Execute selection/navigation action
        }
    }

    // Clear specific key state from the raw key state value
    void ClearKeyState(u64& key_state, HidNpadButton key) {
        key_state &= ~static_cast<u64>(key);
    }

    // Reset all key tracking state to default values
    void ResetAllStates() {
        m_key_hold_counts.clear();
        m_last_key_state = 0;
        m_current_key_state = 0;
        m_last_pressed_dir = Direction::None;

        for (int i = 0; i < (int)Direction::None; ++i) {
            m_dir_repeat_counts[i] = 0;
            m_dir_single_clicked[i] = false;
        }
    }

private:
    // Update hold duration counters for all tracked keys
    void UpdateKeyHoldCounters() {
        // List of keys to track for hold behavior
        const std::vector<HidNpadButton> keys = {
            HidNpadButton_L, HidNpadButton_R, HidNpadButton_A, HidNpadButton_B,
            HidNpadButton_X, HidNpadButton_Y, HidNpadButton_Plus, HidNpadButton_Minus,
            HidNpadButton_Left, HidNpadButton_Right, HidNpadButton_Up, HidNpadButton_Down
        };

        for (auto key : keys) {
            u64 key_val = static_cast<u64>(key);
            bool is_held = (m_current_key_state & key_val) != 0;
            m_key_hold_counts[key] = is_held ? (m_key_hold_counts[key] + 1) : 0;
        }
    }

    // Member variables
    KeyConfig m_config;                          // Key behavior configuration
    u64 m_last_key_state;                        // Previous frame's key state (bitmask)
    u64 m_current_key_state;                     // Current frame's key state (bitmask)
    std::unordered_map<HidNpadButton, unsigned int> m_key_hold_counts; // Key hold duration counters
    unsigned int m_dir_repeat_counts[(int)Direction::None]; // Repeat counters for direction keys
    bool m_dir_single_clicked[(int)Direction::None];        // Single click flags for direction keys
    Direction m_last_pressed_dir;                // Last active direction key (prevents cross-interference)
};

static KeyInputHandler g_key_handler;

// ========== Core Text Editor Class (Selection Retention & Zero-Length Deselection) ==========
class Editor {
public:
    // Undo/Redo operation structure
    // Extended with is_saved flag to track if state was saved to file
    struct EditOperation {
        std::string prev_text;      // Text content of the state
        unsigned int prev_cursor;   // Cursor position in the state
        bool is_modified;           // Whether the state has unsaved changes
        bool is_saved;              // Whether the state was saved to file

        // Constructor with saved state flag
        EditOperation(const std::string& txt, unsigned int cur, bool mod, bool saved) 
            : prev_text(txt), prev_cursor(cur), is_modified(mod), is_saved(saved) {}
    };

    // Core editor state
    std::string text;                       // Current editor content
    std::string last_saved_text;            // Last content saved to file (for change detection)
    bool is_first_l_press_in_select;        // Flag for first L press in selection session (prevents conflict)
    bool has_extended_selection;            // Flag for extended selection (L + direction keys)
    bool is_modified;                       // Whether content has unsaved changes
    bool overwrite_mode;                    // Insert/Overwrite input mode flag
    unsigned int cursor_pos;                // Current cursor position (character index)
    unsigned int scroll_line;               // Top visible line number in viewport
    unsigned int visible_lines;             // Number of visible lines in viewport
    float line_height;                      // Height of single text line (ImGui units)

    // Undo/Redo stack configuration
    std::stack<EditOperation> undo_stack;   // Stack for undo operations (latest state on top)
    std::stack<EditOperation> redo_stack;   // Stack for redo operations (latest undone state on top)
    static const std::size_t MAX_UNDO_STEPS = 50; // Maximum number of undo steps to retain

    // Selection state (retained until zero-length or explicit action)
    unsigned int select_start;              // Selection start position (character index)
    unsigned int select_end;                // Selection end position (character index)
    bool select_active;                     // Whether selection is currently active

    // Editor utilities
    std::string clipboard;                  // Clipboard content (copied text)
    std::string find_text;                  // Current search text for find operations
    unsigned int find_pos;                  // Last position where find text was found
    bool find_active;                       // Whether find mode is active
    bool caps_lock;                         // Caps lock state (affects input text case)

    // Constructor - Initialize editor with file content
    Editor(const std::string& file_path) : 
        is_first_l_press_in_select(false), has_extended_selection(false),
        is_modified(false), overwrite_mode(false), cursor_pos(0), 
        scroll_line(1), visible_lines(20), line_height(0.0f),
        select_start(0), select_end(0), select_active(false),
        find_pos(0), find_active(false), caps_lock(false) {

        // Load file content from disk
        std::ifstream file(file_path, std::ios::binary);
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            text = buffer.str();
            file.close();

            // Convert CRLF line endings to LF (consistent internal format)
            size_t pos = 0;
            while ((pos = text.find("\r\n", pos)) != std::string::npos) {
                text.replace(pos, 2, "\n");
                pos += 1;
            }
        }

        // Ensure trailing newline (consistent file format)
        if (!text.empty() && text.back() != '\n') text.push_back('\n');
        
        // Initialize saved state tracking
        last_saved_text = text;
        
        // Initialize undo stack with initial state (saved to file)
        undo_stack.push(EditOperation(text, cursor_pos, false, true));
        // Clear redo stack (no redo operations on initialization)
        while (!redo_stack.empty()) redo_stack.pop();
    }

    // Activate text selection mode (start at current cursor position)
    void ActivateSelectMode() {
        if (!select_active) {
            select_start = cursor_pos;
            select_end = cursor_pos;
            select_active = true;
            UpdateStatusBar();
        }
    }

    // Deactivate text selection mode (explicit call only)
    void DeactivateSelectMode() {
        if (select_active) {
            select_active = false;
            UpdateStatusBar();
        }
    }

    // Reset selection session flags (called when entering selection mode)
    void ResetSelectSessionFlags() {
        has_extended_selection = false;
        is_first_l_press_in_select = true;
    }

    // EXTENDED: Support vertical selection + zero-length deselection
    void ExtendSelection(KeyInputHandler::Direction dir) {
        if (!select_active) {
            select_start = cursor_pos;
            select_end = cursor_pos;
            select_active = true;
            UpdateStatusBar();
        }

        // Update cursor position based on direction
        switch (dir) {
            case KeyInputHandler::Direction::Right:
                if (cursor_pos < text.length()) cursor_pos++;
                break;
            case KeyInputHandler::Direction::Left:
                if (cursor_pos > 0) cursor_pos--;
                break;
            // Vertical selection support (Up direction)
            case KeyInputHandler::Direction::Up: {
                unsigned int line, col;
                GetCursorPosition(line, col);
                if (line > 1) {
                    SetCursorPosition(line - 1, col);
                } else {
                    cursor_pos = 0; // Move to start of document (top line)
                }
                break;
            }
            // Vertical selection support (Down direction)
            case KeyInputHandler::Direction::Down: {
                unsigned int line, col;
                GetCursorPosition(line, col);
                unsigned int total_lines = GetTotalLines();
                if (line < total_lines) {
                    SetCursorPosition(line + 1, col);
                } else {
                    cursor_pos = text.length(); // Move to end of document (last line)
                }
                break;
            }
            default: return;
        }

        // Update selection range with new cursor position
        select_end = cursor_pos;

        // Clamp selection positions to valid range (text length)
        unsigned int text_len = static_cast<unsigned int>(text.length());
        select_start = std::min(select_start, text_len);
        select_end = std::min(select_end, text_len);

        // ========== CRITICAL: Zero-Length Selection Deselection ==========
        // Deactivate selection if start and end positions are the same (zero-length)
        unsigned int sel_start, sel_end;
        if (GetSelectionRange(sel_start, sel_end) && (sel_start == sel_end)) {
            DeactivateSelectMode(); // Cancel selection if range is zero
        }

        SyncScroll();
        UpdateStatusBar();
    }

    // Check if selection is valid (non-zero length)
    bool HasValidSelection() const {
        return select_active && (select_start != select_end);
    }

    // Get normalized selection range (start <= end)
    bool GetSelectionRange(unsigned int& out_start, unsigned int& out_end) const {
        if (!HasValidSelection()) {
            out_start = 0;
            out_end = 0;
            return false;
        }
        out_start = std::min(select_start, select_end);
        out_end = std::max(select_start, select_end);
        return true;
    }

    // Reset selection state (explicit call only)
    void ResetSelectionState() {
        DeactivateSelectMode();
    }

    // Push current state to undo stack (trim stack if exceeds max steps)
    // Optimized: Fixes stack trimming logic and skips save operations
    void PushUndoState() {
        // Skip pushing state for save operations (avoids undoing saves)
        if (undo_stack.empty()) return;

        // Trim undo stack if it exceeds maximum allowed steps
        if (undo_stack.size() >= MAX_UNDO_STEPS) {
            std::stack<EditOperation> temp;
            const std::size_t keep = MAX_UNDO_STEPS - 1;

            // Keep last 'keep' steps, remove oldest entries
            while (!undo_stack.empty()) {
                if (undo_stack.size() > keep) {
                    undo_stack.pop();
                } else {
                    temp.push(undo_stack.top());
                    undo_stack.pop();
                }
            }
            
            // Restore kept entries to undo stack
            while (!temp.empty()) {
                undo_stack.push(temp.top());
                temp.pop();
            }
        }

        // Push current state to undo stack (mark if it's a saved state)
        undo_stack.push(EditOperation(
            text, 
            cursor_pos, 
            is_modified, 
            (text == last_saved_text) // Mark if current state matches last saved state
        ));
        
        // Clear redo stack (new operations invalidate redo history)
        while (!redo_stack.empty()) redo_stack.pop();
    }

    // Undo last operation (return false if no undo available)
    // Optimized: Accurate modified state tracking based on saved content
    bool Undo() {
        // Prevent undoing initial state (at least one state must remain)
        if (undo_stack.size() <= 1) return false;

        // Push current state to redo stack before undoing
        redo_stack.push(EditOperation(
            text, 
            cursor_pos, 
            is_modified, 
            (text == last_saved_text)
        ));

        // Retrieve previous state from undo stack
        EditOperation op = undo_stack.top();
        undo_stack.pop();

        // Restore previous state
        text = op.prev_text;
        cursor_pos = op.prev_cursor;
        
        // Accurately set modified state (compare with last saved content)
        is_modified = (text != last_saved_text);

        SyncScroll();
        UpdateStatusBar(); // Update status bar after undo
        return true;
    }

    // Redo previously undone operation (return false if no redo available)
    // Optimized: Accurate modified state tracking based on saved content
    bool Redo() {
        if (redo_stack.empty()) return false;

        // Push current state to undo stack before redoing
        undo_stack.push(EditOperation(
            text, 
            cursor_pos, 
            is_modified, 
            (text == last_saved_text)
        ));

        // Retrieve redo state from redo stack
        EditOperation op = redo_stack.top();
        redo_stack.pop();

        // Restore redo state
        text = op.prev_text;
        cursor_pos = op.prev_cursor;
        
        // Accurately set modified state (compare with last saved content)
        is_modified = (text != last_saved_text);

        SyncScroll();
        UpdateStatusBar(); // Update status bar after redo
        return true;
    }

    // Convert cursor position (character index) to line/column numbers
    void GetCursorPosition(unsigned int& line, unsigned int& col) {
        line = 1;
        col = 1;
        size_t text_len = text.length();

        // Iterate through text to find line/column from character index
        for (size_t i = 0; i < cursor_pos && i < text_len; i++) {
            if (text[i] == '\n') {
                line++;
                col = 1; // Reset column on newline
            } else {
                col++; // Increment column for non-newline characters
            }
        }

        // Clamp line to valid range (1 to total lines)
        unsigned int total = GetTotalLines();
        line = std::min(line, total);
        col = std::max(static_cast<unsigned int>(1), col); // Ensure column is at least 1
    }

    // Set cursor position from line/column numbers (return false if invalid)
    bool SetCursorPosition(unsigned int target_line, unsigned int target_col) {
        unsigned int total_lines = GetTotalLines();
        // Clamp target line to valid range (1 to total lines)
        target_line = std::max(static_cast<unsigned int>(1), std::min(target_line, total_lines));

        // Find start position of target line
        unsigned int current_line = 1;
        unsigned int pos = 0;
        size_t text_len = text.length();

        while (pos < text_len && current_line < target_line) {
            if (text[pos] == '\n') current_line++;
            pos++;
        }
        unsigned int line_start = pos;

        // Find end position of target line (next newline or end of text)
        unsigned int line_end = line_start;
        while (line_end < text_len && text[line_end] != '\n') line_end++;

        // Calculate maximum valid column for target line
        unsigned int max_col = line_end - line_start + 1;
        // Clamp target column to valid range (1 to max_col)
        target_col = std::max(static_cast<unsigned int>(1), std::min(target_col, max_col));

        // Convert line/column to character index
        pos = line_start + (target_col - 1);
        cursor_pos = std::min(pos, static_cast<unsigned int>(text_len)); // Clamp to text length
        return true;
    }

    // Get start position (character index) of specified line number
    unsigned int GetLineStartPos(unsigned int line) {
        unsigned int total_lines = GetTotalLines();
        // Clamp line to valid range (1 to total lines)
        line = std::max(static_cast<unsigned int>(1), std::min(line, total_lines));

        // Iterate through text to find start of target line
        unsigned int current_line = 1;
        unsigned int pos = 0;
        size_t text_len = text.length();

        while (pos < text_len && current_line < line) {
            if (text[pos] == '\n') current_line++;
            pos++;
        }
        return pos;
    }

    // Get end position (character index) of specified line number
    unsigned int GetLineEndPos(unsigned int line) {
        unsigned int start = GetLineStartPos(line);
        unsigned int end = start;
        size_t text_len = text.length();

        // Find end of line (next newline or end of text)
        while (end < text_len && text[end] != '\n') end++;
        return end;
    }

    // Synchronize scroll position to keep cursor visible in viewport
    void SyncScroll(bool force = true) {
        line_height = ImGui::GetTextLineHeight();
        ImVec2 contentSize = ImGui::GetContentRegionAvail();
        // Calculate number of visible lines in viewport
        visible_lines = std::max(static_cast<unsigned int>(1), static_cast<unsigned int>(contentSize.y / line_height));

        // Get current cursor position in line/column format
        unsigned int cursor_line, cursor_col;
        GetCursorPosition(cursor_line, cursor_col);
        unsigned int total_lines = GetTotalLines();

        // Calculate viewport boundaries (start/end lines)
        unsigned int view_start = scroll_line;
        unsigned int view_end = scroll_line + visible_lines - 1;
        // Calculate maximum valid scroll line (prevents scrolling past last line)
        unsigned int max_scroll = std::max(static_cast<unsigned int>(1), total_lines - visible_lines + 1);

        // Calculate scroll margin (prevents cursor from touching viewport edges)
        unsigned int margin = std::max(static_cast<unsigned int>(1), visible_lines / 5);
        unsigned int target_scroll = scroll_line;

        // Adjust scroll if cursor is outside visible viewport
        bool cursor_visible = (cursor_line >= view_start && cursor_line <= view_end);
        if (!cursor_visible) {
            // Scroll up or down to bring cursor into view
            target_scroll = (cursor_line < view_start) ? (cursor_line - margin) : (cursor_line - (visible_lines - margin) + 1);
        } else {
            // Fine-tune scroll if cursor is near viewport edges
            if (scroll_line > 1 && cursor_line < (view_start + margin)) target_scroll = cursor_line - margin;
            else if (scroll_line < max_scroll && cursor_line > (view_end - margin)) target_scroll = cursor_line - (visible_lines - margin) + 1;
        }

        // Clamp target scroll to valid range (1 to max_scroll)
        target_scroll = std::max(static_cast<unsigned int>(1), std::min(target_scroll, max_scroll));
        if (total_lines <= visible_lines) target_scroll = 1; // No scroll needed if all lines fit

        // Update scroll position and ImGui scroll offset
        if (target_scroll != scroll_line) {
            scroll_line = target_scroll;
            ImGui::SetScrollY((scroll_line - 1) * line_height);
        }
    }

    // Get total number of lines in editor content
    unsigned int GetTotalLines() {
        if (text.empty()) return 1; // Empty text has 1 line

        unsigned int lines = 0;
        unsigned text_len = text.length();
        // Count newline characters to determine line count
        for (size_t i = 0; i < text_len; i++) {
            if (text[i] == '\n') lines++;
        }
        // Adjust for trailing newline (empty last line)
        return (text.back() == '\n') ? lines : lines + 1;
    }

    // Move cursor up one line (maintain column position)
    void MoveUp() {
        unsigned int line, col;
        GetCursorPosition(line, col);
        if (line > 1) {
            SetCursorPosition(line - 1, col);
        } else {
            cursor_pos = 0; // Move to start of document (top line)
        }
        SyncScroll();
        UpdateStatusBar();
    }

    // Move cursor down one line (maintain column position)
    void MoveDown() {
        unsigned int line, col;
        GetCursorPosition(line, col);
        unsigned int total_lines = GetTotalLines();
        if (line < total_lines) {
            SetCursorPosition(line + 1, col);
        } else {
            cursor_pos = text.length(); // Move to end of document (last line)
        }
        SyncScroll();
        UpdateStatusBar();
    }

    // Move cursor left one character
    void MoveLeft() { 
        if (cursor_pos > 0) cursor_pos--;
        SyncScroll();
        UpdateStatusBar();
    }

    // Move cursor right one character
    void MoveRight() { 
        if (cursor_pos < text.length()) cursor_pos++;
        SyncScroll();
        UpdateStatusBar();
    }

    // Update status bar with current editor state
    void UpdateStatusBar() {
        unsigned int line, col;
        GetCursorPosition(line, col);
        unsigned int total_lines = GetTotalLines();
        unsigned int view_end = std::min(scroll_line + visible_lines - 1, total_lines);

        // Build status string with current editor state
        std::string status = strings[cfg.lang][Lang::TextEditorStatusLine] + std::to_string(line) +
                            strings[cfg.lang][Lang::TextEditorStatusCol] + std::to_string(col) +
                            strings[cfg.lang][Lang::TextEditorStatusView] + std::to_string(scroll_line) + "-" + std::to_string(view_end) +
                            strings[cfg.lang][Lang::TextEditorStatusModified] + (is_modified ? strings[cfg.lang][Lang::CommonYes] : strings[cfg.lang][Lang::CommonNo]) +
                            strings[cfg.lang][Lang::TextEditorStatusMode] + (overwrite_mode ? strings[cfg.lang][Lang::CommonOverwrite] : strings[cfg.lang][Lang::CommonInsert]) +
                            strings[cfg.lang][Lang::TextEditorStatusSelect] + (select_active ? strings[cfg.lang][Lang::CommonOn] : strings[cfg.lang][Lang::CommonOff]) +
                            strings[cfg.lang][Lang::TextEditorStatusCaps] + (caps_lock ? strings[cfg.lang][Lang::CommonOn] : strings[cfg.lang][Lang::CommonOff]);

        TextEditor::SetStatus(status, false);
    }

    // Insert text with caps lock handling (replace selection if active)
    void InsertTextWithCaps(const std::string& insert_text) {
        // Replace selection with new text if selection is active
        if (HasValidSelection()) DeleteSelectedText();
        ResetSelectionState();

        // Save current state to undo stack before modification
        PushUndoState();
        std::string final_text = insert_text;

        // Apply caps lock to input text if enabled
        if (caps_lock) {
            for (char& c : final_text) {
                if (islower(c)) c = toupper(c);
            }
        }

        // Insert or overwrite text based on current input mode
        if (overwrite_mode) {
            size_t text_len = text.length();
            for (char c : final_text) {
                if (cursor_pos < text_len) {
                    text[cursor_pos++] = c; // Overwrite existing character
                } else {
                    text.push_back(c);     // Append to end of text
                    cursor_pos++;
                }
            }
        } else {
            // Insert text at cursor position (shift existing text right)
            text.insert(cursor_pos, final_text);
            cursor_pos += final_text.length(); // Move cursor past inserted text
        }

        // Mark content as modified (unsaved changes)
        is_modified = true;
        SyncScroll();
        UpdateStatusBar();
    }

    // Replace entire line with new content (return false if invalid line)
    bool ReplaceCurrentLine(unsigned int line_num, const std::string& new_content) {
        ResetSelectionState();
        unsigned int total_lines = GetTotalLines();
        // Return false if line number is out of valid range
        if (line_num < 1 || line_num > total_lines) return false;

        // Get current content of target line
        unsigned int line_start = GetLineStartPos(line_num);
        unsigned int line_end = GetLineEndPos(line_num);
        std::string current_content = text.substr(line_start, line_end - line_start);
        
        // Return false if new content is identical to current content (no change)
        if (current_content == new_content) return false;

        // Save current state to undo stack before modification
        PushUndoState();
        // Remove existing line content
        text.erase(line_start, line_end - line_start);
        // Insert new content at line start position
        text.insert(line_start, new_content);

        // Move cursor to end of modified line
        cursor_pos = line_start + new_content.length();
        cursor_pos = std::min(cursor_pos, static_cast<unsigned int>(text.length())); // Clamp to text length
        // Mark content as modified (unsaved changes)
        is_modified = true;

        SyncScroll();
        UpdateStatusBar();
        return true;
    }

    // Delete character before cursor (backspace)
    void DeleteBackward() {
        ResetSelectionState();
        // Only delete if cursor is not at start of text
        if (cursor_pos > 0) {
            // Save current state to undo stack before modification
            PushUndoState();
            // Delete character before cursor
            text.erase(cursor_pos - 1, 1);
            cursor_pos--; // Move cursor left
            // Mark content as modified (unsaved changes)
            is_modified = true;
            SyncScroll();
            UpdateStatusBar();
        }
    }

    // Get text content of current selection (empty if no valid selection)
    std::string GetSelectedText() {
        unsigned int s, e;
        // Return empty string if selection is invalid
        if (!GetSelectionRange(s, e)) return "";

        // Clamp selection range to valid text length
        unsigned int text_len = static_cast<unsigned int>(text.length());
        s = std::min(s, text_len);
        e = std::min(e, text_len);
        // Return empty string if selection is zero-length
        if (s >= e) return "";
        // Return selected text substring
        return text.substr(s, e - s);
    }

    // Delete current selection (no effect if no valid selection)
    void DeleteSelectedText() {
        unsigned int s, e;
        // Return if selection is invalid
        if (!GetSelectionRange(s, e)) return;

        // Save current state to undo stack before modification
        PushUndoState();
        // Clamp end position to valid text length
        e = std::min(e, static_cast<unsigned int>(text.length()));
        // Delete selected text from content
        text.erase(s, e - s);
        cursor_pos = s; // Move cursor to start of deleted selection
        DeactivateSelectMode(); // Deactivate selection after deletion
        // Mark content as modified (unsaved changes)
        is_modified = true;
        SyncScroll();
        UpdateStatusBar();
    }

    // Copy current selection to clipboard (retains selection)
    void CopySelectedText() {
        clipboard = GetSelectedText();
        // Do NOT deactivate selection here (retain selection after copy)
    }

    // Paste clipboard content at cursor position (clears selection)
    void PasteFromClipboard() {
        ResetSelectionState(); // Clear selection before paste
        // Only paste if clipboard is not empty
        if (!clipboard.empty()) InsertTextWithCaps(clipboard);
    }

    // Find next occurrence of find_text (wrap to start if not found)
    bool FindNext() {
        ResetSelectionState(); // Clear selection before find operation
        // Return false if find text is empty
        if (find_text.empty()) return false;

        // Search for find_text starting from cursor position + 1
        size_t pos = text.find(find_text, cursor_pos + 1);
        // Wrap to start of text if not found after cursor
        if (pos == std::string::npos) pos = text.find(find_text, 0);

        // Highlight found text if exists
        if (pos != std::string::npos) {
            cursor_pos = static_cast<unsigned int>(pos); // Move cursor to start of found text
            select_active = true; // Activate selection for found text
            select_start = cursor_pos;
            select_end = cursor_pos + static_cast<unsigned int>(find_text.length()); // Select entire found text
            SyncScroll(); // Ensure found text is visible
            UpdateStatusBar();
            return true;
        }
        return false; // Return false if text not found
    }

    // Find previous occurrence of find_text (wrap to end if not found)
    bool FindPrev() {
        ResetSelectionState(); // Clear selection before find operation
        // Return false if find text is empty
        if (find_text.empty()) return false;

        // Search for find_text backward from cursor position
        size_t pos = text.rfind(find_text, cursor_pos);
        // Wrap to end of text if not found before cursor
        if (pos == std::string::npos || static_cast<unsigned int>(pos) >= cursor_pos) {
            pos = text.rfind(find_text, std::string::npos);
        }

        // Highlight found text if exists
        if (pos != std::string::npos) {
            cursor_pos = static_cast<unsigned int>(pos); // Move cursor to start of found text
            select_active = true; // Activate selection for found text
            select_start = cursor_pos;
            select_end = cursor_pos + static_cast<unsigned int>(find_text.length()); // Select entire found text
            SyncScroll(); // Ensure found text is visible
            UpdateStatusBar();
            return true;
        }
        return false; // Return false if text not found
    }

    // Save editor content to file (return false if save fails)
    // Optimized: No undo stack push, update saved state marker
    bool Save(const std::string& file_path) {
        // Create copy of text with CRLF line endings (Windows compatible)
        std::string save_text = text;
        size_t pos = 0;
        while ((pos = save_text.find("\n", pos)) != std::string::npos) {
            save_text.replace(pos, 1, "\r\n");
            pos += 2;
        }

        // Open file for writing (binary mode, truncate existing content)
        std::ofstream file(file_path, std::ios::binary | std::ios::trunc);
        if (file.is_open()) {
            // Write content to file
            file << save_text;
            file.close();
            
            // Update saved state tracking
            last_saved_text = text;
            is_modified = false;

            // Update undo stack top to mark as saved (no new undo entry)
            if (!undo_stack.empty()) {
                EditOperation top = undo_stack.top();
                undo_stack.pop();
                // Replace top entry with saved state marker
                undo_stack.push(EditOperation(
                    top.prev_text, 
                    top.prev_cursor, 
                    false, 
                    true // Mark as saved state
                ));
            }

            UpdateStatusBar();
            return true; // Save successful
        }
        return false; // Save failed (file not open)
    }

    // Check if current content is different from last saved content
    bool IsContentChangedFromLastSave() const {
        return text != last_saved_text;
    }
};

// ========== Editor Global Management (Fixes Auto-Keyboard Popup & Selection Retention) ==========
namespace TextEditor {
    static Editor* current_editor = nullptr;      // Active editor instance (null if no editor open)
    static std::string file_path = "";            // Path of currently open file
    static std::string status_message = "";       // Current status bar message
    static bool custom_status = false;            // Whether status message is temporary/custom
    static bool confirm_exit = false;             // Confirm flag for exit with unsaved changes
    static std::chrono::steady_clock::time_point status_timeout; // Timeout for temporary status messages

    static bool is_keyboard_popup = false;        // Whether virtual keyboard popup is active
    static bool first_load = true;                // First frame flag (avoid accidental input)
    static bool ignore_next_a = true;             // Ignore first A press (prevents auto-keyboard on open)

    // Initialize editor with specified file path
    void Initialize(const std::string& path) {
        // Clean up existing editor instance if present
        confirm_exit = false;
        if (current_editor) delete current_editor;

        // Create new editor instance with file content
        file_path = path;
        current_editor = new Editor(path);

        // Set initial status message (file opened)
        status_message = strings[cfg.lang][Lang::TextEditorStatusFileOpened] + file_path;
        custom_status = false;
        // Set timeout for initial status message (3 seconds)
        status_timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);

        // Reset input state (critical for auto-popup fix)
        is_keyboard_popup = false;
        first_load = true;
        ignore_next_a = true; // Enable ignore for first A press
        g_key_handler.ResetAllStates();

        // Activate editor in GUI
        GUI::SetTextEditorActive(true);
    }

    // Shutdown editor and clean up resources
    void Shutdown() {
        // Clean up editor instance if present
        if (current_editor) delete current_editor;
        current_editor = nullptr;
        file_path = "";
        status_message = "";
        custom_status = false;
        g_key_handler.ResetAllStates();

        // Reset ignore flag for A press
        ignore_next_a = false;

        // Deactivate editor in GUI
        GUI::SetTextEditorActive(false);
    }

    // Set status bar text (custom/temporary or permanent)
    void SetStatus(const std::string& msg, bool custom) {
        status_message = msg;
        custom_status = custom;
        // Set timeout for temporary status messages (3 seconds)
        if (custom) status_timeout = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    }

    // Handle all editor input (gamepad/keyboard/mouse)
    void HandleInput() {
        if (!current_editor) return; // Return if no active editor

        // Update gamepad state (ImGui Switch backend)
        ImGui_ImplSwitch_UpdateGamepads();

        ImGuiIO& io = ImGui::GetIO();
        // Return if gamepad navigation is disabled
        if ((io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) == 0) return;

        // Skip first frame to avoid accidental input
        if (first_load) {
            first_load = false;
            return;
        }

        // Update key state tracker (critical for input handling)
        g_key_handler.UpdateKeyState();

        // L button selection logic
        bool l_key_down = g_key_handler.IsKeyDown(HidNpadButton_L);
        bool l_key_currently_held = g_key_handler.IsKeyCurrentlyHeld(HidNpadButton_L);
        bool l_key_released = g_key_handler.IsKeyReleased(HidNpadButton_L);
        bool find_active = current_editor->find_active;

        // Activate selection mode (on L button press, if not active)
        if (l_key_down && !current_editor->select_active && !find_active) {
            current_editor->ActivateSelectMode();
            current_editor->ResetSelectSessionFlags();
            SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOn], true);
        }

        // Deactivate selection mode (on L button press, if active - exit selection)
        if (l_key_down && current_editor->select_active && !find_active) {
            if (!current_editor->is_first_l_press_in_select) {
                current_editor->DeactivateSelectMode();
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOff], true);
                current_editor->ResetSelectSessionFlags();
            }
            current_editor->is_first_l_press_in_select = false;
        }

        // Extended selection while L is held (continuous press support)
        if (current_editor->select_active && l_key_currently_held && !find_active) {
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Left, ImGuiKey_GamepadDpadLeft, [&]() {
                current_editor->ExtendSelection(KeyInputHandler::Direction::Left);
                current_editor->has_extended_selection = true;
            });
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Right, ImGuiKey_GamepadDpadRight, [&]() {
                current_editor->ExtendSelection(KeyInputHandler::Direction::Right);
                current_editor->has_extended_selection = true;
            });
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Up, ImGuiKey_GamepadDpadUp, [&]() {
                current_editor->ExtendSelection(KeyInputHandler::Direction::Up);
                current_editor->has_extended_selection = true;
            });
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Down, ImGuiKey_GamepadDpadDown, [&]() {
                current_editor->ExtendSelection(KeyInputHandler::Direction::Down);
                current_editor->has_extended_selection = true;
            });
        }

        // ========== L-release deselection (only if no extended selection) ==========
        if (l_key_released && current_editor->select_active && !find_active) {
            if (!current_editor->has_extended_selection) {
                current_editor->DeactivateSelectMode();
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOff], true);
                current_editor->ResetSelectSessionFlags();
            }
            current_editor->is_first_l_press_in_select = true;
        }

        // Reset first L press flag if L is released and selection is inactive
        if (l_key_released && !current_editor->select_active) {
            current_editor->is_first_l_press_in_select = true;
        }

        // ========== Ignore first A press (prevents auto-keyboard popup on open) ==========
        if (g_key_handler.IsKeyDown(HidNpadButton_A) && !is_keyboard_popup) {
            if (ignore_next_a) {
                ignore_next_a = false; // Reset ignore flag after first A press
                return; // Skip keyboard popup for file-open A press
            }
            current_editor->DeactivateSelectMode();
            is_keyboard_popup = true;

            // Get current line content for editing (virtual keyboard input)
            unsigned int edit_line, edit_col;
            current_editor->GetCursorPosition(edit_line, edit_col);
            unsigned int line_start = current_editor->GetLineStartPos(edit_line);
            unsigned int line_end = current_editor->GetLineEndPos(edit_line);
            std::string initial_text = current_editor->text.substr(line_start, line_end - line_start);

            // Show virtual keyboard and get input text
            std::string input = Keyboard::GetText(strings[cfg.lang][Lang::TextEditorEditLine] + std::to_string(edit_line), initial_text);
            is_keyboard_popup = false;

            // Update line content if input is valid (not empty/canceled)
            if (!input.empty() && input != strings[cfg.lang][Lang::KeyboardEmpty]) {
                current_editor->ReplaceCurrentLine(edit_line, input);
                SetStatus(strings[cfg.lang][Lang::TextEditorUpdatedLine] + std::to_string(edit_line), true);
            } else {
                SetStatus(strings[cfg.lang][Lang::TextEditorCancelEdit], true);
            }
            return;
        }

        // Block other input while virtual keyboard is active
        if (is_keyboard_popup) return;

        // Minus button: Exit editor (confirm if unsaved changes)
        if (g_key_handler.IsKeyDown(HidNpadButton_Minus)) {
            current_editor->DeactivateSelectMode();
            bool need_exit = false;
            if (current_editor->is_modified) {
                // First press: show confirm message, second press: exit
                if (confirm_exit) need_exit = true;
                else {
                    SetStatus(strings[cfg.lang][Lang::TextEditorStatusChangesUnsaved], true);
                    confirm_exit = true;
                    return;
                }
            } else {
                need_exit = true; // No unsaved changes - exit immediately
            }

            // Exit to file browser
            if (need_exit) {
                Shutdown();
                data.state = WINDOW_STATE_FILEBROWSER;
                return;
            }
        }

        // Plus button: Save current file (optimized status messages)
        if (g_key_handler.IsKeyDown(HidNpadButton_Plus)) {
            current_editor->DeactivateSelectMode();
            bool contentChanged = current_editor->IsContentChangedFromLastSave();
            if (contentChanged) {
                bool save_success = current_editor->Save(file_path);
                if (save_success) {
                    SetStatus(strings[cfg.lang][Lang::TextEditorStatusSaved] + file_path, false);
                } else {
                    SetStatus(strings[cfg.lang][Lang::TextEditorStatusSaveFailed], true);
                }
            } else {
                // Show detailed no-changes message with filename
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusNoChangesToSave], false);
            }
        }

        // X button: Copy selected text to clipboard (RETAINS selection)
        if (g_key_handler.IsKeyDown(HidNpadButton_X)) {
            if (current_editor->HasValidSelection()) {
                std::string selected = current_editor->GetSelectedText();
                if (!selected.empty()) {
                    current_editor->CopySelectedText();
                    current_editor->DeactivateSelectMode();
                    // Truncate long text in status message (max 20 chars)
                    if (selected.length() > 20) selected = selected.substr(0, 20) + "...";
                    SetStatus(strings[cfg.lang][Lang::TextEditorStatusCopiedCharacters] + selected, true);
                } else {
                    SetStatus(strings[cfg.lang][Lang::TextEditorStatusNoTextToCopy], true);
                }
            } else {
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusNoTextToCopy], true);
            }
        }

        // Y button: Paste clipboard content (CLEARS selection)
        if (g_key_handler.IsKeyDown(HidNpadButton_Y)) {
            current_editor->DeactivateSelectMode();
            if (!current_editor->clipboard.empty()) {
                current_editor->PasteFromClipboard();
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusPasted], true);
            } else {
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusClipboardEmpty], true);
            }
        }

        // ZL button: Undo last operation (optimized status messages)
        if (g_key_handler.IsKeyDown(HidNpadButton_ZL)) {
            current_editor->DeactivateSelectMode();
            if (current_editor->Undo()) {
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusUndoSuccessful], true);
            } else {
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusNothingToUndo], true);
            }
        }

        // ZR button: Redo previously undone operation (optimized status messages)
        if (g_key_handler.IsKeyDown(HidNpadButton_ZR)) {
            current_editor->DeactivateSelectMode();
            if (current_editor->Redo()) {
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusRedoSuccessful], true);
            } else {
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusNothingToRedo], true);
            }
        }

        // R button + Direction: Find operations (CLEARS selection)
        if (g_key_handler.IsHeldKeyCombo(HidNpadButton_R, HidNpadButton_Right)) {
            current_editor->DeactivateSelectMode();
            current_editor->find_active = true;
            if (!current_editor->find_text.empty()) {
                if (current_editor->FindNext()) SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + current_editor->find_text + "\"", true);
                else SetStatus("\"" + current_editor->find_text + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
            }
        } else if (g_key_handler.IsHeldKeyCombo(HidNpadButton_R, HidNpadButton_Left)) {
            current_editor->DeactivateSelectMode();
            current_editor->find_active = true;
            if (!current_editor->find_text.empty()) {
                if (current_editor->FindPrev()) SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + current_editor->find_text + "\"", true);
                else SetStatus("\"" + current_editor->find_text + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
            }
        } else if (g_key_handler.IsKeyDown(HidNpadButton_R)) {
            // Open find dialog with virtual keyboard (CLEARS selection)
            current_editor->DeactivateSelectMode();
            std::string input = Keyboard::GetText(strings[cfg.lang][Lang::TextEditorStatusFindText], current_editor->find_text);
            if (!input.empty()) {
                current_editor->find_text = input;
                current_editor->find_pos = current_editor->cursor_pos;
                current_editor->find_active = true;

                // Find first occurrence of new search text
                if (current_editor->FindNext()) SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + input + "\"", true);
                else SetStatus("\"" + input + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
            }
        }

        // B button: Delete selection or backspace (CLEARS selection)
        if (g_key_handler.IsKeyDown(HidNpadButton_B)) {
            current_editor->DeactivateSelectMode();
            if (current_editor->HasValidSelection()) {
                current_editor->DeleteSelectedText();
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedSelectedText], true);
            } else {
                current_editor->DeleteBackward();
                SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedPreCharacter], true);
            }
        }

        // Direction pad: Move cursor (with repeat) - only when not selecting
        if (!current_editor->select_active && !l_key_currently_held) {
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Up, ImGuiKey_GamepadDpadUp, [&]() {
                current_editor->MoveUp();
            });
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Down, ImGuiKey_GamepadDpadDown, [&]() {
                current_editor->MoveDown();
            });
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Left, ImGuiKey_GamepadDpadLeft, [&]() {
                current_editor->MoveLeft();
            });
            g_key_handler.HandleDirectionRepeat(KeyInputHandler::Direction::Right, ImGuiKey_GamepadDpadRight, [&]() {
                current_editor->MoveRight();
            });
        }

        // Reset temporary status message after timeout
        if (custom_status && std::chrono::steady_clock::now() > status_timeout) {
            current_editor->UpdateStatusBar();
            custom_status = false;
        }
    }

    // Handle mouse click to set cursor position (clears selection)
    void HandleMouseClick(const ImVec2& click_pos, const ImVec2& scroll_pos, float line_height) {
        if (!current_editor) return;

        // Deactivate selection on mouse click (user explicit action)
        current_editor->DeactivateSelectMode();
        // Calculate clicked line number from mouse position
        unsigned int click_line = current_editor->scroll_line + (int)((click_pos.y - scroll_pos.y) / line_height);
        unsigned int total_lines = current_editor->GetTotalLines();
        // Clamp clicked line to valid range
        click_line = std::clamp(click_line, static_cast<unsigned int>(1), total_lines);

        // Calculate clicked column number from mouse position
        float char_width = ImGui::CalcTextSize(" ").x;
        unsigned int click_col = (int)((click_pos.x - ImGui::GetWindowPos().x - 40) / char_width) + 1;
        click_col = std::max(click_col, static_cast<unsigned int>(1)); // Ensure column is at least 1

        // Set cursor to clicked position
        current_editor->SetCursorPosition(click_line, click_col);
        current_editor->SyncScroll(); // Ensure cursor is visible
        current_editor->UpdateStatusBar();
    }
}

// ========== GUI Rendering (No functional changes) ==========
namespace Windows {
    void TextEditor() {
        // Set editor window position and size (full screen, fixed)
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_Once);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f); // No window rounding

        // Main editor window (no move/resize/collapse/title bar)
        if (ImGui::Begin("TextEditor", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {
            // Show controls hint if editor is active
            if (TextEditor::current_editor) {
                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", strings[cfg.lang][Lang::TextEditorControls]);
            }

            // Status bar separator
            ImGui::Separator();
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            contentSize.y -= 40; // Reserve space for status bar (40 pixels)

            // Scrollable text viewport
            ImGui::BeginChild("TextScrollView", contentSize, true, ImGuiWindowFlags_HorizontalScrollbar);

            if (TextEditor::current_editor) {
                Editor* editor = TextEditor::current_editor;
                editor->line_height = ImGui::GetTextLineHeight();
                // Calculate number of visible lines in viewport
                editor->visible_lines = std::max(static_cast<float>(1), contentSize.y / editor->line_height);

                // Calculate visible line range (start/end lines)
                unsigned int total_lines = editor->GetTotalLines();
                unsigned int start_line = editor->scroll_line;
                unsigned int end_line = std::min(start_line + editor->visible_lines, total_lines);

                // Render visible lines
                std::istringstream iss(editor->text);
                std::string line;
                unsigned int line_num = 1;

                // Skip lines above viewport (not visible)
                while (line_num < start_line && std::getline(iss, line)) line_num++;

                // Render visible lines with line numbers and selection highlight
                while (line_num <= end_line && std::getline(iss, line)) {
                    unsigned int cursor_line, cursor_col;
                    editor->GetCursorPosition(cursor_line, cursor_col);
                    
                    // Highlight current line number (white) - others are gray
                    ImGui::TextColored(
                        line_num == cursor_line ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                        "%6d", line_num
                    );
                    ImGui::SameLine(75.0f); // Align text after line numbers (75 pixels offset)

                    // Draw selection highlight if active
                    ImVec2 line_pos = ImGui::GetCursorScreenPos();
                    ImDrawList* draw_list = ImGui::GetWindowDrawList();
                    if (editor->select_active) {
                        unsigned int sel_start, sel_end;
                        if (editor->GetSelectionRange(sel_start, sel_end)) {
                            unsigned int line_start = editor->GetLineStartPos(line_num);
                            unsigned int line_end_pos = editor->GetLineEndPos(line_num);

                            // Calculate selection bounds for current line
                            unsigned int draw_start = std::max(sel_start, line_start);
                            unsigned int draw_end = std::min(sel_end, line_end_pos);

                            // Draw selection rectangle if valid (non-zero length)
                            if (draw_start < draw_end) {
                                float x_start = line_pos.x + ImGui::CalcTextSize(line.substr(0, draw_start - line_start).c_str()).x;
                                float x_end = line_pos.x + ImGui::CalcTextSize(line.substr(0, draw_end - line_start).c_str()).x;
                                // Selection color: blue (0x0066CC) with 70% alpha (0xAA)
                                draw_list->AddRectFilled(ImVec2(x_start, line_pos.y), ImVec2(x_end, line_pos.y + editor->line_height), ImColor(0x00, 0x66, 0xCC, 0xAA));
                            }
                        }
                    }

                    // Render line text
                    ImGui::Text("%s", line.c_str());

                    // Draw blinking cursor on current line
                    if (line_num == cursor_line) {
                        unsigned int cursor_col_draw = cursor_col - 1;
                        cursor_col_draw = std::min(cursor_col_draw, static_cast<unsigned int>(line.length()));
                        float cursor_x = line_pos.x + ImGui::CalcTextSize(line.substr(0, cursor_col_draw).c_str()).x;
                        static float cursor_flash = 0.0f;
                        cursor_flash += ImGui::GetIO().DeltaTime * 6.0f; // Flash speed: 6 Hz
                        
                        // Blinking effect using sine wave (alpha 0.3 to 1.0)
                        float alpha = std::sin(cursor_flash) > 0 ? 1.0f : 0.3f;
                        // Cursor color: teal (0.1, 0.7, 0.6) with variable alpha
                        draw_list->AddRectFilled(ImVec2(cursor_x, line_pos.y), ImVec2(cursor_x + 2, line_pos.y + editor->line_height), ImColor(0.1f, 0.7f, 0.6f, alpha));
                    }

                    line_num++;
                }

                // Handle mouse click for cursor positioning
                if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) {
                    ImVec2 click_pos = ImGui::GetMousePos();
                    ImVec2 scroll_pos = { ImGui::GetScrollX(), ImGui::GetScrollY() };
                    // Adjust click position for window offset and frame height
                    click_pos.y -= ImGui::GetWindowPos().y + ImGui::GetFrameHeightWithSpacing();
                    TextEditor::HandleMouseClick(click_pos, scroll_pos, editor->line_height);
                }
            }

            ImGui::EndChild();
            ImGui::Separator();
            
            // Status bar text (gray color)
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", TextEditor::status_message.c_str());
        }

        ImGui::End();
        ImGui::PopStyleVar(); // Restore style var (window rounding)
    }
}