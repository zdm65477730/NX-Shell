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
#include "fs.hpp"
#include "keyboard.hpp"
#include "language.hpp"
#include "config.hpp"
#include "log.hpp"

// Global configuration constants (centralized management)
namespace Config {
    // Editor constants
    constexpr size_t MAX_UNDO_STEPS = 50;                       // Maximum number of undo steps
    constexpr unsigned int STATUS_TIMEOUT_SEC = 3;              // Status message timeout in seconds
    constexpr float EDITOR_WINDOW_WIDTH = 1280.0f;              // Editor window width
    constexpr float EDITOR_WINDOW_HEIGHT = 720.0f;              // Editor window height
    constexpr float STATUS_BAR_HEIGHT = 40.0f;                  // Status bar height
    constexpr unsigned int TEXT_LINES_COMP = 5;                 // Lines of text to compensate for partial visibility
    constexpr float LINE_NUMBER_OFFSET = 75.0f;                 // Width of line number area (prevents cursor overlap)
    constexpr float CURSOR_WIDTH = 2.0f;                        // Cursor width in pixels
    constexpr float CURSOR_FLASH_SPEED = 6.0f;                  // Cursor blink animation speed
    constexpr float TEXT_PADDING_X = 8.0f;                      // Horizontal padding for text area
    constexpr float TEXT_PADDING_Y = 2.0f;                      // Vertical padding for text area

    // Input handler constants
    constexpr unsigned int HOLD_THRESHOLD = 15;                 // Increased threshold for long key press (slower initial repeat)
    constexpr unsigned int REPEAT_THRESHOLD = 50;               // Threshold for key repeat activation
    constexpr unsigned int REPEAT_STEP_INTERVAL = 15;           // Faster repeat interval for delete (15ms per step)
    constexpr unsigned int REPEAT_MAX_EXTRA = 3;                // Maximum extra repeat steps (faster acceleration)
    constexpr unsigned int SINGLE_CLICK_FRAMES = 2;             // Frames to detect single click
    constexpr unsigned int DELETE_INITIAL_DELAY = 10;           // Reduced initial delay (from 20 to 10)
    constexpr unsigned int DELETE_REPEAT_INTERVAL = 5;          // Reduced repeat interval (from 10 to 5) for faster delete
    constexpr unsigned int DELETE_MAX_INTERVAL = 3;             // Maximum acceleration level (constant speed)

    constexpr size_t MAX_FILE_SIZE = 768 * 1024;                // Maximum file size: 768 KB
}

namespace UTF8Utils {
    inline size_t GetUTF8CharLength(char c) {
        if ((c & 0x80) == 0) return 1;
        if ((c & 0xE0) == 0xC0) return 2;
        if ((c & 0xF0) == 0xE0) return 3;
        if ((c & 0xF8) == 0xF0) return 4;
        return 1;
    }

    inline size_t ByteToCharIndex(const std::string& str, size_t bytePos) {
        size_t charIndex = 0;
        size_t i = 0;
        bytePos = std::min(bytePos, str.size());
        while (i < bytePos) {
            size_t len = GetUTF8CharLength(str[i]);
            i += len;
            charIndex++;
        }
        return charIndex;
    }

    inline size_t CharToByteIndex(const std::string& str, size_t charIndex) {
        size_t bytePos = 0;
        size_t currentChar = 0;
        while (bytePos < str.size() && currentChar < charIndex) {
            size_t len = GetUTF8CharLength(str[bytePos]);
            bytePos += len;
            currentChar++;
        }
        return bytePos;
    }

    inline size_t GetUTF8CharCount(const std::string& str) {
        return ByteToCharIndex(str, str.size());
    }

    inline std::string GetUTF8Substr(const std::string& str, size_t charStart, size_t charCount) {
        size_t byteStart = CharToByteIndex(str, charStart);
        size_t byteEnd = CharToByteIndex(str, charStart + charCount);
        return str.substr(byteStart, byteEnd - byteStart);
    }

    inline size_t GetPrevCharBytePos(const std::string& str, size_t bytePos) {
        if (bytePos == 0) return 0;
        bytePos--;
        while (bytePos > 0 && (str[bytePos] & 0xC0) == 0x80) {
            bytePos--;
        }
        return bytePos;
    }

    inline size_t GetNextCharBytePos(const std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return str.size();
        size_t len = GetUTF8CharLength(str[bytePos]);
        return bytePos + len;
    }

    inline size_t AlignToCharBoundary(const std::string& str, size_t bytePos) {
        if (bytePos == 0 || bytePos >= str.size()) return bytePos;
        if ((str[bytePos] & 0xC0) != 0x80) {
            return bytePos;
        }
        size_t alignPos = bytePos;
        while (alignPos > 0 && (str[alignPos] & 0xC0) == 0x80) {
            alignPos--;
        }
        return alignPos;
    }

    inline void DeleteUTF8Char(std::string& str, size_t bytePos) {
        if (bytePos >= str.size()) return;
        bytePos = AlignToCharBoundary(str, bytePos);
        size_t len = GetUTF8CharLength(str[bytePos]);
        str.erase(bytePos, len);
    }

    inline void DeleteUTF8Range(std::string& str, size_t startByte, size_t endByte) {
        if (startByte >= endByte || startByte >= str.size()) return;
        startByte = AlignToCharBoundary(str, startByte);
        endByte = AlignToCharBoundary(str, endByte);
        endByte = std::min(endByte, str.size());
        str.erase(startByte, endByte - startByte);
    }
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

class TextEditorManager;

// ========== Module 2: Core Editor Logic (Pure Text Processing, Decoupled from GUI/Input) ==========
class TextEditorCore {
public:
    struct LinePosition {
        size_t start;
        size_t end;
        LinePosition(size_t s = 0, size_t e = 0) : start(s), end(e) {}
    };

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

    struct LineCache {
        std::string content;
        unsigned int startPos;
        size_t byteLength;
        size_t charCount;
        float totalWidth;
        std::vector<float> charWidths;
        std::vector<size_t> charByteOffsets;
        std::vector<size_t> charToByte;  // char index -> byte position in line
        std::vector<size_t> byteToChar;  // byte position in line -> char index
    };

