#include "config.h"
#include "fs.h"
#include "gui.h"
#include "imgui.h"
#include "log.h"
#include "popups.h"

namespace Popups {
    void DeletePopup(void) {
		Popups::SetupPopup("Delete");
		
		if (ImGui::BeginPopupModal("Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("This action cannot be undone");
			if ((item.checked_count > 1) && (!item.checked_cwd.compare(config.cwd))) {
				ImGui::Text("Do you wish to delete the following:");
				ImGui::Dummy(ImVec2(0.0f, 5.0f)); // Spacing
				ImGui::BeginChild("Scrolling", ImVec2(0, 100));
				for (long unsigned int i = 0; i < item.checked.size(); i++) {
					if (item.checked.at(i))
						ImGui::Text(item.entries[i].name);
				}
				ImGui::EndChild();
			}
			else {
				std::string text = "Do you wish to delete " + item.selected_filename + "?";
				ImGui::Text(text.c_str());
			}
			
			ImGui::Dummy(ImVec2(0.0f, 5.0f)); // Spacing
			
			if (ImGui::Button("OK", ImVec2(120, 0))) {
				Result ret = 0;
				Log::Exit();

				if ((item.checked_count > 1) && (!item.checked_cwd.compare(config.cwd))) {
					for (long unsigned int i = 0; i < item.checked.size(); i++) {
						if (item.checked.at(i)) {
							if (R_FAILED(ret = FS::Delete(&item.entries[i]))) {
								FS::GetDirList(config.cwd, item.entries);
								GUI::ResetCheckbox();
								break;
							}
						}
					}
				}
				else
					ret = FS::Delete(&item.entries[item.selected]);
				
				if (R_SUCCEEDED(ret)) {
					FS::GetDirList(config.cwd, item.entries);
					GUI::ResetCheckbox();
				}

				Log::Exit();
				ImGui::CloseCurrentPopup();
				item.state = MENU_STATE_HOME;
			}
			
			ImGui::SameLine(0.0f, 15.0f);
			
			if (ImGui::Button("Cancel", ImVec2(120, 0))) {
				ImGui::CloseCurrentPopup();
				item.state = MENU_STATE_OPTIONS;
			}
		}
		
		Popups::ExitPopup();
	}
}
