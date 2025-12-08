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
    KeyInputHandler() : m_lastKeyState(0), m_currentKeyState(0), m_lastPressedDir(Direction::None),
                        m_deleteHoldCount(0), m_lastDeleteTime(0) {
        // Initialize direction repeat counters and single-click flags
        for (int i = 0; i < static_cast<int>(Direction::None); ++i) {
            m_dirRepeatCounts[i] = 0;
            m_dirSingleClicked[i] = false;
        }
    }

    // Update input state from gamepad
    void Update() {
        m_handledKeys.clear();
        m_lastKeyState = m_currentKeyState;
        m_currentKeyState = padGetButtons(ImGui_ImplSwitch_GetBackendPadState());
        UpdateKeyHoldCounters();
        
        // Update delete key hold counter - only handle B button
        if (IsKeyCurrentlyHeld(HidNpadButton_B)) {
            m_deleteHoldCount++;
        } else {
            m_deleteHoldCount = 0;
            m_lastDeleteTime = 0;
        }
    }

    // Clear key state completely (prevents repeat triggers)
    void ClearKeyCompletely(HidNpadButton key) {
        u64 keyVal = static_cast<u64>(key);
        m_currentKeyState &= ~keyVal;
        m_lastKeyState &= ~keyVal;
        MarkKeyAsHandled(key);
    }

    // Mark key as handled (skip further processing)
    void MarkKeyAsHandled(HidNpadButton key) { m_handledKeys.insert(key); }
    
    // Check if key is marked as handled
    bool IsKeyHandled(HidNpadButton key) const { return m_handledKeys.count(key) > 0; }

    // Detect edge-triggered key down event (only true on first press frame)
    bool IsKeyDown(HidNpadButton key) const {
        if (IsKeyHandled(key)) return false;
        u64 keyVal = static_cast<u64>(key);
        return (m_currentKeyState & keyVal) && !(m_lastKeyState & keyVal);
    }

    // Detect edge-triggered key release event
    bool IsKeyReleased(HidNpadButton key) const {
        u64 keyVal = static_cast<u64>(key);
        return !(m_currentKeyState & keyVal) && (m_lastKeyState & keyVal);
    }

    // Detect level-triggered long key press
    bool IsKeyHeld(HidNpadButton key) const {
        auto it = m_keyHoldCounts.find(key);
        return it != m_keyHoldCounts.end() && it->second >= Config::HOLD_THRESHOLD;
    }

    // Detect level-triggered current key press
    bool IsKeyCurrentlyHeld(HidNpadButton key) const {
        return m_currentKeyState & static_cast<u64>(key);
    }

    // Detect combo: long hold + trigger key press
    bool IsHeldKeyCombo(HidNpadButton holdKey, HidNpadButton triggerKey) const {
        return IsKeyHeld(holdKey) && IsKeyDown(triggerKey);
    }

    // Handle direction key repeat logic
    void HandleDirectionRepeat(Direction dir, const std::function<void()>& onTrigger) {
        if (!onTrigger || dir == Direction::None) return;

        unsigned int& count = m_dirRepeatCounts[static_cast<int>(dir)];
        bool& isSingleClicked = m_dirSingleClicked[static_cast<int>(dir)];

        // Map direction enum to physical gamepad key
        HidNpadButton dirKey;
        switch (dir) {
            case Direction::Up: dirKey = HidNpadButton_Up; break;
            case Direction::Down: dirKey = HidNpadButton_Down; break;
            case Direction::Left: dirKey = HidNpadButton_Left; break;
            case Direction::Right: dirKey = HidNpadButton_Right; break;
            default: return;
        }

        bool isKeyHeld = IsKeyCurrentlyHeld(dirKey);
        if (!isKeyHeld) {
            count = 0;
            isSingleClicked = false;
            if (m_lastPressedDir == dir) m_lastPressedDir = Direction::None;
            return;
        }

        // Track active direction to prevent cross-interference
        if (m_lastPressedDir != dir) {
            if (m_lastPressedDir != Direction::None) {
                m_dirRepeatCounts[static_cast<int>(m_lastPressedDir)] = 0;
                m_dirSingleClicked[static_cast<int>(m_lastPressedDir)] = false;
            }
            m_lastPressedDir = dir;
        }

        // Increment repeat counter
        count++;
        if (count < Config::SINGLE_CLICK_FRAMES) return;

        // Calculate repeat steps
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

        // Critical fix: limit to 1 step per frame to prevent continuous triggers
        const unsigned int maxStepsPerFrame = 1;
        steps = std::min(steps, maxStepsPerFrame);
        for (size_t i = 0; i < steps; ++i) {
            onTrigger();
        }
    }

    // Handle delete key logic (only B button: single press = 1 char, long press = constant acceleration delete)
    bool ShouldDelete() {
        // Only detect B button
        bool bPressed = IsKeyDown(HidNpadButton_B);
        bool deleteKeyHeld = IsKeyCurrentlyHeld(HidNpadButton_B);

        // Single press (immediate delete)
        if (bPressed) {
            m_lastDeleteTime = m_deleteHoldCount;
            return true;
        }

        // Long press acceleration logic - constant speed
        if (deleteKeyHeld && m_deleteHoldCount > Config::DELETE_INITIAL_DELAY) {
            unsigned int timeSinceLastDelete = m_deleteHoldCount - m_lastDeleteTime;
            
            // Calculate acceleration factor (constant speed)
            unsigned int accelerationFactor = std::min(m_deleteHoldCount / 30, Config::DELETE_MAX_INTERVAL);
            unsigned int requiredInterval = Config::DELETE_REPEAT_INTERVAL - accelerationFactor;
            requiredInterval = std::max(requiredInterval, 1U); // Ensure minimum interval of 1
            
            if (timeSinceLastDelete >= requiredInterval) {
                m_lastDeleteTime = m_deleteHoldCount;
                return true;
            }
        }

        return false;
    }

    // Reset all input states (call on editor shutdown)
    void Reset() {
        m_keyHoldCounts.clear();
        m_lastKeyState = 0;
        m_currentKeyState = 0;
        m_lastPressedDir = Direction::None;
        m_handledKeys.clear();
        m_deleteHoldCount = 0;
        m_lastDeleteTime = 0;
        for (int i = 0; i < static_cast<int>(Direction::None); ++i) {
            m_dirRepeatCounts[i] = 0;
            m_dirSingleClicked[i] = false;
        }
    }