    // Constructor: Initialize editor with file content
    TextEditorCore(const std::string& filePath) : 
        m_filePath(filePath), m_fileSize(0), m_isModified(false),
        m_cursorPos(0), m_scrollLine(1), m_visibleLines(1),
        m_lineHeight(0.0f), m_lineSpacing(0.0f), m_cacheValid(false),
        m_scrollOffset(0.0f), m_linePositionsValid(false), m_scrollOffsetX(0.0f),
        m_viewportWidth(0.0f), m_usableTextWidth(0.0f) {
        // Load file and normalize newlines (CRLF -> LF)
        LoadFile(filePath);

        m_lastSavedText = m_text;

        // Initialize undo stack with initial state
        m_undoStack.emplace(m_text, m_cursorPos, false, true);
        UpdateLinePositions();
        UpdateLineCache();
    }

    // Insert text at current cursor position
    void InsertText(const std::string& text) {
        if (m_selection.IsValid()) {
            DeleteSelectedText();
        }

        PushUndoState();
        std::string insertText = text;
        size_t insertPos = UTF8Utils::AlignToCharBoundary(m_text, m_cursorPos);
        m_text.insert(insertPos, insertText);
        m_cursorPos = static_cast<unsigned int>(insertPos + insertText.length());
        m_isModified = true;
        InvalidateLineCache();
        SyncScroll();
    }

    // Delete character before cursor (single deletion)
    void DeleteBackward() {
        if (m_cursorPos == 0) return;

        PushUndoState();

        size_t prevCharPos = UTF8Utils::GetPrevCharBytePos(m_text, m_cursorPos);
        size_t deleteLen = m_cursorPos - prevCharPos;
        m_text.erase(prevCharPos, deleteLen);
        m_cursorPos = static_cast<unsigned int>(prevCharPos);
        m_isModified = true;
        InvalidateLineCache();
        SyncScroll();
    }

    // Delete selected text
    void DeleteSelectedText() {
        unsigned int start, end;
        if (!m_selection.GetNormalizedRange(start, end)) return;

        PushUndoState();

        size_t startByte = UTF8Utils::AlignToCharBoundary(m_text, start);
        size_t endByte = UTF8Utils::AlignToCharBoundary(m_text, end);
        endByte = std::min(endByte, static_cast<size_t>(m_text.length()));
        m_text.erase(startByte, endByte - startByte);
        m_cursorPos = static_cast<unsigned int>(startByte);
        m_selection.Reset();
        m_isModified = true;
        InvalidateLineCache();
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
        InvalidateLineCache();
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
        InvalidateLineCache();
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

        LinePosition linePos = GetLinePosition(lineNum);
        size_t lineStart = linePos.start;
        size_t lineEnd = linePos.end;
        std::string currentContent = m_text.substr(lineStart, lineEnd - lineStart);

        if (currentContent == newContent) return;

        PushUndoState();
        UTF8Utils::DeleteUTF8Range(m_text, lineStart, lineEnd);
        size_t insertPos = UTF8Utils::AlignToCharBoundary(m_text, lineStart);
        m_text.insert(insertPos, newContent);
        size_t newLineEnd = insertPos + newContent.length();
        m_cursorPos = static_cast<unsigned int>(std::min(newLineEnd, static_cast<size_t>(m_text.length())));
        m_isModified = true;
        InvalidateLineCache();
        SyncScroll();
    }

    // Move cursor in specified direction
    void MoveCursor(KeyInputHandler::Direction dir) {
        switch (dir) {
            case KeyInputHandler::Direction::Up: {
                unsigned int currentLine, currentCol;
                GetCursorLineCol(currentLine, currentCol);
                if (currentLine > 1) {
                    size_t currentLineCharIndex = currentCol - 1;
                    unsigned int targetLine = currentLine - 1;
                    LinePosition targetLinePos = GetLinePosition(targetLine);
                    std::string targetLineContent = m_text.substr(targetLinePos.start, targetLinePos.end - targetLinePos.start);
                    size_t targetLineCharCount = UTF8Utils::GetUTF8CharCount(targetLineContent);
                    size_t targetCharIndex = std::min(currentLineCharIndex, targetLineCharCount);
                    unsigned int targetCol = static_cast<unsigned int>(targetCharIndex) + 1;
                    SetCursorPosition(targetLine, targetCol);
                }
                break;
            }
            case KeyInputHandler::Direction::Down: {
                unsigned int currentLine, currentCol;
                GetCursorLineCol(currentLine, currentCol);
                unsigned int totalLines = GetTotalLines();
                if (currentLine < totalLines) {
                    size_t currentLineCharIndex = currentCol - 1;
                    unsigned int targetLine = currentLine + 1;
                    LinePosition targetLinePos = GetLinePosition(targetLine);
                    std::string targetLineContent = m_text.substr(targetLinePos.start, targetLinePos.end - targetLinePos.start);
                    size_t targetLineCharCount = UTF8Utils::GetUTF8CharCount(targetLineContent);
                    size_t targetCharIndex = std::min(currentLineCharIndex, targetLineCharCount);
                    unsigned int targetCol = static_cast<unsigned int>(targetCharIndex) + 1;
                    SetCursorPosition(targetLine, targetCol);
                }
                break;
            }
            case KeyInputHandler::Direction::Left: {
                if (m_cursorPos == 0) break;
                size_t newBytePos = UTF8Utils::GetPrevCharBytePos(m_text, m_cursorPos);
                m_cursorPos = static_cast<unsigned int>(newBytePos);
                break;
            }
            case KeyInputHandler::Direction::Right: {
                if (m_cursorPos >= m_text.size()) break;
                size_t newBytePos = UTF8Utils::GetNextCharBytePos(m_text, m_cursorPos);
                m_cursorPos = static_cast<unsigned int>(newBytePos);
                break;
            }
            default: break;
        }
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
    void DeactivateSelection() { m_selection.Reset(); }

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

        LinePosition linePos = GetLinePosition(targetLine);
        size_t lineStart = linePos.start;
        size_t lineEnd = linePos.end;
        std::string lineContent = m_text.substr(lineStart, lineEnd - lineStart);

        size_t targetCharIndex = static_cast<size_t>(std::max(targetCol - 1, 0U));
        size_t targetByteInLine = 0;
        if (m_cacheValid && targetLine <= m_lineCache.size()) {
            const auto& lineCache = m_lineCache[targetLine - 1];
            targetCharIndex = std::clamp(targetCharIndex, 0UL, lineCache.charCount);
            targetByteInLine = lineCache.charToByte[targetCharIndex];
        } else {
            targetByteInLine = UTF8Utils::CharToByteIndex(lineContent, targetCharIndex);
        }
        size_t targetBytePos = lineStart + targetByteInLine;
        targetBytePos = UTF8Utils::AlignToCharBoundary(m_text, targetBytePos);
        m_cursorPos = static_cast<unsigned int>(std::clamp(targetBytePos, 0UL, static_cast<size_t>(m_text.length())));
    }

