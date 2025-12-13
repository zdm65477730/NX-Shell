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
#include <unordered_set>
#include "windows.hpp"

#include "imgui.h"
#include "imgui_impl_switch.hpp"
#include "windows.hpp"
#include "fs.hpp"
#include "keyboard.hpp"
#include "language.hpp"
#include "config.hpp"
#include "log.hpp"

// Global configuration constants (centralized management)
namespace Config {
    // Editor constants
    constexpr size_t MAX_UNDO_STEPS = 50;                  // Maximum number of undo steps
    constexpr unsigned int STATUS_TIMEOUT_SEC = 3;         // Status message timeout in seconds
    constexpr float EDITOR_WINDOW_WIDTH = 1280.0f;         // Editor window width
    constexpr float EDITOR_WINDOW_HEIGHT = 720.0f;         // Editor window height
    constexpr float STATUS_BAR_HEIGHT = 40.0f;             // Status bar height
    constexpr float LINE_NUMBER_OFFSET = 75.0f;            // Width of line number area (prevents cursor overlap)
    constexpr float CURSOR_WIDTH = 2.0f;                   // Cursor width in pixels
    constexpr float CURSOR_FLASH_SPEED = 6.0f;             // Cursor blink animation speed
    constexpr float TEXT_PADDING_X = 8.0f;                 // Horizontal padding for text area
    constexpr float TEXT_PADDING_Y = 2.0f;                 // Vertical padding for text area
    constexpr float SCROLL_THRESHOLD_RATIO = 0.25f;        // Scroll trigger threshold (1/4 of viewport height)

    // Input handler constants
    constexpr unsigned int HOLD_THRESHOLD = 15;            // Increased threshold for long key press (slower initial repeat)
    constexpr unsigned int REPEAT_THRESHOLD = 50;          // Threshold for key repeat activation
    constexpr unsigned int REPEAT_STEP_INTERVAL = 15;      // Faster repeat interval for delete (15ms per step)
    constexpr unsigned int REPEAT_MAX_EXTRA = 3;           // Maximum extra repeat steps (faster acceleration)
    constexpr unsigned int SINGLE_CLICK_FRAMES = 2;        // Frames to detect single click
    constexpr unsigned int DELETE_INITIAL_DELAY = 10;      // Reduced initial delay (from 20 to 10)
    constexpr unsigned int DELETE_REPEAT_INTERVAL = 5;     // Reduced repeat interval (from 10 to 5) for faster delete
    constexpr unsigned int DELETE_MAX_INTERVAL = 3;        // Maximum acceleration level (constant speed)
}

// ========== Module 1: Pure Input State Management (Decoupled from ImGui/Editor) ==========
class KeyInputHandler {
public:
    enum class Direction { Up, Down, Left, Right, None };

    // Constructor: Initialize input state variables
    KeyInputHandler() : m_lastPressedDir(Direction::None),
                        m_deleteHoldCount(0), m_lastDeleteTime(0) {
        // Initialize direction repeat counters and single-click flags
        for (int i = 0; i < static_cast<int>(Direction::None); ++i) {
            m_dirRepeatCounts[i] = 0;
            m_dirSingleClicked[i] = false;
        }
    }

    // Update input state from imgui_impl_switch
    void Update() {
        m_deleteHoldCount = ImGui_ImplSwitch_GetKeyState(HidNpadButton_B)->hold_count;
    }

    // Clear key state completely (prevents repeat triggers)
    void ClearKeyCompletely(HidNpadButton key) {
        ImGui_ImplSwitch_ResetKeyStates(); // Reset all states to prevent residual triggers
    }

    // Detect edge-triggered key down event (only true on first press frame)
    bool IsKeyDown(HidNpadButton key) const {
        return ImGui_ImplSwitch_GetKeyState(key)->is_down;
    }

    // Detect edge-triggered key release event
    bool IsKeyReleased(HidNpadButton key) const {
        return ImGui_ImplSwitch_GetKeyState(key)->is_up;
    }

    // Detect level-triggered long key press
    bool IsKeyHeld(HidNpadButton key) const {
        return ImGui_ImplSwitch_GetKeyState(key)->is_held;
    }

    // Detect level-triggered current key press
    bool IsKeyPressed(HidNpadButton key) const {
        return ImGui_ImplSwitch_GetKeyState(key)->is_pressed;
    }

    // Handle direction key repeat logic
    void HandleDirectionRepeat(Direction dir, const std::function<void()>& onTrigger) {
        if (!onTrigger || dir == Direction::None) return;

        unsigned int& count = m_dirRepeatCounts[static_cast<int>(dir)];
        bool& isSingleClicked = m_dirSingleClicked[static_cast<int>(dir)];

        HidNpadButton dirKey;
        switch (dir) {
            case Direction::Up: dirKey = HidNpadButton_Up; break;
            case Direction::Down: dirKey = HidNpadButton_Down; break;
            case Direction::Left: dirKey = HidNpadButton_Left; break;
            case Direction::Right: dirKey = HidNpadButton_Right; break;
            default: return;
        }

        bool isKeyPressed = IsKeyPressed(dirKey);
        if (!isKeyPressed) {
            count = 0;
            isSingleClicked = false;
            if (m_lastPressedDir == dir) m_lastPressedDir = Direction::None;
            return;
        }

        if (m_lastPressedDir != dir) {
            if (m_lastPressedDir != Direction::None) {
                m_dirRepeatCounts[static_cast<int>(m_lastPressedDir)] = 0;
                m_dirSingleClicked[static_cast<int>(m_lastPressedDir)] = false;
            }
            m_lastPressedDir = dir;
        }

        count++;
        if (count < Config::SINGLE_CLICK_FRAMES) return;

        unsigned int steps = 0;
        if (count <= Config::REPEAT_THRESHOLD) {
            if (!isSingleClicked) {
                steps = 1;
                isSingleClicked = true;
            }
        } else {
            unsigned int extra = (count - Config::REPEAT_THRESHOLD) / Config::REPEAT_STEP_INTERVAL;
            extra = std::min(extra, Config::REPEAT_MAX_EXTRA);
            steps = 1 + extra;
        }

        const unsigned int maxStepsPerFrame = 1;
        steps = std::min(steps, maxStepsPerFrame);
        for (size_t i = 0; i < steps; ++i) {
            onTrigger();
        }
    }