private:
    // Update hold duration counters for tracked keys
    void UpdateKeyHoldCounters() {
        const std::vector<HidNpadButton> trackedKeys = {
            HidNpadButton_L, HidNpadButton_R, HidNpadButton_A, HidNpadButton_B,
            HidNpadButton_X, HidNpadButton_Y, HidNpadButton_Plus, HidNpadButton_Minus,
            HidNpadButton_Left, HidNpadButton_Right, HidNpadButton_Up, HidNpadButton_Down
        };

        for (auto key : trackedKeys) {
            bool isHeld = IsKeyCurrentlyHeld(key);
            m_keyHoldCounts[key] = isHeld ? (m_keyHoldCounts[key] + 1) : 0;
        }
    }

    // Input state variables
    u64 m_lastKeyState = 0;                                  // Previous frame key state
    u64 m_currentKeyState = 0;                              // Current frame key state
    std::unordered_map<HidNpadButton, unsigned int> m_keyHoldCounts; // Key hold duration counters
    unsigned int m_dirRepeatCounts[static_cast<int>(Direction::None)]; // Direction repeat counters
    bool m_dirSingleClicked[static_cast<int>(Direction::None)];       // Direction single-click flags
    Direction m_lastPressedDir;                              // Last pressed direction
    std::unordered_set<HidNpadButton> m_handledKeys;         // Handled keys set
    
    // Delete key specific state
    unsigned int m_deleteHoldCount;                          // Delete key hold duration
    unsigned int m_lastDeleteTime;                           // Last delete execution time
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
        m_filePath(filePath), m_isModified(false), m_overwriteMode(false),
        m_cursorPos(0), m_scrollLine(1), m_visibleLines(20), m_lineHeight(0.0f),
        m_findPos(0), m_findActive(false), m_capsLock(false),
        m_firstLPressInSelect(true), m_hasExtendedSelection(false) {

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

        // Apply caps lock if enabled
        if (m_capsLock) {
            std::transform(insertText.begin(), insertText.end(), insertText.begin(), ::toupper);
        }

        // Insert or overwrite text based on mode
        if (m_overwriteMode) {
            size_t len = m_text.length();
            for (char c : insertText) {
                if (m_cursorPos < len) {
                    m_text[m_cursorPos++] = c;
                } else {
                    m_text.push_back(c);
                    m_cursorPos++;
                }
            }
        } else {
            m_text.insert(m_cursorPos, insertText);
            m_cursorPos += insertText.length();
        }

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
                    SetCursorPosition(line - 1, col);
                } else {
                    m_cursorPos = 0;
                }
                break;
            }
            case KeyInputHandler::Direction::Down: {
                unsigned int line, col;
                GetCursorLineCol(line, col);
                unsigned int totalLines = GetTotalLines();
                if (line < totalLines) {
                    SetCursorPosition(line + 1, col);
                } else {
                    m_cursorPos = m_text.length();
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

        // Limit column to valid range (1 to lineLength + 1)
        targetCol = std::clamp(targetCol, 1U, lineLength + 1);

        // Calculate final cursor position
        m_cursorPos = lineStart + (targetCol - 1);
        m_cursorPos = std::min(m_cursorPos, static_cast<unsigned int>(m_text.length()));
        
        // Update scroll position
        SyncScroll();
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
        
        // Fixed: Explicitly handle cursor moving outside viewport boundaries
        
        // 1. Cursor moved above viewport - scroll up to show cursor
        if (cursorLine < viewStart) {
            m_scrollLine = std::max(1U, cursorLine);
            ImGui::SetScrollY((m_scrollLine - 1) * m_lineHeight);
        }
        // 2. Cursor moved below viewport - scroll down to show cursor
        else if (cursorLine > viewEnd) {
            m_scrollLine = std::min(maxScroll, cursorLine);
            ImGui::SetScrollY((m_scrollLine - 1) * m_lineHeight);
        }
        // 3. Cursor within viewport - handle edge scrolling
        else {
            // Scroll up if cursor near top edge
            if (cursorLine <= (viewStart + scrollThreshold) && viewStart > 1) {
                m_scrollLine = viewStart - 1;
                ImGui::SetScrollY((m_scrollLine - 1) * m_lineHeight);
            }
            // Scroll down if cursor near bottom edge
            else if (cursorLine >= (viewEnd - scrollThreshold) && viewStart < maxScroll) {
                m_scrollLine = viewStart + 1;
                ImGui::SetScrollY((m_scrollLine - 1) * m_lineHeight);
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
        if (m_text.empty() || m_cursorPos == 0) {
            line = 1;
            col = 1;
            return;
        }

        // Safety check: prevent out of bounds
        size_t cursorPos = std::min(static_cast<size_t>(m_cursorPos), m_text.length());
        
        // Character-by-character line/column counting - precise calculation
        size_t currentPos = 0;
        line = 1;
        col = 1;
        
        while (currentPos < cursorPos) {
            if (m_text[currentPos] == '\n') {
                // Newline found - increment line number, reset column to 1
                line++;
                col = 1;
            } else {
                // Regular character - increment column number
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
        col = std::clamp(col, 1U, lineLength + 1);
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
        m_visibleLines = lines; 
        // Update scroll position to adapt to new viewport size
        SyncScroll();
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

    // Reset selection session flags
    void ResetSelectSessionFlags() {
        m_hasExtendedSelection = false;
        m_firstLPressInSelect = true;
    }

    // Check if this is the first L button press in selection mode
    bool IsFirstLPressInSelect() const { return m_firstLPressInSelect; }
    
    // Set first L press flag
    void SetFirstLPressInSelect(bool val) { m_firstLPressInSelect = val; }
    
    // Check if selection has been extended
    bool HasExtendedSelection() const { return m_hasExtendedSelection; }
    
    // Set extended selection flag
    void SetExtendedSelection(bool val) { m_hasExtendedSelection = val; }

private:
    // Load file content and normalize newlines
    void LoadFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::binary);
        if (file.is_open()) {
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
    bool m_isModified;                           // Modification flag
    bool m_overwriteMode;                        // Overwrite/insert mode flag
    unsigned int m_cursorPos;                    // Current cursor position (character index)
    unsigned int m_scrollLine;                   // Current scroll line
    unsigned int m_visibleLines;                 // Number of visible lines in viewport
    float m_lineHeight;                          // Line height (for rendering)

    // Find state variables
    unsigned int m_findPos;                      // Current find position
    bool m_findActive;                           // Find mode active flag
    bool m_capsLock;                             // Caps lock state

    // Selection state
    Selection m_selection;                       // Current selection state

    // Undo/Redo stacks
    std::stack<EditState> m_undoStack;           // Undo history stack
    std::stack<EditState> m_redoStack;           // Redo history stack

    // Selection session flags
    bool m_firstLPressInSelect;                  // First L button press flag
    bool m_hasExtendedSelection;                 // Selection extended flag
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
        m_ignoreNextA = true;
        m_isKeyboardPopup = false;
        m_firstLoad = true;
        g_keyInputHandler.Reset();

        // Set initial status message
        SetStatus(strings[cfg.lang][Lang::TextEditorStatusFileOpened] + filePath, false);
    }

    // Shutdown editor and clean up resources
    void Shutdown() {
        m_core.reset();
        m_filePath.clear();
        m_statusMessage.clear();
        m_customStatus = false;
        m_confirmExit = false;
        m_ignoreNextA = false;
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

    // Check if status message timeout has expired (restore default status)
    void CheckStatusTimeout() {
        if (m_customStatus && std::chrono::steady_clock::now() > m_statusTimeout) {
            if (m_core) {
                // Get current cursor position for status
                unsigned int line, col;
                m_core->GetCursorLineCol(line, col);
                unsigned int totalLines = m_core->GetTotalLines();
                unsigned int viewEnd = std::min(m_core->GetScrollLine() + m_core->GetVisibleLines() - 1, totalLines);
                
                // Build default status message
                std::string status = strings[cfg.lang][Lang::TextEditorStatusLine] + std::to_string(line) +
                                    strings[cfg.lang][Lang::TextEditorStatusCol] + std::to_string(col) +
                                    strings[cfg.lang][Lang::TextEditorStatusView] + std::to_string(m_core->GetScrollLine()) + "-" + std::to_string(viewEnd) +
                                    strings[cfg.lang][Lang::TextEditorStatusModified] + (m_core->IsModified() ? strings[cfg.lang][Lang::CommonYes] : strings[cfg.lang][Lang::CommonNo]) +
                                    strings[cfg.lang][Lang::TextEditorStatusMode] + (m_core->IsModified() ? strings[cfg.lang][Lang::CommonOverwrite] : strings[cfg.lang][Lang::CommonInsert]) +
                                    strings[cfg.lang][Lang::TextEditorStatusSelect] + (m_core->GetSelection().active ? strings[cfg.lang][Lang::CommonOn] : strings[cfg.lang][Lang::CommonOff]) +
                                    strings[cfg.lang][Lang::TextEditorStatusCaps] + (false ? strings[cfg.lang][Lang::CommonOn] : strings[cfg.lang][Lang::CommonOff]);
                
                // Set permanent status message
                SetStatus(status, false);
            }
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
    
    // Check if next A button press should be ignored
    bool IgnoreNextA() const { return m_ignoreNextA; }
    
    // Set ignore next A button press flag
    void SetIgnoreNextA(bool val) { m_ignoreNextA = val; }
    
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
    bool m_ignoreNextA = false;                              // Ignore next A button press flag
};

// ========== Module 4: Input Adapter (Maps Input to Editor Operations) ==========
namespace TextEditorInput {
    // Handle L button selection logic
    void HandleLSelection(TextEditorCore* core, TextEditorManager& manager) {
        bool lDown = g_keyInputHandler.IsKeyDown(HidNpadButton_L);
        bool lHeld = g_keyInputHandler.IsKeyCurrentlyHeld(HidNpadButton_L);
        bool lReleased = g_keyInputHandler.IsKeyReleased(HidNpadButton_L);
        bool findActive = false;

        // Activate selection mode on L button press
        if (lDown && !core->GetSelection().active && !findActive) {
            core->ActivateSelection();
            core->ResetSelectSessionFlags();
            manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOn], true);
        }

        // Deactivate selection mode on second L button press
        if (lDown && core->GetSelection().active && !findActive) {
            if (!core->IsFirstLPressInSelect()) {
                core->DeactivateSelection();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOff], true);
                core->ResetSelectSessionFlags();
            }
            core->SetFirstLPressInSelect(false);
        }

        // Extend selection while L button is held
        if (core->GetSelection().active && lHeld && !findActive) {
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Left, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Left);
                core->SetExtendedSelection(true);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Right, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Right);
                core->SetExtendedSelection(true);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Up, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Up);
                core->SetExtendedSelection(true);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Down, [&]() {
                core->ExtendSelection(KeyInputHandler::Direction::Down);
                core->SetExtendedSelection(true);
            });
        }

        // Deactivate selection on L button release (if not extended)
        if (lReleased && core->GetSelection().active && !findActive) {
            if (!core->HasExtendedSelection()) {
                core->DeactivateSelection();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSelectModeOff], true);
                core->ResetSelectSessionFlags();
            }
            core->SetFirstLPressInSelect(true);
        }

        // Reset first L press flag if released and no selection
        if (lReleased && !core->GetSelection().active) {
            core->SetFirstLPressInSelect(true);
        }
    }

    // Handle A button (virtual keyboard activation)
    bool HandleAKey(TextEditorCore* core, TextEditorManager& manager) {
        if (g_keyInputHandler.IsKeyDown(HidNpadButton_A) && !manager.IsKeyboardPopup()) {
            // Ignore first A press (prevents auto-popup on editor open)
            if (manager.IgnoreNextA()) {
                manager.SetIgnoreNextA(false);
                return true;
            }

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

        // Find next (R + Right)
        if (g_keyInputHandler.IsHeldKeyCombo(HidNpadButton_R, HidNpadButton_Right)) {
            core->DeactivateSelection();
            if (!findText.empty()) {
                if (core->FindNext(findText)) {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + findText + "\"", true);
                } else {
                    manager.SetStatus("\"" + findText + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
                }
            }
        } 
        // Find previous (R + Left)
        else if (g_keyInputHandler.IsHeldKeyCombo(HidNpadButton_R, HidNpadButton_Left)) {
            core->DeactivateSelection();
            if (!findText.empty()) {
                if (core->FindPrev(findText)) {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + findText + "\"", true);
                } else {
                    manager.SetStatus("\"" + findText + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
                }
            }
        } 
        // Open find dialog (R button)
        else if (g_keyInputHandler.IsKeyDown(HidNpadButton_R)) {
            core->DeactivateSelection();
            std::string input = Keyboard::GetText(strings[cfg.lang][Lang::TextEditorStatusFindText], findText);
            if (!input.empty()) {
                findText = input;
                if (core->FindNext(findText)) {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusTextFound] + input + "\"", true);
                } else {
                    manager.SetStatus("\"" + input + strings[cfg.lang][Lang::TextEditorStatusTextNotFound], true);
                }
            }
        }
    }

    // Handle direction keys (cursor movement)
    void HandleDirectionKeys(TextEditorCore* core) {
        bool lHeld = g_keyInputHandler.IsKeyCurrentlyHeld(HidNpadButton_L);
        // Only move cursor if not in selection mode
        if (!core->GetSelection().active && !lHeld) {
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Up, [&]() {
                core->MoveCursor(KeyInputHandler::Direction::Up);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Down, [&]() {
                core->MoveCursor(KeyInputHandler::Direction::Down);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Left, [&]() {
                core->MoveCursor(KeyInputHandler::Direction::Left);
            });
            g_keyInputHandler.HandleDirectionRepeat(KeyInputHandler::Direction::Right, [&]() {
                core->MoveCursor(KeyInputHandler::Direction::Right);
            });
        }
    }

    // Fixed: Mouse click position calculation (exclude line number area)
    void HandleMouseClick(const ImVec2& clickPos, const ImVec2& scrollPos, float lineHeight) {
        auto& manager = TextEditorManager::GetInstance();
        if (!manager.IsActive()) return;

        auto* core = manager.GetCore();
        core->DeactivateSelection();

        // 1. Calculate clicked line number (based on scroll position and line height)
        float clickYRelative = clickPos.y - scrollPos.y - Config::TEXT_PADDING_Y;
        unsigned int clickLine = core->GetScrollLine() + static_cast<unsigned int>(clickYRelative / lineHeight);
        unsigned int totalLines = core->GetTotalLines();
        clickLine = std::clamp(clickLine, 1U, totalLines);

        // 2. Calculate clicked column number (exclude line number area)
        float clickXRelative = clickPos.x - ImGui::GetWindowPos().x - Config::LINE_NUMBER_OFFSET - Config::TEXT_PADDING_X;
        float charWidth = ImGui::CalcTextSize(" ").x;
        unsigned int clickCol = clickXRelative > 0 ? static_cast<unsigned int>(clickXRelative / charWidth) + 1 : 1;

        // 3. Limit column to valid range for current line
        unsigned int lineStart = core->GetLineStartPos(clickLine);
        unsigned int lineEnd = core->GetLineEndPos(clickLine);
        unsigned int lineLength = lineEnd - lineStart;
        clickCol = std::clamp(clickCol, 1U, lineLength + 1);

        // 4. Set cursor position
        core->SetCursorPosition(clickLine, clickCol);
        core->SyncScroll();

        // Update status message with new cursor position
        unsigned int line, col;
        core->GetCursorLineCol(line, col);
        manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusLine] + std::to_string(line) + "," + std::to_string(col), false);
    }

    // Main input handler (process all input events)
    void HandleInput(u64& key) {
        auto& manager = TextEditorManager::GetInstance();
        if (!manager.IsActive()) return;

        auto* core = manager.GetCore();
        ImGuiIO& io = ImGui::GetIO();

        // Update gamepad input state
        ImGui_ImplSwitch_UpdateGamepads();
        g_keyInputHandler.Update();

        // Skip first load frame (prevent accidental input)
        if (manager.IsFirstLoad()) {
            manager.SetFirstLoad(false);
            return;
        }

        // Block input while keyboard popup is active
        if (manager.IsKeyboardPopup()) return;

        // Process selection input
        HandleLSelection(core, manager);

        // Process A button (keyboard)
        if (HandleAKey(core, manager)) return;

        // Process Minus button (exit only)
        if (HandleMinusKey(core, manager)) return;

        // Process Delete logic (only B button)
        if (g_keyInputHandler.ShouldDelete()) {
            core->DeactivateSelection();
            
            // Delete selected text or single character
            if (core->GetSelection().IsValid()) {
                core->DeleteSelectedText();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedSelectedText], true);
            } else {
                core->DeleteBackward();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedPreCharacter], true);
            }

            // Block ImGui default back button behavior for B key
            if (g_keyInputHandler.IsKeyCurrentlyHeld(HidNpadButton_B)) {
                io.KeysDown[ImGuiKey_GamepadBack] = false;
                io.WantCaptureKeyboard = true;
                g_keyInputHandler.MarkKeyAsHandled(HidNpadButton_B);
                key &= ~static_cast<u64>(HidNpadButton_B);
            }
        }

        // Process other function buttons
        HandlePlusKey(core, manager);       // Save
        HandleCopyPaste(core, manager);     // Copy/Paste
        HandleUndoRedo(core, manager);      // Undo/Redo
        HandleFind(core, manager);          // Find
        HandleDirectionKeys(core);          // Cursor movement

        // Update status message timeout
        manager.CheckStatusTimeout();
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
        
        // Fix: Calculate visible lines with proper floor calculation + 2 extra lines to prevent cutoff
        float availableHeight = ImGui::GetContentRegionAvail().y;
        unsigned int visibleLines = static_cast<unsigned int>(floor(availableHeight / lineHeight)) + 2;
        core->SetVisibleLines(visibleLines);

        unsigned int totalLines = core->GetTotalLines();
        unsigned int startLine = core->GetScrollLine();
        // Fix: Correct end line calculation to avoid overflow and ensure last lines are included
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

        // Handle mouse click input (fixed position calculation)
        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered()) {
            ImVec2 clickPos = ImGui::GetMousePos();
            ImVec2 scrollPos = ImVec2(ImGui::GetScrollX(), ImGui::GetScrollY());
            // Correct click position with window padding (fixed Y offset calculation)
            clickPos.y -= ImGui::GetWindowPos().y + ImGui::GetFrameHeightWithSpacing() + Config::TEXT_PADDING_Y;
            TextEditorInput::HandleMouseClick(clickPos, scrollPos, lineHeight);
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

            // Render controls hint (multi-language)
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", strings[cfg.lang][Lang::TextEditorControls]);

            // Separator between controls and text area
            ImGui::Separator();
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            contentSize.y -= Config::STATUS_BAR_HEIGHT + 2; // Small adjustment to prevent status bar cutoff

            // Create scrollable text viewport
            ImGui::BeginChild("TextScrollView", contentSize, true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Config::TEXT_PADDING_X, Config::TEXT_PADDING_Y));
            
            // Force initial scroll position to 0 (top)
            if (core->GetScrollLine() == 1) {
                ImGui::SetScrollY(0.0f);
            }
            
            RenderTextContent(core);
            ImGui::PopStyleVar();
            ImGui::EndChild();

            // Render status bar
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", manager.GetStatus().c_str());
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