    unsigned int GetCursorPosition() const { return m_cursorPos; }

    // Horizontal Scroll Methods
    void SetViewportWidth(float width) {
        // Only update when width changes (avoid redundant calculation)
        if (m_viewportWidth != width) {
            m_viewportWidth = width;

            // Calculate usable text width: viewport width - line number area - total horizontal padding
            // LineNumberOffset: fixed width of line number area; TEXT_PADDING_X*2: left + right padding
            m_usableTextWidth = m_viewportWidth - Config::LINE_NUMBER_OFFSET - (Config::TEXT_PADDING_X * 2);
            // Ensure width is non-negative (prevent calculation errors)
            m_usableTextWidth = std::max(m_usableTextWidth, 0.0f);

            // Sync horizontal scroll state after width update
            SyncHorizontalScroll();
        }
    }

    float GetScrollOffsetX() const { return m_scrollOffsetX; }

    float GetCursorHorizontalPos(unsigned int cursorLine, unsigned int cursorCol) const {
        const auto& lineCache = GetLineCache();
        // Boundary check: return 0 for invalid line number
        if (cursorLine < 1 || cursorLine > lineCache.size()) {
            return 0.0f;
        }
        const auto& line = lineCache[cursorLine - 1];
        // Force column number to be within valid range (1 ~ character count + 1)
        cursorCol = std::clamp(cursorCol, 1U, static_cast<unsigned int>(line.charCount) + 1);
        size_t charIndex = static_cast<size_t>(cursorCol - 1);
        charIndex = std::clamp(charIndex, 0UL, line.charWidths.size() - 1);
        // Force cursor position to be between 0 and total line width (inclusive)
        float cursorX = line.charWidths[charIndex];
        cursorX = std::clamp(cursorX, 0.0f, line.totalWidth);
        return cursorX;
    }

    // Horizontal Scroll Synchronization: Ensure cursor is always in text display area
    void SyncHorizontalScroll() {
        unsigned int cursorLine, cursorCol;
        GetCursorLineCol(cursorLine, cursorCol);
        const auto& lineCache = GetLineCache();

        // Step 1: Boundary check for invalid line (reset scroll offset)
        if (cursorLine < 1 || cursorLine > lineCache.size()) {
            m_scrollOffsetX = 0.0f;
            return;
        }

        const auto& currentLine = lineCache[cursorLine - 1];
        // Step 2: Get absolute cursor position (calibrated to 0 ~ lineTotalWidth)
        float cursorX = GetCursorHorizontalPos(cursorLine, cursorCol);
        float lineTotalWidth = currentLine.totalWidth;
        float usableWidth = m_usableTextWidth;

        // Step 3: Guard against invalid usable width (reset scroll offset)
        if (usableWidth <= 0) {
            m_scrollOffsetX = 0.0f;
            return;
        }

        // Step 4: Configuration (adjust padding to your visual preference)
        const float visiblePadding = 8.0f; // 8px padding from edges (can be 0 for hard edge alignment)
        float maxScrollX = std::max(0.0f, lineTotalWidth - usableWidth); // Max right scroll offset

        // Step 5: Hard constraint logic (core fix for extremely long text)
        // Calculate current visible area bounds
        float viewLeft = m_scrollOffsetX;
        float viewRight = m_scrollOffsetX + usableWidth;

        // Case 1: Cursor is LEFT of the visible area (with padding) → Pull scroll offset left to show cursor
        if (cursorX < viewLeft + visiblePadding) {
            m_scrollOffsetX = cursorX - visiblePadding;
        }
        // Case 2: Cursor is RIGHT of the visible area (with padding) → Push scroll offset right to show cursor
        else if (cursorX > viewRight - visiblePadding) {
            m_scrollOffsetX = cursorX - (usableWidth - visiblePadding);
        }
        // Case 3: Cursor is inside the visible area → Do NOT change scroll offset (no unnecessary jitter)

        // Step 6: Strictly clamp scroll offset to valid range [0, maxScrollX] (critical for edge cases)
        m_scrollOffsetX = std::clamp(m_scrollOffsetX, 0.0f, maxScrollX);

        // Step 7: Final override for short lines (line width < usable width → force scroll to 0)
        if (lineTotalWidth < usableWidth) {
            m_scrollOffsetX = 0.0f;
        }

        // ==============================================
        // Emergency fallback: 100% guarantee cursor is visible (for extreme edge cases)
        // ==============================================
        // Recalculate visible area after all adjustments
        float finalViewLeft = m_scrollOffsetX;
        float finalViewRight = m_scrollOffsetX + usableWidth;

        // If cursor is STILL left of visible area → Force scroll offset to cursorX (no padding)
        if (cursorX < finalViewLeft) {
            m_scrollOffsetX = std::clamp(cursorX, 0.0f, maxScrollX);
        }
        // If cursor is STILL right of visible area → Force scroll offset to cursorX - usableWidth (no padding)
        else if (cursorX > finalViewRight) {
            m_scrollOffsetX = std::clamp(cursorX - usableWidth, 0.0f, maxScrollX);
        }
    }