    // Handle delete key logic (only B button: single press = 1 char, long press = constant acceleration delete)
    bool ShouldDelete() {
        bool bKeyDown = IsKeyDown(HidNpadButton_B);
        bool deleteKeyHeld = IsKeyHeld(HidNpadButton_B);

        if (bKeyDown) {
            m_lastDeleteTime = m_deleteHoldCount;
            return true;
        }

        if (deleteKeyHeld && m_deleteHoldCount > Config::DELETE_INITIAL_DELAY) {
            unsigned int timeSinceLastDelete = m_deleteHoldCount - m_lastDeleteTime;
            unsigned int accelerationFactor = std::min(m_deleteHoldCount / 30, Config::DELETE_MAX_INTERVAL);
            unsigned int requiredInterval = Config::DELETE_REPEAT_INTERVAL - accelerationFactor;
            requiredInterval = std::max(requiredInterval, 1U);

            if (timeSinceLastDelete >= requiredInterval) {
                m_lastDeleteTime = m_deleteHoldCount;
                return true;
            }
        }

        return false;
    }

    // Reset all input states (call on editor shutdown)
    void Reset() {
        m_lastPressedDir = Direction::None;
        m_deleteHoldCount = 0;
        m_lastDeleteTime = 0;
        ImGui_ImplSwitch_ResetKeyStates(); // Reset global key states
        for (int i = 0; i < static_cast<int>(Direction::None); ++i) {
            m_dirRepeatCounts[i] = 0;
            m_dirSingleClicked[i] = false;
        }
    }

private:
    unsigned int m_dirRepeatCounts[static_cast<int>(Direction::None)];
    bool m_dirSingleClicked[static_cast<int>(Direction::None)];
    Direction m_lastPressedDir;
    unsigned int m_deleteHoldCount;
    unsigned int m_lastDeleteTime;
};

// Global input handler instance
static KeyInputHandler g_keyInputHandler;

// ========== Module 2: Core Editor Logic (Pure Text Processing, Decoupled from GUI/Input) ==========
class TextEditorCore {
public:
    // Undo/Redo state structure
    struct EditState {
        std::string text;
        unsigned int cursorPos;
        bool isModified;
        bool isSaved;

        EditState(const std::string& t, unsigned int c, bool m, bool s)
            : text(t), cursorPos(c), isModified(m), isSaved(s) {}
    };

    // Selection state structure
    struct Selection {
        unsigned int start = 0;
        unsigned int end = 0;
        bool active = false;

        // Get normalized selection range (start <= end)
        bool GetNormalizedRange(unsigned int& outStart, unsigned int& outEnd) const {
            if (!active || start == end) return false;
            outStart = std::min(start, end);
            outEnd = std::max(start, end);
            return true;
        }

        // Check if selection is valid (non-zero length)
        bool IsValid() const { return active && start != end; }

        // Reset selection state
        void Reset() { start = end = 0; active = false; }
    };

    // Constructor: Initialize editor with file content
    TextEditorCore(const std::string& filePath) : 
        m_filePath(filePath), m_fileSize(0), m_isModified(false),
        m_cursorPos(0), m_scrollLine(1), m_visibleLines(20), m_lineHeight(0.0f),
        m_findPos(0), m_findActive(false) {

        // Load file and normalize newlines (CRLF -> LF)
        LoadFile(filePath);

        m_lastSavedText = m_text;

        // Initialize undo stack with initial state
        m_undoStack.emplace(m_text, m_cursorPos, false, true);

        // Force initial scroll position to 1
        m_scrollLine = 1;
    }

    // Insert text at current cursor position
    void InsertText(const std::string& text) {
        // Replace selected text if selection exists
        if (m_selection.IsValid()) {
            DeleteSelectedText();
        }

        // Save state for undo
        PushUndoState();
        std::string insertText = text;

        // Insert or overwrite text based on mode
        m_text.insert(m_cursorPos, insertText);
        m_cursorPos += insertText.length();

        // Mark as modified and update scroll
        m_isModified = true;
        SyncScroll();
    }

    // Delete character before cursor (single deletion)
    void DeleteBackward() {
        if (m_cursorPos == 0) return;

        // Save state for undo
        PushUndoState();
        m_text.erase(m_cursorPos - 1, 1);
        m_cursorPos--;

        // Mark as modified and update scroll
        m_isModified = true;
        SyncScroll();
    }

    // Delete selected text
    void DeleteSelectedText() {
        unsigned int start, end;
        if (!m_selection.GetNormalizedRange(start, end)) return;

        // Save state for undo
        PushUndoState();
        end = std::min(end, static_cast<unsigned int>(m_text.length()));
        m_text.erase(start, end - start);
        m_cursorPos = start;
        m_selection.Reset();

        // Mark as modified and update scroll
        m_isModified = true;
        SyncScroll();
    }

