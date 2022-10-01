#include <browedit/BrowEdit.h>
#include <imgui.h>
#include <glm/gtc/type_ptr.hpp>

void BrowEdit::showColorEditWindow()
{
	ImGui::Begin("Color Edit Window");

	static ImVec4 color;
	ImGui::ColorPicker4("Color", glm::value_ptr(colorEditBrushColor));
	ImGui::SliderFloat("Brush Hardness", &colorEditBrushHardness, 0, 1);
	ImGui::InputInt("Brush Size", &colorEditBrushSize);
	ImGui::InputFloat("Brush Delay", &colorEditDelay, 0.05f);



	ImGui::End();
}