    // Scroll logic (FIXED: Fix last lines truncation and viewEnd mismatch)
    void SyncScroll() {
        unsigned int cursorLine, cursorCol;
        GetCursorLineCol(cursorLine, cursorCol);
        unsigned int totalLines = GetTotalLines();

        // Early return if no lines (avoid division by zero)
        if (totalLines == 0) {
            m_scrollLine = 1;
            m_scrollOffset = 0.0f;
            SyncHorizontalScroll();
            return;
        }

        // Vertical scroll logic (use accurate visible lines count)
        unsigned int viewportLines = m_visibleLines;
        viewportLines = std::max(viewportLines, 1U); // Fallback to 1 line if not set

        // ---- Critical Fix 1: Relax max scroll line calculation (allow partial lines at the end) ----
        // Original formula: maxScrollLine = totalLines - viewportLines + 1 (causes truncation)
        // New logic: Allow scrollLine to go up to totalLines (endLine will clamp to totalLines)
        // This fixes the last 5 lines not showing up
        unsigned int maxScrollLine = totalLines; 

        // ---- Critical Fix 2: Adjust scroll line based on cursor position (with last line handling) ----
        unsigned int currentEndLine = m_scrollLine + viewportLines - 1;
        // Clamp currentEndLine to totalLines (actual visible end line)
        currentEndLine = std::min(currentEndLine, totalLines);

        // Case 1: Cursor is above the visible area (scroll up to cursor)
        if (cursorLine < m_scrollLine) {
            m_scrollLine = std::clamp(cursorLine, 1U, maxScrollLine);
        }
        // Case 2: Cursor is below the visible area (scroll down to fit cursor)
        else if (cursorLine > currentEndLine) {
            // Calculate new scroll line to place cursor at the bottom of the viewport
            unsigned int newScrollLine = cursorLine - viewportLines + 1;
            // Clamp new scroll line (allow it to go up to totalLines for last lines)
            m_scrollLine = std::clamp(newScrollLine, 1U, maxScrollLine);
        }
        // Case 3: Cursor is inside visible area (no unnecessary scroll)
        else {
            // Keep scroll line unchanged to avoid jitter
        }

        // Final safety clamp (eliminate any out-of-bounds)
        m_scrollLine = std::clamp(m_scrollLine, 1U, maxScrollLine);

        // ---- Critical Fix 3: Recalculate actual end line (clamp to totalLines) ----
        // This fixes viewEnd being larger than actual displayed lines
        unsigned int actualEndLine = m_scrollLine + viewportLines - 1;
        actualEndLine = std::min(actualEndLine, totalLines);
        // Update scroll offset (exact pixel position for the actual start line)
        // For last lines: scroll offset is (m_scrollLine - 1) * m_lineSpacing (even if partial lines)
        m_scrollOffset = static_cast<float>(m_scrollLine - 1) * m_lineSpacing;

        // Sync horizontal scroll after vertical scroll
        SyncHorizontalScroll();
    }

    // Find next occurrence of text
    bool FindNext(const std::string& findText) {
        if (findText.empty()) return false;
        m_selection.Reset();
        size_t findCharCount = UTF8Utils::GetUTF8CharCount(findText);
        if (findCharCount == 0) return false;

        size_t pos = m_text.find(findText, m_cursorPos + 1);
        if (pos == std::string::npos) {
            pos = m_text.find(findText, 0);
        }

        if (pos != std::string::npos) {
            pos = UTF8Utils::AlignToCharBoundary(m_text, pos);
            m_cursorPos = static_cast<unsigned int>(pos);
            m_selection.active = true;
            m_selection.start = m_cursorPos;
            m_selection.end = static_cast<unsigned int>(pos + findText.length());
            SyncScroll();
            return true;
        }
        return false;
    }

    bool FindPrev(const std::string& findText) {
        if (findText.empty()) return false;
        m_selection.Reset();

        size_t findCharCount = UTF8Utils::GetUTF8CharCount(findText);
        if (findCharCount == 0) return false;

        size_t pos = m_text.rfind(findText, m_cursorPos);
        if (pos == std::string::npos || static_cast<unsigned int>(pos) >= m_cursorPos) {
            pos = m_text.rfind(findText, std::string::npos);
        }

        if (pos != std::string::npos) {
            pos = UTF8Utils::AlignToCharBoundary(m_text, pos);
            m_cursorPos = static_cast<unsigned int>(pos);
            m_selection.active = true;
            m_selection.start = m_cursorPos;
            m_selection.end = static_cast<unsigned int>(pos + findText.length());
            SyncScroll();
            return true;
        }
        return false;
    }

    // Get total number of lines in text
    unsigned int GetTotalLines() const {
        if (!m_linePositionsValid) {
            return m_text.empty() ? 1 : std::count(m_text.begin(), m_text.end(), '\n') + 1;
        }
        return static_cast<unsigned int>(m_linePositions.size());
    }