    // Save current text to file (convert LF -> CRLF for Windows compatibility)
    bool Save() {
        std::string saveText = m_text;
        size_t pos = 0;
        while ((pos = saveText.find("\n", pos)) != std::string::npos) {
            saveText.replace(pos, 1, "\r\n");
            pos += 2;
        }

        // Open file for writing (binary mode to preserve newlines)
        std::ofstream file(m_filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        // Write content and close file
        file << saveText;
        file.close();

        // Update saved state
        m_lastSavedText = m_text;
        m_isModified = false;

        // Update undo stack (mark top state as saved)
        if (!m_undoStack.empty()) {
            EditState top = m_undoStack.top();
            m_undoStack.pop();
            m_undoStack.emplace(top.text, top.cursorPos, false, true);
        }

        return true;
    }

    // Undo last edit operation
    bool Undo() {
        if (m_undoStack.size() <= 1) return false;

        // Save current state to redo stack
        m_redoStack.emplace(m_text, m_cursorPos, m_isModified, (m_text == m_lastSavedText));

        // Restore previous state from undo stack
        EditState state = m_undoStack.top();
        m_undoStack.pop();

        m_text = state.text;
        m_cursorPos = state.cursorPos;
        m_isModified = state.isModified;

        // Update scroll position
        SyncScroll();
        return true;
    }

    // Redo previously undone operation
    bool Redo() {
        if (m_redoStack.empty()) return false;

        // Save current state to undo stack
        m_undoStack.emplace(m_text, m_cursorPos, m_isModified, (m_text == m_lastSavedText));

        // Restore redo state
        EditState state = m_redoStack.top();
        m_redoStack.pop();

        m_text = state.text;
        m_cursorPos = state.cursorPos;
        m_isModified = state.isModified;

        // Update scroll position
        SyncScroll();
        return true;
    }

    // Copy selected text to clipboard
    void CopySelectedText(std::string& outClipboard) {
        unsigned int start, end;
        if (m_selection.GetNormalizedRange(start, end)) {
            outClipboard = m_text.substr(start, end - start);
        } else {
            outClipboard.clear();
        }
    }

    // Paste text from clipboard at current cursor position
    void PasteText(const std::string& clipboard) {
        if (clipboard.empty()) return;
        m_selection.Reset();
        InsertText(clipboard);
    }

    // Replace content of specified line
    void ReplaceCurrentLine(unsigned int lineNum, const std::string& newContent) {
        unsigned int totalLines = GetTotalLines();
        if (lineNum < 1 || lineNum > totalLines) return;

        // Get current line boundaries
        unsigned int lineStart = GetLineStartPos(lineNum);
        unsigned int lineEnd = GetLineEndPos(lineNum);
        std::string currentContent = m_text.substr(lineStart, lineEnd - lineStart);

        // No change needed if content is identical
        if (currentContent == newContent) return;

        // Save state for undo
        PushUndoState();
        m_text.erase(lineStart, lineEnd - lineStart);
        m_text.insert(lineStart, newContent);

        // Move cursor to end of modified line
        m_cursorPos = lineStart + newContent.length();
        m_cursorPos = std::min(m_cursorPos, static_cast<unsigned int>(m_text.length()));
        m_isModified = true;

        // Update scroll position
        SyncScroll();
    }

    // Move cursor in specified direction
    void MoveCursor(KeyInputHandler::Direction dir) {
        switch (dir) {
            case KeyInputHandler::Direction::Up: {
                unsigned int line, col;
                GetCursorLineCol(line, col);
                if (line > 1) {
                    unsigned int targetLine = line - 1;
                    unsigned int targetLineStart = GetLineStartPos(targetLine);
                    unsigned int targetLineEnd = GetLineEndPos(targetLine);
                    unsigned int targetLineLength = targetLineEnd - targetLineStart;
                    unsigned int adjustedCol = std::min(col, targetLineLength == 0 ? 1U : targetLineLength);
                    SetCursorPosition(targetLine, adjustedCol);
                }
                break;
            }
            case KeyInputHandler::Direction::Down: {
                unsigned int line, col;
                GetCursorLineCol(line, col);
                unsigned int totalLines = GetTotalLines();
                if (line < totalLines) {
                    unsigned int targetLine = line + 1;
                    unsigned int targetLineStart = GetLineStartPos(targetLine);
                    unsigned int targetLineEnd = GetLineEndPos(targetLine);
                    unsigned int targetLineLength = targetLineEnd - targetLineStart;
                    unsigned int adjustedCol = std::min(col, targetLineLength == 0 ? 1U : targetLineLength);
                    SetCursorPosition(targetLine, adjustedCol);
                }
                break;
            }
            case KeyInputHandler::Direction::Left:
                if (m_cursorPos > 0) m_cursorPos--;
                break;
            case KeyInputHandler::Direction::Right:
                if (m_cursorPos < m_text.length()) m_cursorPos++;
                break;
            default: break;
        }
        // Update scroll position after cursor movement
        SyncScroll();
    }

    // Activate text selection at current cursor position
    void ActivateSelection() {
        if (!m_selection.active) {
            m_selection.start = m_cursorPos;
            m_selection.end = m_cursorPos;
            m_selection.active = true;
        }
    }

    // Deactivate text selection
    void DeactivateSelection() {
        m_selection.Reset();
    }

    // Extend selection in specified direction
    void ExtendSelection(KeyInputHandler::Direction dir) {
        ActivateSelection();
        MoveCursor(dir);
        m_selection.end = m_cursorPos;

        // Auto-deactivate selection if zero-length
        if (m_selection.start == m_selection.end) {
            DeactivateSelection();
        }

        // Update scroll position
        SyncScroll();
    }

    // Precisely set cursor position with column limit
    void SetCursorPosition(unsigned int targetLine, unsigned int targetCol) {
        unsigned int totalLines = GetTotalLines();
        targetLine = std::clamp(targetLine, 1U, totalLines);

        // Calculate target line boundaries
        unsigned int lineStart = GetLineStartPos(targetLine);
        unsigned int lineEnd = GetLineEndPos(targetLine);
        unsigned int lineLength = lineEnd - lineStart;

        // Limit column to valid range
        unsigned int validCol = (lineLength == 0) ? 1 : std::min(targetCol, lineLength);

        // Calculate final cursor position
        m_cursorPos = lineStart + (validCol - 1);
        m_cursorPos = std::min(m_cursorPos, static_cast<unsigned int>(m_text.length()));
    }

    // Refactored scroll logic with explicit handling for cursor moving outside viewport
    void SyncScroll() {
        unsigned int cursorLine, cursorCol;
        GetCursorLineCol(cursorLine, cursorCol);
        unsigned int totalLines = GetTotalLines();

        // Calculate viewport parameters
        unsigned int viewportLines = m_visibleLines;
        unsigned int scrollThreshold = static_cast<unsigned int>(viewportLines * Config::SCROLL_THRESHOLD_RATIO);
        scrollThreshold = std::max(scrollThreshold, 1U); // Minimum 1 line threshold

        // Calculate max scroll position (fixed formula)
        unsigned int maxScroll = (totalLines <= viewportLines) ? 1U : (totalLines - viewportLines + 1);

        // Current viewport range
        unsigned int viewStart = m_scrollLine;
        unsigned int viewEnd = viewStart + viewportLines - 1;
        viewEnd = std::min(viewEnd, totalLines);

        // 1. Cursor moved above viewport - scroll up to show cursor
        if (cursorLine < viewStart) {
            m_scrollLine = std::max(1U, cursorLine);
        }
        // 2. Cursor moved below viewport - scroll down to show cursor
        else if (cursorLine > viewEnd) {
            m_scrollLine = std::min(maxScroll, cursorLine);
        }
        // 3. Cursor within viewport - handle edge scrolling
        else {
            // Scroll up if cursor near top edge
            if (cursorLine <= (viewStart + scrollThreshold) && viewStart > 1) {
                m_scrollLine = viewStart - 1;
            }
            // Scroll down if cursor near bottom edge
            else if (cursorLine >= (viewEnd - scrollThreshold) && viewStart < maxScroll) {
                m_scrollLine = viewStart + 1;
            }
        }

        // Ensure scroll position is always valid
        m_scrollLine = std::clamp(m_scrollLine, 1U, maxScroll);
        ImGui::SetScrollY((m_scrollLine - 1) * m_lineHeight);
    }

    // Find next occurrence of text
    bool FindNext(const std::string& findText) {
        if (findText.empty()) return false;
        m_selection.Reset();

        // Search from current cursor position
        size_t pos = m_text.find(findText, m_cursorPos + 1);
        if (pos == std::string::npos) pos = m_text.find(findText, 0); // Wrap around

        if (pos != std::string::npos) {
            // Set cursor to match position and select match
            m_cursorPos = static_cast<unsigned int>(pos);
            m_selection.active = true;
            m_selection.start = m_cursorPos;
            m_selection.end = m_cursorPos + findText.length();
            SyncScroll();
            return true;
        }
        return false;
    }

    // Find previous occurrence of text
    bool FindPrev(const std::string& findText) {
        if (findText.empty()) return false;
        m_selection.Reset();

        // Search backward from current cursor position
        size_t pos = m_text.rfind(findText, m_cursorPos);
        if (pos == std::string::npos || static_cast<unsigned int>(pos) >= m_cursorPos) {
            pos = m_text.rfind(findText, std::string::npos); // Wrap around
        }

        if (pos != std::string::npos) {
            // Set cursor to match position and select match
            m_cursorPos = static_cast<unsigned int>(pos);
            m_selection.active = true;
            m_selection.start = m_cursorPos;
            m_selection.end = m_cursorPos + findText.length();
            SyncScroll();
            return true;
        }
        return false;
    }

    // Get total number of lines in text
    unsigned int GetTotalLines() const {
        if (m_text.empty()) return 1;
        unsigned int lines = std::count(m_text.begin(), m_text.end(), '\n');
        return (m_text.back() == '\n') ? lines : lines + 1;
    }

    // Fixed: Precisely calculate cursor line/column (fixes first line/column display issue)
    void GetCursorLineCol(unsigned int& line, unsigned int& col) const {
        line = 1;
        col = 1;

        // Empty text or cursor at start position (0) - force return line 1, column 1
        if (m_text.empty() || m_cursorPos == 0)
            return;

        // Character-by-character line/column counting - precise calculation
        size_t cursorPos = std::min(static_cast<size_t>(m_cursorPos), m_text.length());
        size_t currentPos = 0;
        line = 1;
        col = 1;

        while (currentPos < cursorPos) {
            if (m_text[currentPos] == '\n') {
                line++;
                col = 1;
            } else {
                col++;
            }
            currentPos++;
        }

        // Final validation: ensure line/column are within valid range
        unsigned int totalLines = GetTotalLines();
        line = std::clamp(line, 1U, totalLines);

        // Validate column number does not exceed current line length
        unsigned int lineStart = GetLineStartPos(line);
        unsigned int lineEnd = GetLineEndPos(line);
        unsigned int lineLength = lineEnd - lineStart;
        col = (lineLength == 0) ? 1 : std::clamp(col, 1U, lineLength);
    }

    // Check if text has unsaved modifications
    bool IsModified() const { return m_isModified; }
    
    // Check if text content differs from saved version
    bool IsContentChanged() const { return m_text != m_lastSavedText; }
    
    // Get current text content
    const std::string& GetText() const { return m_text; }
    
    // Get current selection state
    const Selection& GetSelection() const { return m_selection; }
    
    // Get current scroll line
    unsigned int GetScrollLine() const { return m_scrollLine; }
    
    // Get line height (for rendering)
    float GetLineHeight() const { return m_lineHeight; }
    
    // Set line height (from GUI)
    void SetLineHeight(float height) { m_lineHeight = height; }
    
    // Get number of visible lines (viewport height / line height)
    unsigned int GetVisibleLines() const { return m_visibleLines; }
    
    // Set number of visible lines
    void SetVisibleLines(unsigned int lines) { 
        if (lines != m_visibleLines) {
            m_visibleLines = lines;
            // Update scroll position to adapt to new viewport size
            SyncScroll();
        }
    }

    // Get start position of specified line (public for GUI/input access)
    unsigned int GetLineStartPos(unsigned int line) const {
        unsigned int targetLine = std::clamp(line, 1U, GetTotalLines());
        unsigned int currentLine = 1;
        unsigned int pos = 0;
        size_t len = m_text.length();

        // Iterate to find start of target line
        while (pos < len && currentLine < targetLine) {
            if (m_text[pos] == '\n') {
                currentLine++;
            }
            pos++;
        }
        return pos;
    }

    // Get end position of specified line (public for GUI/input access)
    unsigned int GetLineEndPos(unsigned int line) const {
        unsigned int start = GetLineStartPos(line);
        unsigned int end = start;
        size_t len = m_text.length();
        // Find end of line (newline or end of text)
        while (end < len && m_text[end] != '\n') {
            end++;
        }
        return end;
    }

    size_t GetFileSize() const { return m_fileSize; }

private:
    // Load file content and normalize newlines
    void LoadFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (file.is_open()) {
            file.seekg(0, std::ios::end);
            const std::streampos filePos = file.tellg();
            if (filePos != std::streampos(-1) && filePos >= 0) {
                const uint64_t fileSize64 = static_cast<uint64_t>(filePos);
                if (fileSize64 <= std::numeric_limits<size_t>::max()) {
                    m_fileSize = static_cast<size_t>(fileSize64);
                }
            }
            file.seekg(0, std::ios::beg);

            std::stringstream buffer;
            buffer << file.rdbuf();
            m_text = buffer.str();
            file.close();
            // Normalize CRLF to LF
            NormalizeNewlines(m_text);
        }

        // Ensure trailing newline for consistent line counting
        if (!m_text.empty() && m_text.back() != '\n') {
            m_text.push_back('\n');
        }
    }

