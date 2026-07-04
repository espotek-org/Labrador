#pragma once
#include "imgui.h"
class CursorProperties
{
public:
	CursorProperties()
	{}
	void Render()
	{
		ImGui::Begin("Cursor Properties",NULL);
		ImGui::Text("test");
		ImGui::End();
	}
};