    // Precisely calculate cursor line/column (fixes first line/column display issue)
    void GetCursorLineCol(unsigned int& line, unsigned int& col) const {
        line = 1;
        col = 1;

        size_t len = m_text.length();
        size_t cursorBytePos = std::min(static_cast<size_t>(m_cursorPos), len);
        if (len == 0 || cursorBytePos == 0) {
            return;
        }

        line = FindLineByBytePos(cursorBytePos);
        LinePosition linePos = GetLinePosition(line);
        size_t lineStart = linePos.start;
        size_t cursorInLineByte = cursorBytePos - lineStart;
        std::string lineContent = m_text.substr(lineStart, linePos.end - lineStart);

        size_t cursorInLineChar = UTF8Utils::ByteToCharIndex(lineContent, cursorInLineByte);
        col = static_cast<unsigned int>(cursorInLineChar) + 1;

        size_t lineCharCount = UTF8Utils::GetUTF8CharCount(lineContent);
        unsigned int maxCol = static_cast<unsigned int>(lineCharCount) + 1;
        col = std::clamp(col, 1U, maxCol);
        unsigned int totalLines = GetTotalLines();
        line = std::clamp(line, 1U, totalLines);
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

    void SetScrollLine(unsigned int line) {
        m_scrollLine = line;
        SyncScroll();
    }

    // Get line height (for rendering)
    float GetLineHeight() const { return m_lineHeight; }

    void SetLineSpacing(float spacing) { m_lineSpacing = spacing; }

    // Set line height (from GUI)
    void SetLineHeight(float height) {
        m_lineHeight = height;
        SyncScroll();
    }

    // Get total content height (total lines * line spacing) for ImGui scrollbar calculation
    float GetTotalContentHeight() const { return static_cast<float>(GetTotalLines()) * m_lineSpacing; }

    // Get number of visible lines (viewport height / line height)
    unsigned int GetVisibleLines() const { return m_visibleLines; }
    
    // Set number of visible lines
    void SetVisibleLines(unsigned int lines) { 
        m_visibleLines = lines;
        // Update scroll position to adapt to new viewport size
        SyncScroll();
    }

    // Get start position of specified line (public for GUI/input access)
    unsigned int GetLineStartPos(unsigned int line) const { return static_cast<unsigned int>(GetLinePosition(line).start); }

    // Get end position of specified line (public for GUI/input access)
    unsigned int GetLineEndPos(unsigned int line) const { return static_cast<unsigned int>(GetLinePosition(line).end); }

    LinePosition GetLinePosition(unsigned int line) const {
        unsigned int totalLines = GetTotalLines();
        line = std::clamp(line, 1U, totalLines);
        if (!m_linePositionsValid || line > m_linePositions.size()) {
            size_t start = 0;
            size_t end = 0;
            size_t pos = 0;
            unsigned int currentLine = 1;
            size_t len = m_text.length();

            while (pos < len && currentLine < line) {
                if (m_text[pos] == '\n') {
                    currentLine++;
                    start = pos + 1;
                }
                pos++;
            }

            end = start;
            while (end < len && m_text[end] != '\n') {
                end++;
            }

            return LinePosition(start, end);
        }

        return m_linePositions[line - 1];
    }

    void UpdateLinePositions() {
        if (m_linePositionsValid)
            return;

        m_linePositions.clear();
        size_t len = m_text.length();
        size_t start = 0;
        size_t pos = 0;
        while (pos <= len) {
            if (pos == len || m_text[pos] == '\n') {
                m_linePositions.emplace_back(start, pos);
                start = pos + 1;
            }
            pos++;
        }
        m_linePositionsValid = true;
    }

    float GetScrollOffset() const { return m_scrollOffset; }

    void SetScrollOffset(float offset) { m_scrollOffset = offset; }

    // Update LineCache (optimized with line positions cache and incremental update)
    void UpdateLineCache() {
        UpdateLinePositions();
        if (m_cacheValid || m_lineHeight <= 0 || m_linePositions.empty())
            return;

        m_lineCache.clear();
        m_lineCache.reserve(m_linePositions.size());
        float spaceWidth = ImGui::CalcTextSize(" ").x;
        const float tabWidth = 4 * spaceWidth;

        for (const auto& linePos : m_linePositions) {
            LineCache line;
            line.content = m_text.substr(linePos.start, linePos.end - linePos.start);
            line.startPos = static_cast<unsigned int>(linePos.start);
            line.byteLength = line.content.size();
            line.charCount = UTF8Utils::GetUTF8CharCount(line.content);

            line.charToByte.reserve(line.charCount + 1);
            line.byteToChar.reserve(line.byteLength + 1);
            line.charWidths.reserve(line.charCount + 1);
            line.charByteOffsets.reserve(line.charCount + 1);
            line.charToByte.push_back(0);
            line.byteToChar.push_back(0);
            line.charWidths.push_back(0.0f);
            line.charByteOffsets.push_back(0);

            float currentWidth = 0.0f;
            size_t bytePos = 0;
            size_t charIdx = 0;

            while (bytePos < line.byteLength) {
                size_t charLen = UTF8Utils::GetUTF8CharLength(line.content[bytePos]);
                std::string utf8Char = line.content.substr(bytePos, charLen);
                float charW = (utf8Char == "\t") ? tabWidth : ImGui::CalcTextSize(utf8Char.c_str()).x;
                for (size_t i = 0; i < charLen; i++) {
                    line.byteToChar.push_back(charIdx);
                }

                currentWidth += charW;
                charIdx++;
                line.charToByte.push_back(bytePos + charLen);
                line.charByteOffsets.push_back(bytePos + charLen);
                line.charWidths.push_back(currentWidth);

                bytePos += charLen;
            }

            while (line.byteToChar.size() <= line.byteLength) {
                line.byteToChar.push_back(line.charCount);
            }

            line.charToByte.push_back(line.byteLength);
            line.charWidths.push_back(currentWidth);
            line.charByteOffsets.push_back(line.byteLength);
            line.totalWidth = currentWidth;

            m_lineCache.push_back(line);
        }

        m_cacheValid = true;
    }

    const std::vector<LineCache>& GetLineCache() const { return m_lineCache; }

    unsigned int FindLineByBytePos(size_t bytePos) const {
        if (m_linePositions.empty() || !m_linePositionsValid) {
            unsigned int line = 1;
            size_t pos = 0;
            size_t len = m_text.length();

            while (pos < len && pos < bytePos) {
                if (m_text[pos] == '\n') {
                    line++;
                }
                pos++;
            }

            return line;
        }

        unsigned int left = 0;
        unsigned int right = static_cast<unsigned int>(m_linePositions.size()) - 1;
        unsigned int result = right;
        while (left <= right) {
            unsigned int mid = (left + right) / 2;
            const auto& linePos = m_linePositions[mid];
            if (linePos.start <= bytePos) {
                result = mid;
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }

        return result + 1;
    }

    void InvalidateLineCache(bool invalidateLinePositions = true) {
        m_cacheValid = false;
        if (invalidateLinePositions)
            m_linePositionsValid = false;
    }

    size_t GetFileSize() const { return m_fileSize; }

private:
    // Load file content and normalize newlines
    void LoadFile(const std::string& filePath);

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
    float m_lineSpacing;

    // Selection state
    Selection m_selection;                       // Current selection state

    // Undo/Redo stacks
    std::stack<EditState> m_undoStack;           // Undo history stack
    std::stack<EditState> m_redoStack;           // Redo history stack

    std::vector<LineCache> m_lineCache;
    bool m_cacheValid;
    float m_scrollOffset;                        // Scroll offset (in pixels)
    std::vector<LinePosition> m_linePositions;
    bool m_linePositionsValid;

    // Pixel-level horizontal scroll state variables
    float m_scrollOffsetX;                       // Horizontal scroll offset (in pixels)
    float m_viewportWidth;                       // Viewport width (in pixels)
    float m_usableTextWidth;                     // Usable text display width (pixels: viewport width - line number area - padding)
};

// ========== Module 3: Editor Manager (Singleton, Lifecycle/Global State Management) ==========
class TextEditorManager {
public:
    bool IsShowFileTooLargePopup() const { return m_showFileTooLargePopup; }

    void SetShowFileTooLargePopup(bool val) { m_showFileTooLargePopup = val; }

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
    TextEditorCore* GetCore() { 
        if (!m_core) return nullptr;
        return m_core.get();
    }

    // Get current file path
    const std::string& GetFilePath() const { return m_filePath; }
    
    // Check if editor is active (initialized)
    bool IsActive() const { return m_core != nullptr; }

    bool IsApplet() const {
        const auto type = appletGetAppletType();
        return type != AppletType_Application && type != AppletType_SystemApplication;
    }

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
    bool m_showFileTooLargePopup = false;
};

void TextEditorCore::LoadFile(const std::string& filePath) {
     std::ifstream file(filePath, std::ios::binary);
     if (file.is_open()) {
         // Get file size from file stream
         file.seekg(0, std::ios::end);
         const std::streampos filePos = file.tellg();
         if (filePos != std::streampos(-1) && filePos >= 0) {
             const uint64_t fileSize64 = static_cast<uint64_t>(filePos);
             // Convert file size to size_t (ensure it's within the range of size_t)
             if (fileSize64 <= std::numeric_limits<size_t>::max()) {
                 m_fileSize = static_cast<size_t>(fileSize64);
             } else {
                 // Mark file size as exceeding limit if it's larger than size_t can hold
                 m_fileSize = Config::MAX_FILE_SIZE + 1;
             }
         } else {
             // Failed to get file size, close file and return early
             file.close();
             return;
         }

         // Check if file size exceeds the maximum allowed size (MAX_FILE_SIZE)
         if (TextEditorManager::GetInstance().IsApplet() && m_fileSize >= Config::MAX_FILE_SIZE) {
             file.close(); // Close the file handle before exiting
             // Set popup state
             TextEditorManager::GetInstance().SetShowFileTooLargePopup(true);
             return; // Terminate file loading process
         }

         // Proceed to read file content if size is within the limit
         file.seekg(0, std::ios::beg);
         m_text.reserve(m_fileSize);
         char buffer[8192];
         while (file.read(buffer, sizeof(buffer))) {
             m_text.append(buffer, sizeof(buffer));
         }
         m_text.append(buffer, file.gcount());
         file.close();

         // Normalize CRLF to LF (unified newline format)
         NormalizeNewlines(m_text);
     }
 }

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
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSaved] + manager.GetFilePath(), true);
                } else {
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusSaveFailed], true);
                }
            } else {
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusNoChangesToSave], true);
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

        if (manager.IsKeyboardPopup()) {
            key = 0;
            return;
        }

        // Update input state (uses unified key state)
        g_keyInputHandler.Update();

        if (manager.IsFirstLoad()) {
            manager.SetFirstLoad(false);
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
            if (core->GetSelection().IsValid()) {
                core->DeleteSelectedText();
                core->DeactivateSelection();
                manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedSelectedText], true);
            } else {
                if (core->GetCursorPosition() != 0) {
                    core->DeleteBackward();
                    manager.SetStatus(strings[cfg.lang][Lang::TextEditorStatusDeletedPreCharacter], true);
                }
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
    // Render line numbers (independent function: line numbers only)
    void RenderLineNumber(TextEditorCore* core, unsigned int lineNum, float lineHeight, float lineSpacing) {
        unsigned int cursorLine, cursorCol;
        core->GetCursorLineCol(cursorLine, cursorCol);

        // Line number text color: highlight current line
        ImGui::TextColored(
            (lineNum == cursorLine) ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "%6d", lineNum
        );
        // Match line spacing
        //ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (lineSpacing - lineHeight));
        ImGui::Dummy(ImVec2(0.0f, std::max((lineSpacing - lineHeight) * 0.5f, 0.0f)));
    }

    // Render text line content (independent function: text only, includes selection highlight and cursor)
    void RenderTextLineContent(TextEditorCore* core, const std::string& line, unsigned int lineNum, float lineHeight, float lineSpacing, const TextEditorCore::LineCache& lineCache) {
        unsigned int cursorLine, cursorCol;
        core->GetCursorLineCol(cursorLine, cursorCol);

        ImVec2 currentLineScreenPos = ImGui::GetCursorScreenPos();
        ImVec2 currentCursorPos = ImGui::GetCursorPos();

        // Get horizontal scroll offset
        float scrollOffsetX = core->GetScrollOffsetX();

        // Render selection highlight (apply horizontal scroll offset)
        auto& selection = core->GetSelection();
        if (selection.active) {
            unsigned int selStart, selEnd;
            if (selection.GetNormalizedRange(selStart, selEnd)) {
                unsigned int lineStart = lineCache.startPos;
                unsigned int lineEnd = lineStart + static_cast<unsigned int>(lineCache.byteLength);
                unsigned int drawStart = std::max(selStart, lineStart);
                unsigned int drawEnd = std::min(selEnd, lineEnd);
                if (drawStart < drawEnd) {
                    size_t charStart = UTF8Utils::ByteToCharIndex(lineCache.content, drawStart - lineStart);
                    size_t charEnd = UTF8Utils::ByteToCharIndex(lineCache.content, drawEnd - lineStart);
                    charStart = std::clamp(charStart, 0UL, lineCache.charCount);
                    charEnd = std::clamp(charEnd, 0UL, lineCache.charCount);
                    
                    float xStart = currentLineScreenPos.x + Config::TEXT_PADDING_X + lineCache.charWidths[charStart] - scrollOffsetX;
                    float xEnd = currentLineScreenPos.x + Config::TEXT_PADDING_X + lineCache.charWidths[charEnd] - scrollOffsetX;
                    float yStart = currentLineScreenPos.y + Config::TEXT_PADDING_Y;
                    float yEnd = currentLineScreenPos.y + lineHeight - Config::TEXT_PADDING_Y;
                    
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(ImVec2(xStart, yStart), ImVec2(xEnd, yEnd), ImColor(0x00, 0x66, 0xCC, 0xAA));
                }
            }
        }

        // Render line text (apply horizontal scroll offset)
        std::string displayLine = lineCache.content;
        size_t tabPos;
        while ((tabPos = displayLine.find('\t')) != std::string::npos) {
            displayLine.replace(tabPos, 1, "    ");
        }
        ImGui::SetCursorPosX(currentCursorPos.x + Config::TEXT_PADDING_X - scrollOffsetX);
        ImGui::Text("%s", displayLine.c_str());

        // Render cursor (apply horizontal scroll offset)
        if (lineNum == cursorLine) {
            size_t charIndex = static_cast<size_t>(cursorCol - 1);
            charIndex = std::clamp(charIndex, 0UL, lineCache.charCount);
            float textOffsetX = lineCache.charWidths[charIndex];
            textOffsetX = std::min(textOffsetX, lineCache.totalWidth);
            
            float cursorX = currentLineScreenPos.x + Config::TEXT_PADDING_X + textOffsetX - scrollOffsetX;
            float cursorY = currentLineScreenPos.y + Config::TEXT_PADDING_Y;
            float cursorHeight = lineHeight - 2 * Config::TEXT_PADDING_Y;
            
            static float cursorFlash = 0.0f;
            cursorFlash += ImGui::GetIO().DeltaTime * Config::CURSOR_FLASH_SPEED;
            float alpha = std::sin(cursorFlash) > 0 ? 1.0f : 0.3f;
            
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                ImVec2(cursorX, cursorY),
                ImVec2(cursorX + Config::CURSOR_WIDTH, cursorY + cursorHeight),
                ImColor(0.1f, 0.7f, 0.6f, alpha)
            );
        }

        // Match line spacing
        //ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (lineSpacing - lineHeight));
        ImGui::Dummy(ImVec2(0.0f, std::max((lineSpacing - lineHeight) * 0.5f, 0.0f)));
    }

    // Render text content (modified to use optimized LineCache)
    void RenderTextContent(TextEditorCore* core, float lineHeight, float lineSpacing, float textAreaHeight) {
        // Update core settings and line cache before rendering
        core->SetLineHeight(lineHeight);
        core->SetLineSpacing(lineSpacing);
        core->UpdateLinePositions();
        core->UpdateLineCache();

        // Get line cache data and total lines
        const auto& lineCache = core->GetLineCache();
        unsigned int totalLines = static_cast<unsigned int>(lineCache.size());
        unsigned int visibleLines = core->GetVisibleLines();
        visibleLines = std::max(visibleLines, 1U);

        // ========== 1. Line Number Area: Fixed width, vertical scroll only ==========
        const float lineNumWidth = Config::LINE_NUMBER_OFFSET;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        ImGui::BeginChild(
            "LineNumberArea",
            ImVec2(lineNumWidth, textAreaHeight),
            false,
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar
        );

        // Get core's exact scroll offset (fix last lines positioning)
        float scrollOffset = core->GetScrollOffset();
        ImGui::SetScrollY(scrollOffset);
        ImGui::SetCursorPos(ImVec2(0.0f, scrollOffset)); // Align cursor with scroll offset

        // Calculate line range to render (fix viewEnd mismatch)
        unsigned int startLine = core->GetScrollLine();
        // ---- Critical Fix: Calculate actual end line (clamp to totalLines, fix viewEnd being too large) ----
        unsigned int actualEndLine = startLine + visibleLines - 1;
        actualEndLine = std::min(actualEndLine, totalLines);

        // Render only up to actualEndLine (fix viewEnd showing extra lines)
        for (unsigned int lineNum = startLine; lineNum <= actualEndLine; ++lineNum) {
            if (lineNum < 1 || lineNum > totalLines) {
                continue;
            }
            RenderLineNumber(core, lineNum, lineHeight, lineSpacing);
            // Stop rendering if we exceed the text area height (optimization)
            if (ImGui::GetCursorPosY() > textAreaHeight + scrollOffset) {
                break;
            }
        }
        if (totalLines == 0) {
            RenderLineNumber(core, 1, lineHeight, lineSpacing);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(1);

        ImGui::SameLine();

        // ========== 2. Text Area: Remaining width, supports horizontal/vertical scroll ==========
        ImVec2 textAreaSize = ImVec2(ImGui::GetContentRegionAvail().x, textAreaHeight);
        textAreaSize.x = std::max(textAreaSize.x, 10.0f);
        core->SetViewportWidth(textAreaSize.x);

        ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

        // Calculate total content height (for ImGui scrollbar accuracy)
        float totalContentHeight = core->GetTotalContentHeight();
        totalContentHeight = std::max(totalContentHeight, textAreaHeight);

        ImGui::BeginChild(
            "TextScrollView",
            textAreaSize,
            false,
            ImGuiWindowFlags_HorizontalScrollbar
        );

        // Use core's exact scroll offset (fix last lines positioning)
        ImGui::SetScrollY(scrollOffset);
        ImGui::SetCursorPos(ImVec2(0.0f, scrollOffset)); // Align text cursor with scroll offset

        // Render only up to actualEndLine (fix last 5 lines not showing)
        for (unsigned int lineNum = startLine; lineNum <= actualEndLine; ++lineNum) {
            if (lineNum < 1 || lineNum > totalLines) {
                continue;
            }
            const auto& line = lineCache[lineNum - 1];
            RenderTextLineContent(core, line.content, lineNum, lineHeight, lineSpacing, line);
            // Stop rendering if we exceed the text area height (optimization)
            if (ImGui::GetCursorPosY() > textAreaHeight + scrollOffset) {
                break;
            }
        }
        if (totalLines == 0) {
            RenderTextLineContent(core, "", 1, lineHeight, lineSpacing, TextEditorCore::LineCache{});
        }

        // Ensure ImGui recognizes full content height (prevents scrollbar from being too small)
        ImGui::SetCursorPosY(totalContentHeight);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));

        ImGui::EndChild();
        ImGui::PopStyleColor(1);
    }

    // Main GUI renderer (FIXED: Visible lines calculation to fix 5-line offset)
    void Render() {
        auto& manager = TextEditorManager::GetInstance();
        if (!manager.IsActive()) return;

        // Render file too large popup
        if (manager.IsShowFileTooLargePopup()) {
            // Set popup properties: centered, fixed size, modal (block other operations)
            ImGui::OpenPopup("FileTooLargePopup");
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);

            // Draw popup
            if (ImGui::BeginPopupModal(
                "FileTooLargePopup", 
                nullptr, 
                ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
            )) {
                // Show prompt text (centered)
                ImGui::SetCursorPosY((ImGui::GetWindowSize().y - 40) / 2);
                std::string text = strings[cfg.lang][Lang::TextEditorExitPrompt];
                ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
                ImVec2 windowSize = ImGui::GetWindowSize();
                ImGui::SetCursorPosX((windowSize.x - textSize.x) / 2);
                ImGui::Text("%s", text.c_str());

                // Draw confirm button (centered)
                ImVec2 buttonSize(120, 40);
                ImVec2 buttonPos((ImGui::GetWindowSize().x - buttonSize.x) / 2, ImGui::GetWindowSize().y - 50);
                ImGui::SetCursorPos(buttonPos);
                if (ImGui::Button(strings[cfg.lang][Lang::CommonYes], buttonSize)) {
                    // 1. Switch to file browser interface (global state)
                    data.state = WINDOW_STATE_FILEBROWSER;
                    // 2. Shutdown the text editor instance
                    manager.Shutdown();
                    // 3. Close the popup
                    manager.SetShowFileTooLargePopup(false);
                }

                ImGui::EndPopup();
            }
        }

        TextEditorCore* core = manager.GetCore();
        if (!core)
            return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        const float windowWidth = Config::EDITOR_WINDOW_WIDTH;
        const float windowHeight = Config::EDITOR_WINDOW_HEIGHT;
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight), ImGuiCond_Always);

        ImGuiWindowFlags windowFlags = 
            ImGuiWindowFlags_NoMove | 
            ImGuiWindowFlags_NoResize | 
            ImGuiWindowFlags_NoCollapse | 
            ImGuiWindowFlags_NoTitleBar | 
            ImGuiWindowFlags_NoScrollbar | 
            ImGuiWindowFlags_NoScrollWithMouse;

        const float SCREEN_PADDING = 4.0f;
        const float SEPARATOR_HEIGHT = 2.0f;
        const float HINT_PADDING = 8.0f;

        if (ImGui::Begin("TextEditor", nullptr, windowFlags)) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 windowPos = ImGui::GetWindowPos();

            // Get exact ImGui text metrics (no custom offset)
            float lineHeight = ImGui::GetTextLineHeight();
            float lineSpacing = ImGui::GetTextLineHeightWithSpacing();
            lineSpacing = std::max(lineSpacing, lineHeight);

            // Calculate fixed element heights (exact pixel calculation)
            const float hintBarHeight = lineSpacing + 2 * HINT_PADDING;
            const float topTotalHeight = SCREEN_PADDING + hintBarHeight + SEPARATOR_HEIGHT;
            const float bottomTotalHeight = SEPARATOR_HEIGHT + Config::STATUS_BAR_HEIGHT + SCREEN_PADDING;
            const float textAreaHeight = windowHeight - topTotalHeight - bottomTotalHeight;
            const float textAreaHeightClamped = std::max(textAreaHeight, 10.0f);

            // Render top hint bar
            const float hintBarY = SCREEN_PADDING;
            ImGui::SetCursorPos(ImVec2(4.0f, hintBarY + HINT_PADDING));
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", strings[cfg.lang][Lang::TextEditorControls]);

            // Render top separator
            const float sep1Y = SCREEN_PADDING + hintBarHeight;
            drawList->AddRectFilled(
                ImVec2(windowPos.x, windowPos.y + sep1Y),
                ImVec2(windowPos.x + windowWidth, windowPos.y + sep1Y + SEPARATOR_HEIGHT),
                ImColor(0x44, 0x44, 0x44, 0xFF)
            );

            // Render core content
            const float textAreaY = sep1Y + SEPARATOR_HEIGHT;
            ImGui::SetCursorPosY(textAreaY);

            unsigned int visibleLines = static_cast<unsigned int>(textAreaHeightClamped / lineSpacing);
            visibleLines = std::max(visibleLines - Config::TEXT_LINES_COMP, 1U); // Remove the extra lines causing truncation
            core->SetVisibleLines(visibleLines);

            // Render text content (with core fixes)
            RenderTextContent(core, lineHeight, lineSpacing, textAreaHeightClamped);

            // Render bottom separator
            const float sep2Y = textAreaY + textAreaHeightClamped;
            drawList->AddRectFilled(
                ImVec2(windowPos.x, windowPos.y + sep2Y),
                ImVec2(windowPos.x + windowWidth, windowPos.y + sep2Y + SEPARATOR_HEIGHT),
                ImColor(0x44, 0x44, 0x44, 0xFF)
            );

            // Render status bar
            const float statusBarY = sep2Y + SEPARATOR_HEIGHT;
            const float statusBarActualHeight = windowHeight - SCREEN_PADDING - statusBarY;
            ImGui::SetCursorPosY(statusBarY);
            ImGui::BeginChild(
                "##StatusBar", 
                ImVec2(windowWidth, statusBarActualHeight), 
                false,
                ImGuiWindowFlags_NoScrollbar
            );
            ImGui::SetCursorPosX(4.0f);
            ImGui::SetCursorPosY((statusBarActualHeight - lineHeight) / 2.0f);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", manager.GetStatus().c_str());
            ImGui::EndChild();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
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