    // Push current state to undo stack (trim stack if needed)
    void PushUndoState() {
        // Trim undo stack if exceeding maximum steps
        if (m_undoStack.size() >= Config::MAX_UNDO_STEPS) {
            std::stack<EditState> temp;
            const size_t keep = Config::MAX_UNDO_STEPS - 1;
            while (!m_undoStack.empty()) {
                if (m_undoStack.size() > keep) {
                    m_undoStack.pop();
                } else {
                    temp.push(m_undoStack.top());
                    m_undoStack.pop();
                }
            }
            // Restore trimmed stack
            while (!temp.empty()) {
                m_undoStack.push(temp.top());
                temp.pop();
            }
        }

        // Push current state to undo stack
        m_undoStack.emplace(m_text, m_cursorPos, m_isModified, (m_text == m_lastSavedText));
        // Clear redo stack (new edits invalidate redo history)
        while (!m_redoStack.empty()) m_redoStack.pop();
    }

    // Normalize newlines (convert CRLF to LF)
    void NormalizeNewlines(std::string& text) {
        size_t pos = 0;
        while ((pos = text.find("\r\n", pos)) != std::string::npos) {
            text.replace(pos, 2, "\n");
            pos += 1;
        }
    }

    // Core state variables (declaration order matches initialization list)
    std::string m_text;                          // Current text content
    std::string m_lastSavedText;                 // Last saved text content
    std::string m_filePath;                      // Current file path
    size_t m_fileSize;                           // Current file size
    bool m_isModified;                           // Modification flag
    unsigned int m_cursorPos;                    // Current cursor position (character index)
    unsigned int m_scrollLine;                   // Current scroll line
    unsigned int m_visibleLines;                 // Number of visible lines in viewport
    float m_lineHeight;                          // Line height (for rendering)

