#include <algorithm>
#include <cstring>

#include "config.hpp"
#include "imgui.h"
#include "popups.hpp"
#include "tabs.hpp"
#include "windows.hpp"
#include "imgui_impl_switch.hpp"

WindowData data;

namespace Windows {
    static bool image_properties = false, file_stat = false;

    void SetupWindow(void) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_Once);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    };

    void ExitWindow(void) {
        ImGui::End();
        ImGui::PopStyleVar();
    };

    void ResetCheckbox(WindowData &data) {
        data.checkbox_data.checked.clear();
        data.checkbox_data.checked_copy.clear();
        data.checkbox_data.checked.resize(data.entries.size(), false);
        data.checkbox_data.cwd = "";
        data.checkbox_data.device = "";
        data.checkbox_data.count = 0;
    };

    void MainWindow(WindowData &data, u64 &key, bool progress) {
        Windows::SetupWindow();
        if (ImGui::Begin("NX-Shell", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::BeginTabBar("NX-Shell-tabs")) {
                Tabs::FileBrowser(data);
                Tabs::Settings(data);
                ImGui::EndTabBar();
            }
        }
        Windows::ExitWindow();

        if (progress)
            return;

        switch (data.state) {
            case WINDOW_STATE_OPTIONS:
                Popups::OptionsPopup(data);
                break;
            case WINDOW_STATE_PROPERTIES:
                Popups::FilePropertiesPopup(data, file_stat);
                break;
            case WINDOW_STATE_DELETE:
                Popups::DeletePopup(data);
                break;
            case WINDOW_STATE_IMAGEVIEWER:
                Windows::ImageViewer(image_properties, file_stat);
                ImageViewer::HandleControls(key, image_properties);
                break;
            case WINDOW_STATE_TEXTEDITOR:
                Windows::TextEditor();
                TextEditor::HandleInput(key);
                break;
            default:
                break;
        }

        // Only process main interface keys if not in text editor state
        if (data.state != WINDOW_STATE_TEXTEDITOR) {
            if ((key & HidNpadButton_X) && (data.state == WINDOW_STATE_FILEBROWSER))
                data.state = WINDOW_STATE_OPTIONS;

            if (data.state == WINDOW_STATE_FILEBROWSER) {
                static bool prev_y_pressed = false;
                bool curr_y_pressed = (key & HidNpadButton_Y) != 0;
                bool y_just_pressed = curr_y_pressed && !prev_y_pressed;
                prev_y_pressed = curr_y_pressed;
                if (y_just_pressed) {
                    if ((data.checkbox_data.cwd.length() != 0) && (data.checkbox_data.cwd != cwd))
                        Windows::ResetCheckbox(data);
                    if ((std::strncmp(data.entries[data.selected].name, "..", 2)) != 0) {
                        data.checkbox_data.checked[data.selected] = !data.checkbox_data.checked[data.selected];
                        data.checkbox_data.count += data.checkbox_data.checked[data.selected] ? 1 : -1;
                        data.checkbox_data.cwd = cwd;
                        data.checkbox_data.device = device;
                    }
                }
            }
        }

        if (key & HidNpadButton_B) {
            switch(data.state) {
                case WINDOW_STATE_OPTIONS:
                    data.state = WINDOW_STATE_FILEBROWSER;
                    break;
                case WINDOW_STATE_PROPERTIES:
                    data.state = WINDOW_STATE_OPTIONS;
                    file_stat = false;
                    break;
                case WINDOW_STATE_DELETE:
                    data.state = WINDOW_STATE_OPTIONS;
                    break;
                case WINDOW_STATE_IMAGEVIEWER:
                    if (image_properties) {
                        image_properties = false;
                        file_stat = false;
                    }
                    else {
                        ImageViewer::ClearTextures();
                        data.state = WINDOW_STATE_FILEBROWSER;
                    }
                    break;
                default:
                    break;
            }
            ImGui_ImplSwitch_ResetKeyStates();
        }

        key = 0;
    }
}