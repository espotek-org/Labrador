#pragma once
#include "imgui.h"
#include <string>
#include <sstream>
#include "util.h"
#include "OscData.hpp"


/// <summary>
/// Abstract class that draws control widget such as PSU Control
/// </summary>
class ControlWidget
{
public:
	bool show_help = false;
	bool init_closed = false;
	bool first_render = true;

	/// <summary>
	/// Constructor
	/// </summary>
	/// <param name="label">Name of controller</param>
	/// <param name="size">Child window size</param>
	/// <param name="borderColor">Accent colour</param>
	ControlWidget(std::string label, ImVec2 size, const float* accentColour)
	    : label(label)
	    , size(size)
	    , accentColour(accentColour)
	    , help_popup_id(label + " Help")
	    , pinout_height(0)
	    , pinout_width(0)
	    , pinout_texture((intptr_t)0)
	{}
	void SetOscs(OscData* Osc1Data, OscData* Osc2Data, OscData* MathData)
	{
		this->OSC1Data = Osc1Data;
		this->OSC2Data = Osc2Data;
		this->MathData = MathData;
	}
	void SetPlots(SpectrumPlots* spectrum_plots, NetworkPlots* network_plots)
	{
		this->spectrum_plots = spectrum_plots;
		this->network_plots = network_plots;
	}

	/// <summary>
	/// Update size of child window
	/// </summary>
	/// <param name="new_size">New size</param>
	void setSize(ImVec2 new_size)
	{
		size = new_size;
	}

	/// <summary>
	/// Generic function to render control widget with correct style
	/// </summary>
	virtual void Render()
	{
		BeginControlWidgetStyle(accentColour);

		ImU32 acol = ImGui::ColorConvertFloat4ToU32(
		    ImVec4(accentColour[0], accentColour[1], accentColour[2], 1));

		float height = ImGui::GetFrameHeight();
		ImVec2 p1 = ImGui::GetCursorScreenPos();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		static float border_radius = 8.0f;
		
		// Title bar background
		draw->AddRectFilled(p1,
		    ImVec2(p1.x + ImGui::GetContentRegionAvail().x, p1.y + height), acol,
		    border_radius, ImDrawFlags_RoundCornersTop);

		ImGui::AlignTextToFramePadding();
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_AllowOverlap; // 1.92: AllowItemOverlap renamed AllowOverlap
		if (!init_closed)
		{
			flags |= ImGuiTreeNodeFlags_DefaultOpen;
		}
		if (first_render && init_closed)
		{
			ImGui::SetNextItemOpen(false, ImGuiCond_Always);
		}
		first_render = false;
		bool treeopen = ImGui::TreeNodeEx(label.c_str(), flags);
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
		
		// Help button
		if (ImGui::Button("  ?  "))
		{
			show_help = true;
		}

		// Widget Body
		if (treeopen)
		{
			renderControl();
			ImGui::TreePop();
		}
		
		// Widget border
		ImVec2 p2 = ImGui::GetCursorScreenPos();
		draw->AddRect(p1, ImVec2(p2.x + ImGui::GetContentRegionAvail().x, p2.y), acol,
		    border_radius, ImDrawFlags_RoundCornersAll, 1.0f);
		// Empty Space
		ImGui::Dummy(ImVec2(0, 6.0f));

		WidgetHeight = p2.y - p1.y;

		EndControlWidgetStyle();
	}

	float GetHeight()
	{
		return WidgetHeight;
	}

	ImU32 GetAccentColour()
	{
		return colourConvert(accentColour);
	}

	std::string getLabel()
	{
		return label;
	}

	void setHelpText(std::string text)
	{
		std::stringstream ss(text);
		std::string line;
		while (std::getline(ss, line))
		{
			// Top-level heading
			if (line.compare(0, 5, "#### ") == 0)
			{
				line.erase(0, 5);
				TreeNode new_header;
				new_header.name = line;
				help_trees.push_back(new_header);
				help_trees.back().expanded = false;
			}
			else help_trees.back().bullets.push_back(line);
			
		}
	}

	void setPinoutImg(intptr_t texture, int w, int h)
	{
		pinout_texture = texture;
		pinout_width = w;
		pinout_height = h;
	}