    // Find state variables
    unsigned int m_findPos;                      // Current find position
    bool m_findActive;                           // Find mode active flag

    // Selection state
    Selection m_selection;                       // Current selection state

    // Undo/Redo stacks
    std::stack<EditState> m_undoStack;           // Undo history stack
    std::stack<EditState> m_redoStack;           // Redo history stack
};

// ========== Module 3: Editor Manager (Singleton, Lifecycle/Global State Management) ==========
class TextEditorManager {
public:
    // Singleton instance access
    static TextEditorManager& GetInstance() {
        static TextEditorManager instance;
        return instance;
    }

    // Initialize editor with specified file
    void Initialize(const std::string& filePath) {
        // Clean up previous instance
        Shutdown();
        // Create new core editor instance
        m_core = std::make_unique<TextEditorCore>(filePath);
        m_filePath = filePath;
        // Reset manager state
        m_confirmExit = false;
        m_isKeyboardPopup = false;
        m_firstLoad = true;
        g_keyInputHandler.Reset();

        // Set initial status message
        SetStatus(strings[cfg.lang][Lang::TextEditorStatusFileOpened] + filePath, false);
        UpdateStatus();
    }

    // Shutdown editor and clean up resources
    void Shutdown() {
        m_core.reset();
        m_filePath.clear();
        m_statusMessage.clear();
        m_customStatus = false;
        m_confirmExit = false;
        m_isKeyboardPopup = false;
        g_keyInputHandler.Reset();
    }

    // Set status bar message (custom/temporary or permanent)
    void SetStatus(const std::string& msg, bool custom) {
        m_statusMessage = msg;
        m_customStatus = custom;
        if (custom) {
            // Set timeout for temporary messages
            m_statusTimeout = std::chrono::steady_clock::now() + std::chrono::seconds(Config::STATUS_TIMEOUT_SEC);
        }
    }

    // Update status message
    void UpdateStatus(bool custom = false) {
        if (!m_core) return;

        // Get current cursor position for status
        unsigned int line, col;
        m_core->GetCursorLineCol(line, col);
        unsigned int totalLines = m_core->GetTotalLines();
        unsigned int viewEnd = std::min(m_core->GetScrollLine() + m_core->GetVisibleLines() - 1, totalLines);

        // Get file size
        std::string sizeStr;
        size_t bytes = m_core->GetFileSize();
        if (bytes == 0)
            sizeStr = std::format("{} B", 0);
        // Define units: B, KB, MB, GB (up to 1024^3)
        const char* units[] = {"B", "KB", "MB", "GB"};
        int unitIndex = 0;
        double size = static_cast<double>(bytes);
        // Convert to larger units until size is less than 1024
        while (size >= 1024.0 && unitIndex < 3) {
            size /= 1024.0;
            unitIndex++;
        }
        sizeStr = std::format("{:.2f} {}", size, units[unitIndex]);

        // Build default status message
        std::string status = std::format(
            "{}{} {}{} | {}{}-{} | {}{} | {}{} | {}{}",
            strings[cfg.lang][Lang::TextEditorStatusLine], line,
            strings[cfg.lang][Lang::TextEditorStatusCol], col,
            strings[cfg.lang][Lang::TextEditorStatusView], m_core->GetScrollLine(), viewEnd,
            strings[cfg.lang][Lang::TextEditorStatusModified], (m_core->IsModified() ? strings[cfg.lang][Lang::CommonYes] : strings[cfg.lang][Lang::CommonNo]),
            strings[cfg.lang][Lang::TextEditorStatusSelect], (m_core->GetSelection().active ? strings[cfg.lang][Lang::CommonOn] : strings[cfg.lang][Lang::CommonOff]),
            strings[cfg.lang][Lang::TextEditorStatusFileSize], sizeStr);

        // Set permanent status message
        SetStatus(status, custom);
    }

    // Check if status message timeout has expired (restore default status)
    void CheckStatusTimeout() {
        if (m_customStatus && std::chrono::steady_clock::now() > m_statusTimeout) {
            UpdateStatus(true);
            m_customStatus = false;
        }
    }

    // Get current status message
    const std::string& GetStatus() const { return m_statusMessage; }
    
    // Check if status message is custom (temporary)
    bool IsCustomStatus() const { return m_customStatus; }
    
    // Check if keyboard popup is active
    bool IsKeyboardPopup() const { return m_isKeyboardPopup; }
    
    // Set keyboard popup state
    void SetKeyboardPopup(bool val) { m_isKeyboardPopup = val; }

    // Check if this is the first load frame
    bool IsFirstLoad() const { return m_firstLoad; }
    
    // Set first load flag
    void SetFirstLoad(bool val) { m_firstLoad = val; }
    
    // Check if exit confirmation is pending
    bool IsConfirmExit() const { return m_confirmExit; }
    
    // Set exit confirmation flag
    void SetConfirmExit(bool val) { m_confirmExit = val; }
    
    // Get core editor instance
    TextEditorCore* GetCore() { return m_core.get(); }
    
    // Get current file path
    const std::string& GetFilePath() const { return m_filePath; }
    
    // Check if editor is active (initialized)
    bool IsActive() const { return m_core != nullptr; }

private:
    // Private constructor/destructor (singleton pattern)
    TextEditorManager() = default;
    ~TextEditorManager() = default;
    
    // Disable copy/move (singleton pattern)
    TextEditorManager(const TextEditorManager&) = delete;
    TextEditorManager& operator=(const TextEditorManager&) = delete;

    // Manager state variables
    std::unique_ptr<TextEditorCore> m_core;                  // Core editor instance
    std::string m_filePath;                                  // Current file path
    std::string m_statusMessage;                             // Current status message
    bool m_customStatus = false;                             // Custom status flag
    std::chrono::steady_clock::time_point m_statusTimeout;   // Status message timeout
    bool m_confirmExit = false;                              // Exit confirmation flag
    bool m_isKeyboardPopup = false;                          // Keyboard popup active flag
    bool m_firstLoad = true;                                 // First load frame flag
};

// ========== Module 4: Input Adapter (Maps Input to Editor Operations) ==========
namespace TextEditorInput {
    // Handle L button selection logic
    void HandleLSelection(TextEditorCore* core, TextEditorManager& manager) {
        bool lDown = g_keyInputHandler.IsKeyDown(HidNpadButton_L);
        bool lHeld = g_keyInputHandler.IsKeyHeld(HidNpadButton_L);

        // Activate/deactivate selection mode on L button press
        if (!lHeld && lDown) {
            if (!core->GetSelection().active) {
                core->ActivateSelection();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOn], true);
            } else {
                core->DeactivateSelection();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOff], true);
            }
        }

        // Extend selection while L button is held
        if (core->GetSelection().active && lHeld) {
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Left, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Left);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Right, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Right);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Up, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Up);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Down, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Down);
            });
        }
    }

    // Handle A button (virtual keyboard activation)
    bool HandleAKey(TextEditorCore* core, TextEditorManager& manager) {
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_A) && !manager.IsKeyboardPopup()) {
            // Deactivate selection and show keyboard
            core->DeactivateSelection();
            manager.SetKeyboardPopup(true);

            // Get current line content for editing
            unsigned int line, col;
            core->GetCursorLineCol(line, col);
            unsigned int lineStart = core->GetLineStartPos(line);
            unsigned int lineEnd = core->GetLineEndPos(line);
            std::string initialText = core->GetText().substr(lineStart, lineEnd - lineStart);

            // Show virtual keyboard and get input
            std::string input = Keyboard::GetText(strings[cfg.lang][Lang::TextEditorEditLine] + std::to_string(line), initialText);
            manager.SetKeyboardPopup(false);

            // Update line content if input is valid
            if (!input.empty() && input != strings[cfg.lang][Lang::KeyboardEmpty]) {
                core->ReplaceCurrentLine(line, input);
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorUpdatedLine] + std::to_string(line), true);
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorCancelEdit], true);
            }
            return true;
        }
        return false;
    }

    // Handle Minus button (editor exit - exit only, no delete logic)
    bool HandleMinusKey(TextEditorCore* core, TextEditorManager& manager) {
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_Minus)) {
            // Deactivate selection on L button release (if not extended)
            core->DeactivateSelection();
            bool needExit = false;

            // Confirm exit if there are unsaved changes
            if (core->IsModified()) {
                if (manager.IsConfirmExit()) {
                    needExit = true;
                } else {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusChangesUnsaved], true);
                    manager.SetConfirmExit(true);
                    return true;
                }
            } else {
                needExit = true;
            }

            // Exit to file browser if confirmed
            if (needExit) {
                manager.Shutdown();
                data.state = WINDOW_STATE_FILEBROWSER;
                return true;
            }
        }
        return false;
    }

    // Handle Plus button (save file)
    void HandlePlusKey(TextEditorCore* core, TextEditorManager& manager) {
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_Plus)) {
            // Deactivate selection on L button release (if not extended)
            core->DeactivateSelection();
            if (core->IsContentChanged()) {
                bool success = core->Save();
                if (success) {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSaved] + manager.GetFilePath(), false);
                } else {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSaveFailed], true);
                }
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusNoChangesToSave], false);
            }
        }
    }

    // Handle X/Y buttons (copy/paste)
    void HandleCopyPaste(TextEditorCore* core, TextEditorManager& manager) {
        static std::string clipboard;

        // Copy selected text (X button)
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_X)) {
            core->CopySelectedText(clipboard);
            if (!clipboard.empty()) {
                std::string displayText = (clipboard.length() > 20) ? clipboard.substr(0, 20) + "..." : clipboard;
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusCopiedCharacters] + displayText, true);
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusNoTextToCopy], true);
            }
            core->DeactivateSelection();
        }

        // Paste text from clipboard (Y button)
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_Y)) {
            core->DeactivateSelection();
            if (!clipboard.empty()) {
                core->PasteText(clipboard);
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusPasted], true);
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusClipboardEmpty], true);
            }
        }
    }

    // Handle ZL/ZR buttons (undo/redo)
    void HandleUndoRedo(TextEditorCore* core, TextEditorManager& manager) {
        // Undo (ZL button)
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_ZL)) {
            core->DeactivateSelection();
            if (core->Undo()) {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusUndoSuccessful], true);
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusNothingToUndo], true);
            }
        }

        // Redo (ZR button)
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_ZR)) {
            core->DeactivateSelection();
            if (core->Redo()) {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusRedoSuccessful], true);
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusNothingToRedo], true);
            }
        }
    }

    // Handle R button (find operations)
    void HandleFind(TextEditorCore* core, TextEditorManager& manager) {
        static std::string findText;
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_R)) {
            //Open find dialog or use selected text to search (R button)
            findText.clear();
            if (core->GetSelection().IsValid()) {
                core->CopySelectedText(findText);
            } else {
                findText = Keyboard::GetText(strings[cfg.lang][Lang::TextEditorStatusFindText], "");
            }
            if (!findText.empty()) {
                if (core->FindNext(findText)) {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + findText + "\"", true);
                } else {
                    manager.SetStatus("\"" + findText + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
                }
            }
        }
    }

    // Handle direction keys (cursor movement)
    void HandleDirectionKeys(TextEditorCore* core) {
        if (!g_keyInputHandler.IsKeyHeld(HidNpadButton_L)) {
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Up, [&]() {
                core->DeactivateSelection();
                core->MoveCursor(KeyInputHandler::Direction::Up);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Down, [&]() {
                core->DeactivateSelection();
                core->MoveCursor(KeyInputHandler::Direction::Down);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Left, [&]() {
                core->DeactivateSelection();
                core->MoveCursor(KeyInputHandler::Direction::Left);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Right, [&]() {
                core->DeactivateSelection();
                core->MoveCursor(KeyInputHandler::Direction::Right);
            });
        }
    }

    // Main input handler (process all input events)
    void HandleInput(u64& key) {
        auto& manager = TextEditorManager::GetInstance();
        if (!manager.IsActive()) return;

        auto* core = manager.GetCore();
        ImGuiIO& io = ImGui::GetIO();

        // Update input state (uses unified key state)
        g_keyInputHandler.Update();

        if (manager.IsFirstLoad()) {
            manager.SetFirstLoad(false);
            return;
        }

        if (manager.IsKeyboardPopup()) {
            return;
        }

        HandleLSelection(core, manager);

        // Process all input logic (unchanged, but uses unified key state)
        if (HandleAKey(core, manager)) {
            key = 0; // Clear key to prevent main interface processing
            return;
        }

        if (HandleMinusKey(core, manager)) {
            key = 0;
            return;
        }

        if (g_keyInputHandler.ShouldDelete()) {
            core->DeactivateSelection();
            if (core->GetSelection().IsValid()) {
                core->DeleteSelectedText();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedSelectedText], true);
            } else {
                core->DeleteBackward();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedPreCharacter], true);
            }

            // Block main interface from processing B key
            io.KeysDown[ImGuiKey_GamepadBack] = false;
            io.WantCaptureKeyboard = true;
            key &= ~static_cast<u64>(HidNpadButton_B);
        }

        HandlePlusKey(core, manager);
        HandleCopyPaste(core, manager);
        HandleUndoRedo(core, manager);
        HandleFind(core, manager);
        HandleDirectionKeys(core);

        manager.CheckStatusTimeout();
        if (!manager.IsCustomStatus()) {
            manager.UpdateStatus();
        }

        // Clear key state to prevent cross-interface conflict
        key = 0;
    }
}