	void renderHelpText(bool expandAll = false, bool collapseAll = false, std::string search = "")
	{
		if (search.length() ==  0) for (TreeNode& t : help_trees)
		{
			if (expandAll) ImGui::SetNextItemOpen(true);
			else if (collapseAll) ImGui::SetNextItemOpen(false);
			else ImGui::SetNextItemOpen(t.expanded);

			if (ImGui::TreeNode((t.name + "##" + label).c_str()))
			{
				t.expanded = true;
				for (const std::string& l : t.bullets)
				{
					MarkdownToImGUIBullet(l);
				}
				ImGui::TreePop();
			}
			else
				t.expanded = false;
		}
		// Filtered help list
		else for (TreeNode& t : help_trees)
		{
			// Always expand when search string is long enough
			ImGui::SetNextItemOpen(true);
			
			if (t.isFound(search) && ImGui::TreeNode((t.name + "##" + label).c_str()))
			{
				for (const std::string& l : t.bullets)
				{
					if (l.find(search) != std::string::npos)
						MarkdownToImGUIBullet(l);
				}
				ImGui::TreePop();
			}
		}

		if (expandAll)
			ImGui::SetNextItemOpen(true);
		else if (collapseAll)
			ImGui::SetNextItemOpen(false);
		if (pinout_texture != (intptr_t)0 && ImGui ::TreeNode(("Pinout Diagram##" + label).c_str()))
		{
			ImGui::Image((ImTextureID) pinout_texture, ImVec2(pinout_width, pinout_height)); // 1.92: ImTextureID is ImU64, no void* cast
			ImGui::TreePop();
		}
	}

	void MarkdownToImGUIBullet(std::string md_text)
	{
		if (md_text.compare(0, 2, "- ") == 0)
		{
			md_text.erase(0, 2);
			ImGui::BulletText("");
			ImGui::SameLine();
			ImGui::TextWrapped(u8"%s", md_text.c_str());
		}
		else if (md_text.compare(0, 4, "  - ") == 0)
		{
			md_text.erase(0, 4);
			ImGui::Dummy(ImVec2(10, 0));
			ImGui::SameLine();
			ImGui::BulletText("");
			ImGui::SameLine();
			ImGui::TextWrapped(u8"%s", md_text.c_str());
		}
		else if (md_text.compare(0, 6, "    - ") == 0)
		{
			md_text.erase(0, 6);
			ImGui::Dummy(ImVec2(20, 0));
			ImGui::SameLine();
			ImGui::BulletText("");
			ImGui::SameLine();
			ImGui::TextWrapped(u8"%s", md_text.c_str());
		}
		else
		{
			ImGui::TextWrapped(u8"%s", md_text.c_str());
		}
	}

	void renderHelp()
	{
		const int min_width = 400;
		const int min_height = 400;
		ImGui::SetNextWindowSizeConstraints(ImVec2(min_width, min_height), ImVec2(FLT_MAX, FLT_MAX));
		// Render Help Text from markdown format 
		// Edit README.md to change help popup content
		if (!ImGui::Begin((help_popup_id).c_str(), &show_help))
		{
			ImGui::End();
			return;
		}
		if (!ImGui::IsWindowFocused())
		{
			show_help = false;
			ImGui::End();
			return;
		}

		renderHelpText();

		ImGui::End();
		
	}
	void SetInitClosed(bool closed = true)
	{
		init_closed = closed;
	}

	//bool BeginHelpPopup(const char* str_id, ImGuiWindowFlags flags)
	//{
	//	
	//	ImGuiContext& g = *GImGui;
	//	if (g.OpenPopupStack.Size <= g.BeginPopupStack.Size) // Early out for performance
	//	{
	//		g.NextWindowData.ClearFlags(); // We behave like Begin() and need to consume
	//		                               // those values
	//		return false;
	//	}
	//	flags |= ImGuiWindowFlags_NoTitleBar;
	//	return ImGui::BeginPopupEx(g.CurrentWindow->GetID(str_id), flags);
	//}

	// Customise for each widget, see PSUControl.hpp for example
	virtual void renderControl() = 0;
	virtual bool controlLab() = 0;
	

protected:
	const std::string label;
	ImVec2 size;
	const float* accentColour;
	OscData* OSC1Data = nullptr;
	OscData* OSC2Data = nullptr;
	OscData* MathData = nullptr;
	SpectrumPlots* spectrum_plots = nullptr;
	NetworkPlots* network_plots = nullptr;
private:
	const std::string help_popup_id;
	float WidgetHeight = 0;
	std::vector<TreeNode> help_trees;
	intptr_t pinout_texture;
	int pinout_width, pinout_height;
	
};

/// <summary>
/// Purely documentation, eg. TroubleShooting
/// Hacky solution, probably need to make Help parsing external to ControlWidget
/// </summary>
class HelpWidget : public ControlWidget
{
public:

	HelpWidget(std::string label)
		: ControlWidget(label, ImVec2(0, 0), NULL)
	{}

	void renderControl() override
	{
		return;
	}
	bool controlLab() override
	{
		return false;
	}
};