// ========== Module 5: GUI Renderer (ImGui Only) ==========
namespace TextEditorGUI {
    // Fixed: Precisely render cursor position (matches actual position exactly)
    void RenderTextLine(TextEditorCore* core, const std::string& line, unsigned int lineNum, float lineHeight) {
        unsigned int cursorLine, cursorCol;
        core->GetCursorLineCol(cursorLine, cursorCol);

        // 1. Render line number (fixed width area)
        ImGui::TextColored(
            (lineNum == cursorLine) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "%6d", lineNum
        );
        ImGui::SameLine(Config::LINE_NUMBER_OFFSET);  // End of line number area

        // 2. Render selection highlight
        auto& selection = core->GetSelection();
        if (selection.active) {
            unsigned int selStart, selEnd;
            if (selection.GetNormalizedRange(selStart, selEnd)) {
                unsigned int lineStart = core->GetLineStartPos(lineNum);
                unsigned int lineEnd = core->GetLineEndPos(lineNum);

                unsigned int drawStart = std::max(selStart, lineStart);
                unsigned int drawEnd = std::min(selEnd, lineEnd);

                if (drawStart < drawEnd) {
                    ImVec2 linePos = ImGui::GetCursorScreenPos();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    // Calculate selection highlight position (exclude line number area)
                    float xStart = linePos.x + Config::TEXT_PADDING_X + ImGui::CalcTextSize(line.substr(0, drawStart - lineStart).c_str()).x;
                    float xEnd = linePos.x + Config::TEXT_PADDING_X + ImGui::CalcTextSize(line.substr(0, drawEnd - lineStart).c_str()).x;
                    float yStart = linePos.y + Config::TEXT_PADDING_Y;
                    float yEnd = linePos.y + lineHeight - Config::TEXT_PADDING_Y;

                    // Draw selection highlight
                    drawList->AddRectFilled(
                        ImVec2(xStart, yStart),
                        ImVec2(xEnd, yEnd),
                        ImColor(0x00, 0x66, 0xCC, 0xAA)
                    );
                }
            }
        }

        // 3. Render line text (with padding)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Config::TEXT_PADDING_X);
        ImGui::Text("%s", line.c_str());

        // 4. Render cursor (only for current line - exact coordinate calculation)
        if (lineNum == cursorLine) {
            // Get current line screen position (base position)
            ImVec2 currentLinePos = ImGui::GetCursorScreenPos();
            // Calculate cursor column (corrected to start from 0)
            unsigned int renderCol = std::max(cursorCol - 1, 0U);
            renderCol = std::min(renderCol, static_cast<unsigned int>(line.length()));

            // Precisely calculate cursor X position:
            // - Window left offset + line number area width + text padding + character width offset
            float textOffsetX = ImGui::CalcTextSize(line.substr(0, renderCol).c_str()).x;
            float cursorX = ImGui::GetWindowPos().x + Config::LINE_NUMBER_OFFSET + Config::TEXT_PADDING_X + textOffsetX;

            // Precisely calculate cursor Y position:
            // - Current line top position (subtract line height since ImGui::Text moves cursor down)
            float cursorY = currentLinePos.y - lineHeight + Config::TEXT_PADDING_Y;
            float cursorHeight = lineHeight - 2 * Config::TEXT_PADDING_Y;

            // Cursor blink animation
            static float cursorFlash = 0.0f;
            cursorFlash += ImGui::GetIO().DeltaTime * Config::CURSOR_FLASH_SPEED;
            float alpha = std::sin(cursorFlash) > 0 ? 1.0f : 0.3f;

            // Draw cursor (vertical line) - ensure rendered on top
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                ImVec2(cursorX, cursorY),
                ImVec2(cursorX + Config::CURSOR_WIDTH, cursorY + cursorHeight),
                ImColor(0.1f, 0.7f, 0.6f, alpha)
            );
        }
    }

    // Render scrollable text content area (fixed last lines display issue)
    void RenderTextContent(TextEditorCore* core) {
        float lineHeight = ImGui::GetTextLineHeight();
        core->SetLineHeight(lineHeight);

        // Calculate visible lines with proper floor calculation + 1 extra lines to prevent cutoff
        float availableHeight = ImGui::GetContentRegionAvail().y;
        unsigned int visibleLines = static_cast<unsigned int>(std::floor(availableHeight / lineHeight));
        visibleLines = std::max(visibleLines, 1U);
        core->SetVisibleLines(visibleLines);

        unsigned int totalLines = core->GetTotalLines();
        unsigned int startLine = core->GetScrollLine();
        // Correct end line calculation to avoid overflow and ensure last lines are included
        unsigned int endLine = std::min(startLine + visibleLines - 1, totalLines);

        // Reset text stream to ensure starting from correct position
        std::string text = core->GetText();
        size_t pos = 0;
        size_t len = text.length();
        unsigned int currentLine = 1;

        // Position to start line (robust handling for large files)
        while (currentLine < startLine && pos < len) {
            if (text[pos] == '\n') {
                currentLine++;
            }
            pos++;
        }

        // Render visible lines (fixed: handle partial last line and empty text case properly)
        while (currentLine <= endLine) {
            // Extract current line content (handle end of text correctly)
            size_t lineEnd = text.find('\n', pos);
            if (lineEnd == std::string::npos) {
                lineEnd = len;
            }
            std::string line = text.substr(pos, lineEnd - pos);

            // Render current line (pass line height for precise calculation)
            RenderTextLine(core, line, currentLine, lineHeight);

            // Move to next line (prevent infinite loop at end of text)
            if (lineEnd >= len) {
                break;
            }
            pos = lineEnd + 1;
            currentLine++;
        }

        // Handle empty text case (ensure first line is rendered)
        if (totalLines == 1 && text.empty()) {
            RenderTextLine(core, "", 1, lineHeight);
        }
    }

    // Main GUI renderer
    void Render() {
        auto& manager = TextEditorManager::GetInstance();
        if (!manager.IsActive()) return;

        // Get core instance
        TextEditorCore* core = manager.GetCore();

        // Set editor window position and size (fixed fullscreen)
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(Config::EDITOR_WINDOW_WIDTH, Config::EDITOR_WINDOW_HEIGHT), ImGuiCond_Once);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

        // Create main editor window
        if (ImGui::Begin("TextEditor", nullptr, 
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar)) {

            // Calculate heights for each section
            const float controlHintHeight = ImGui::GetTextLineHeightWithSpacing() + 2 * ImGui::GetStyle().FramePadding.y;
            const float separatorHeight = 1.0f; // ImGui::Separator() height
            const float statusBarHeight = Config::STATUS_BAR_HEIGHT;

            // Top control hint area
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", strings[cfg.lang][Lang::TextEditorControls]);

            // First separator
            ImGui::Separator();

            // Calculate text area height: total height - control hint - 2 separators - status bar
            float textAreaHeight = Config::EDITOR_WINDOW_HEIGHT - controlHintHeight - 2 * separatorHeight - statusBarHeight;

            // Text editing area with fixed size
            ImVec2 textSize = ImVec2(Config::EDITOR_WINDOW_WIDTH, textAreaHeight);
            ImGui::BeginChild("TextScrollView", textSize, true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Config::TEXT_PADDING_X, Config::TEXT_PADDING_Y));
            RenderTextContent(core);
            ImGui::PopStyleVar();
            ImGui::EndChild();

            // Second separator
            ImGui::Separator();

            // Status bar with fixed height
            ImVec2 statusBarSize = ImVec2(Config::EDITOR_WINDOW_WIDTH, statusBarHeight);
            ImGui::BeginChild("##StatusBar", statusBarSize, false, ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", manager.GetStatus().c_str());
            ImGui::EndChild();
        }

        // Cleanup ImGui state
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

// ========== Public API (Backward Compatibility) ==========
namespace TextEditor {
    // Initialize editor with specified file
    void Initialize(const std::string& path) {
        TextEditorManager::GetInstance().Initialize(path);
        ImGui_ImplSwitch_ResetKeyState(HidNpadButton_A);
        ImGui_ImplSwitch_UpdateGamepads();
    }

    // Shutdown editor
    void Shutdown() {
        TextEditorManager::GetInstance().Shutdown();
    }

    // Process editor input
    void HandleInput(u64& key) {
        TextEditorInput::HandleInput(key);
    }

    // Set status bar message
    void SetStatus(const std::string& msg, bool custom) {
        TextEditorManager::GetInstance().SetStatus(msg, custom);
    }

    // Check if editor is active
    bool IsActive() {
        return TextEditorManager::GetInstance().IsActive();
    }
}

// Windows namespace integration (for main application)
namespace Windows {
    // Render text editor window
    void TextEditor() {
        TextEditorGUI::Render();
    }
}