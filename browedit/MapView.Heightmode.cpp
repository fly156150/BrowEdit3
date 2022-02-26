#include "MapView.h"

#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/gl/FBO.h>
#include <browedit/shaders/SimpleShader.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/math/Polygon.h>

#include <glm/gtc/type_ptr.hpp>

float TriangleHeight(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& p)
{
	glm::vec3 v0 = b - a;
	glm::vec3 v1 = c - a;
	glm::vec3 v2 = p - a;
	const float denom = v0.x * v1.z - v1.x * v0.z;
	float v = (v2.x * v1.z - v1.x * v2.z) / denom;
	float w = (v0.x * v2.z - v2.x * v0.z) / denom;
	float u = 1.0f - v - w;
	return a.y * u + b.y * v + c.y * w;
}

bool TriangleContainsPoint(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& p)
{
	bool b1 = ((p.x - b.x) * (a.z - b.z) - (p.z - b.z) * (a.x - b.x)) <= 0.0f;
	bool b2 = ((p.x - c.x) * (b.z - c.z) - (p.z - c.z) * (b.x - c.x)) <= 0.0f;
	bool b3 = ((p.x - a.x) * (c.z - a.z) - (p.z - a.z) * (c.x - a.x)) <= 0.0f;
	return ((b1 == b2) && (b2 == b3));
}


void MapView::postRenderHeightMode(BrowEdit* browEdit)
{
	auto gnd = map->rootNode->getComponent<Gnd>();
	auto gndRenderer = map->rootNode->getComponent<GndRenderer>();


	static bool showCenterArrow = true;
	static bool showCornerArrows = true;
	static bool showEdgeArrows = true;
	static int edgeMode = 0;
	static int tool = 0;

	ImGui::Begin("Height Edit");
	if (ImGui::CollapsingHeader("Tool Options", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::CollapsingHeader("Tool", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::RadioButton("Rectangle", &tool, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Lasso", &tool, 1);
			ImGui::SameLine();
			ImGui::RadioButton("Doodle", &tool, 2);
		}

		if (tool == 0 || tool == 1)
		{
			if (ImGui::CollapsingHeader("Selection Options", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Checkbox("Show Center Arrow", &showCenterArrow);
				ImGui::Checkbox("Show Corner Arrows", &showCornerArrows);
				ImGui::Checkbox("Show Edge Arrows", &showEdgeArrows);

				if (ImGui::CollapsingHeader("Edge handling", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::RadioButton("Nothing", &edgeMode, 0);
					ImGui::SameLine();
					ImGui::RadioButton("Raise ground", &edgeMode, 1);
					ImGui::SameLine();
					ImGui::RadioButton("Build walls", &edgeMode, 2);
				}
			}
		}
	}
	if (tileSelection.size() == 1)
	{
		auto cube = gnd->cubes[tileSelection[0].x][tileSelection[0].y];
		if (ImGui::CollapsingHeader("Tile Details", ImGuiTreeNodeFlags_DefaultOpen))
		{
			bool changed = false;
			changed |= util::DragFloat(browEdit, map, map->rootNode, "H1", &cube->h1);
			changed |= util::DragFloat(browEdit, map, map->rootNode, "H2", &cube->h2);
			changed |= util::DragFloat(browEdit, map, map->rootNode, "H3", &cube->h3);
			changed |= util::DragFloat(browEdit, map, map->rootNode, "H4", &cube->h4);

			static const char* tileNames[] = {"Tile Up", "Tile Front", "Tile Side"};
			for (int t = 0; t < 3; t++)
			{
				ImGui::PushID(t);
				changed |= util::DragInt(browEdit, map, map->rootNode, tileNames[t], &cube->tileIds[t], 1.0f, 0, (int)gnd->tiles.size()-1);
				if (cube->tileIds[t] >= 0)
				{
					if (ImGui::CollapsingHeader(("Edit " + std::string(tileNames[t])).c_str(), ImGuiTreeNodeFlags_DefaultOpen))
					{
						auto tile = gnd->tiles[cube->tileIds[t]];
						int lightmapId = tile->lightmapIndex;
						if (changed |= util::DragInt(browEdit, map, map->rootNode, "LightmapID", &lightmapId, 1.0, 0, (int)gnd->lightmaps.size() - 1))
							tile->lightmapIndex = lightmapId;

						int texIndex = tile->textureIndex;
						if (changed |= util::DragInt(browEdit, map, map->rootNode, "TextureID", &texIndex, 1.0f, 0, (int)gnd->textures.size()-1))
							tile->textureIndex = texIndex;
						glm::vec4 color = glm::vec4(tile->color) / 255.0f;
						if (changed |= util::ColorEdit4(browEdit, map, map->rootNode, "Color", &color))
							tile->color = glm::ivec4(color * 255.0f);
						changed |= util::DragFloat2(browEdit, map, map->rootNode, "UV1", &tile->v1, 0.01f, 0.0f, 1.0f);
						changed |= util::DragFloat2(browEdit, map, map->rootNode, "UV2", &tile->v2, 0.01f, 0.0f, 1.0f);
						changed |= util::DragFloat2(browEdit, map, map->rootNode, "UV3", &tile->v3, 0.01f, 0.0f, 1.0f);
						changed |= util::DragFloat2(browEdit, map, map->rootNode, "UV4", &tile->v4, 0.01f, 0.0f, 1.0f);
					}
				}
				ImGui::PopID();
			}

			if (changed)
				gndRenderer->setChunkDirty(tileSelection[0].x, tileSelection[0].y);
		}
	}
	ImGui::End();

	glUseProgram(0);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(glm::value_ptr(nodeRenderContext.projectionMatrix));
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(glm::value_ptr(nodeRenderContext.viewMatrix));
	glColor3f(1, 0, 0);
	fbo->bind();
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDisable(GL_TEXTURE_2D);

	simpleShader->use();
	simpleShader->setUniform(SimpleShader::Uniforms::projectionMatrix, nodeRenderContext.projectionMatrix);
	simpleShader->setUniform(SimpleShader::Uniforms::viewMatrix, nodeRenderContext.viewMatrix);
	simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
	simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
	glEnable(GL_BLEND);
	glDepthMask(0);
	bool canSelect = true;


	auto mouse3D = gnd->rayCast(mouseRay, viewEmptyTiles);
	glm::ivec2 tileHovered((int)glm::floor(mouse3D.x / 10), (gnd->height - (int)glm::floor(mouse3D.z) / 10));

	ImGui::Begin("Statusbar");
	ImGui::SetNextItemWidth(100.0f);
	ImGui::InputInt2("Cursor:", glm::value_ptr(tileHovered));
	ImGui::SameLine();
	ImGui::End();

	bool snap = snapToGrid;
	if (ImGui::GetIO().KeyShift)
		snap = !snap;

	//draw selection
	if (tileSelection.size() > 0)
	{
		std::vector<VertexP3T2N3> verts;
		float dist = 0.002f * cameraDistance;
		for (auto& tile : tileSelection)
		{
			auto cube = gnd->cubes[tile.x][tile.y];
			verts.push_back(VertexP3T2N3(glm::vec3(10 * tile.x, -cube->h3 + dist, 10 * gnd->height - 10 * tile.y), glm::vec2(0), cube->normals[2]));
			verts.push_back(VertexP3T2N3(glm::vec3(10 * tile.x, -cube->h1 + dist, 10 * gnd->height - 10 * tile.y + 10), glm::vec2(0), cube->normals[0]));
			verts.push_back(VertexP3T2N3(glm::vec3(10 * tile.x + 10, -cube->h4 + dist, 10 * gnd->height - 10 * tile.y), glm::vec2(0), cube->normals[3]));

			verts.push_back(VertexP3T2N3(glm::vec3(10 * tile.x, -cube->h1 + dist, 10 * gnd->height - 10 * tile.y + 10), glm::vec2(0), cube->normals[0]));
			verts.push_back(VertexP3T2N3(glm::vec3(10 * tile.x + 10, -cube->h4 + dist, 10 * gnd->height - 10 * tile.y), glm::vec2(0), cube->normals[3]));
			verts.push_back(VertexP3T2N3(glm::vec3(10 * tile.x + 10, -cube->h2 + dist, 10 * gnd->height - 10 * tile.y + 10), glm::vec2(0), cube->normals[1]));
		}
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		glDisableVertexAttribArray(3);
		glDisableVertexAttribArray(4); //TODO: vao
		glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data + 3);
		glVertexAttribPointer(2, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data + 5);

		glLineWidth(1.0f);
		simpleShader->setUniform(SimpleShader::Uniforms::color, glm::vec4(1, 0, 0, 1.0f));
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glDrawArrays(GL_TRIANGLES, 0, (int)verts.size());
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		simpleShader->setUniform(SimpleShader::Uniforms::color, glm::vec4(1, 0, 0, 0.25f));
		glDrawArrays(GL_TRIANGLES, 0, (int)verts.size());


		Gadget::setMatrices(nodeRenderContext.projectionMatrix, nodeRenderContext.viewMatrix);
		glDepthMask(1);

		glm::ivec2 maxValues(-99999, -99999), minValues(999999, 9999999);
		for (auto& s : tileSelection)
		{
			maxValues = glm::max(maxValues, s);
			minValues = glm::min(minValues, s);
		}
		static std::map<Gnd::Cube*, float[4]> originalValues;
		static glm::vec3 originalCorners[4];

		glm::vec3 pos[9];
		pos[0] = gnd->getPos(minValues.x, minValues.y, 0);
		pos[1] = gnd->getPos(maxValues.x, minValues.y, 1);
		pos[2] = gnd->getPos(minValues.x, maxValues.y, 2);
		pos[3] = gnd->getPos(maxValues.x, maxValues.y, 3);
		pos[4] = (pos[0] + pos[1] + pos[2] + pos[3]) / 4.0f;
		pos[5] = (pos[0] + pos[1]) / 2.0f;
		pos[6] = (pos[2] + pos[3]) / 2.0f;
		pos[7] = (pos[0] + pos[2]) / 2.0f;
		pos[8] = (pos[1] + pos[3]) / 2.0f;

		ImGui::Begin("Height Edit");
		if (ImGui::CollapsingHeader("Selection Details", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::DragFloat("Corner 1", &pos[0].y);
			ImGui::DragFloat("Corner 2", &pos[1].y);
			ImGui::DragFloat("Corner 3", &pos[2].y);
			ImGui::DragFloat("Corner 4", &pos[3].y);
		}
		ImGui::End();


		static int dragIndex = -1;
		for (int i = 0; i < 9; i++)
		{
			if (!showCornerArrows && i < 4)
				continue;
			if (!showCenterArrow && i == 4)
				continue;
			if (!showEdgeArrows && i > 4)
				continue;
			glm::mat4 mat = glm::translate(glm::mat4(1.0f), pos[i]);
			if (maxValues.x - minValues.x <= 1 || maxValues.y - minValues.y <= 1)
				mat = glm::scale(mat, glm::vec3(0.25f, 0.25f, 0.25f));

			if (dragIndex == -1)
				gadgetHeight[i].disabled = false;
			else
				gadgetHeight[i].disabled = dragIndex != i;

			gadgetHeight[i].draw(mouseRay, mat);

			if (gadgetHeight[i].axisClicked)
			{
				dragIndex = i;
				mouseDragPlane.normal = glm::normalize(glm::vec3(nodeRenderContext.viewMatrix * glm::vec4(0, 0, 1, 1)) - glm::vec3(nodeRenderContext.viewMatrix * glm::vec4(0, 0, 0, 1)));
				mouseDragPlane.normal.y = 0;
				mouseDragPlane.normal = glm::normalize(mouseDragPlane.normal);
				mouseDragPlane.D = -glm::dot(pos[i], mouseDragPlane.normal);

				float f;
				mouseRay.planeIntersection(mouseDragPlane, f);
				mouseDragStart = mouseRay.origin + f * mouseRay.dir;

				originalValues.clear();
				for (auto& t : tileSelection)
					for (int ii = 0; ii < 4; ii++)
						originalValues[gnd->cubes[t.x][t.y]][ii] = gnd->cubes[t.x][t.y]->heights[ii];

				for (int ii = 0; ii < 4; ii++)
					originalCorners[ii] = pos[ii];

				canSelect = false;
			}
			else if (gadgetHeight[i].axisReleased)
			{
				dragIndex = -1;
				canSelect = false;
			}
			else if (gadgetHeight[i].axisDragged)
			{

				if (snap)
				{
					glm::mat4 mat(1.0f);
					
					glm::vec3 rounded = pos[i];
					if (!gridLocal)
						rounded.y = glm::floor(rounded.y / gridSize) * gridSize;

					mat = glm::translate(mat, rounded);
					mat = glm::rotate(mat, glm::radians(90.0f), glm::vec3(1, 0, 0));
					simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, mat);
					simpleShader->setUniform(SimpleShader::Uniforms::color, glm::vec4(0, 0, 0, 1));
					glLineWidth(2);
					gridVbo->bind();
					glEnableVertexAttribArray(0);
					glEnableVertexAttribArray(1);
					glDisableVertexAttribArray(2);
					glDisableVertexAttribArray(3);
					glDisableVertexAttribArray(4); //TODO: vao
					glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2), (void*)(0 * sizeof(float)));
					glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2), (void*)(3 * sizeof(float)));
					glDrawArrays(GL_LINES, 0, (int)gridVbo->size());
					gridVbo->unBind();
				}


				float f;
				mouseRay.planeIntersection(mouseDragPlane, f);
				glm::vec3 mouseOffset = (mouseRay.origin + f * mouseRay.dir) - mouseDragStart;
				if (snap && gridLocal)
					mouseOffset = glm::round(mouseOffset / (float)gridSize) * (float)gridSize;

				canSelect = false;
				static int masks[] = { 0b0001, 0b0010, 0b0100, 0b1000, 0b1111, 0b0011, 0b1100, 0b0101, 0b1010 };
				int mask = masks[i];

				for (int ii = 0; ii < 4; ii++)
				{
					pos[ii] = originalCorners[ii];
					if (((mask >> ii) & 1) != 0)
					{
						pos[ii].y -= mouseOffset.y;
						if (snap && !gridLocal)
						{
							pos[ii].y += 2*mouseOffset.y;
							pos[ii].y = glm::round(pos[ii].y / gridSize) * gridSize;
							pos[ii].y = originalCorners[ii].y + (originalCorners[ii].y - pos[ii].y);
						}
					}
				}

				for (auto& t : tileSelection)
				{
					if (gndRenderer)
						gndRenderer->setChunkDirty(t.x, t.y);
					for (int ii = 0; ii < 4; ii++)
					{
						glm::vec3 p = gnd->getPos(t.x, t.y, ii);
						if (TriangleContainsPoint(pos[0], pos[1], pos[2], p))
							gnd->cubes[t.x][t.y]->heights[ii] = originalValues[gnd->cubes[t.x][t.y]][ii] + (TriangleHeight(pos[0], pos[1], pos[2], p) - TriangleHeight(originalCorners[0], originalCorners[1], originalCorners[2], p));
						if (TriangleContainsPoint(pos[1], pos[3], pos[2], p))
							gnd->cubes[t.x][t.y]->heights[ii] = originalValues[gnd->cubes[t.x][t.y]][ii] + (TriangleHeight(pos[1], pos[3], pos[2], p) - TriangleHeight(originalCorners[1], originalCorners[3], originalCorners[2], p));
					}
					gnd->cubes[t.x][t.y]->calcNormal();
				}
				for (auto& t : tileSelection)
					gnd->cubes[t.x][t.y]->calcNormals(gnd, t.x, t.y);
			}
		}

		simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
		glDepthMask(0);
	}




	if (hovered && canSelect)
	{
		if (ImGui::IsMouseDown(0))
		{
			if (!mouseDown)
				mouseDragStart = mouse3D;
			mouseDown = true;
		}

		if (ImGui::IsMouseReleased(0))
		{
			auto mouseDragEnd = mouse3D;
			mouseDown = false;
			if(!ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl)
				tileSelection.clear();

			if (tool == 0) //rectangle
			{
				int tileMinX = (int)glm::floor(glm::min(mouseDragStart.x, mouseDragEnd.x) / 10);
				int tileMaxX = (int)glm::ceil(glm::max(mouseDragStart.x, mouseDragEnd.x) / 10);

				int tileMaxY = gnd->height - (int)glm::floor(glm::min(mouseDragStart.z, mouseDragEnd.z) / 10) + 1;
				int tileMinY = gnd->height - (int)glm::ceil(glm::max(mouseDragStart.z, mouseDragEnd.z) / 10) + 1;

				if (tileMinX >= 0 && tileMaxX < gnd->width + 1 && tileMinY >= 0 && tileMaxY < gnd->height + 1)
					for (int x = tileMinX; x < tileMaxX; x++)
						for (int y = tileMinY; y < tileMaxY; y++)
							if (ImGui::GetIO().KeyCtrl)
							{
								if (std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(x, y)) != tileSelection.end())
									tileSelection.erase(std::remove_if(tileSelection.begin(), tileSelection.end(), [&](const glm::ivec2& el) { return el.x == x && el.y == y; }));
							}
							else if (std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(x, y)) == tileSelection.end())
								tileSelection.push_back(glm::ivec2(x, y));
			}
			else if (tool == 1) // lassoo
			{
				math::Polygon polygon;
				for (size_t i = 0; i < selectLasso.size(); i++)
					polygon.push_back(glm::vec2(selectLasso[i]));
				for (int x = 0; x < gnd->width; x++)
				{
					for (int y = 0; y < gnd->height; y++)
					{
						if(polygon.contains(glm::vec2(x, y)))
							if (ImGui::GetIO().KeyCtrl)
							{
								if (std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(x, y)) != tileSelection.end())
									tileSelection.erase(std::remove_if(tileSelection.begin(), tileSelection.end(), [&](const glm::ivec2& el) { return el.x == x && el.y == y; }));
							}
							else if (std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(x, y)) == tileSelection.end())
								tileSelection.push_back(glm::ivec2(x, y));

					}
				}
				for (auto t : selectLasso)
					if (ImGui::GetIO().KeyCtrl)
					{
						if (std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(t.x, t.y)) != tileSelection.end())
							tileSelection.erase(std::remove_if(tileSelection.begin(), tileSelection.end(), [&](const glm::ivec2& el) { return el.x == t.x && el.y == t.y; }));
					}
					else if (std::find(tileSelection.begin(), tileSelection.end(), glm::ivec2(t.x, t.y)) == tileSelection.end())
						tileSelection.push_back(glm::ivec2(t.x, t.y));
				selectLasso.clear();
			}
		}
	}
	if (mouseDown)
	{
		auto mouseDragEnd = mouse3D;
		std::vector<VertexP3T2N3> verts;
		float dist = 0.002f * cameraDistance;

		int tileX = (int)glm::floor(mouseDragEnd.x / 10);
		int tileY = gnd->height - (int)glm::floor(mouseDragEnd.z / 10);

		int tileMinX = (int)glm::floor(glm::min(mouseDragStart.x, mouseDragEnd.x) / 10);
		int tileMaxX = (int)glm::ceil(glm::max(mouseDragStart.x, mouseDragEnd.x) / 10);

		int tileMaxY = gnd->height - (int)glm::floor(glm::min(mouseDragStart.z, mouseDragEnd.z) / 10) + 1;
		int tileMinY = gnd->height - (int)glm::ceil(glm::max(mouseDragStart.z, mouseDragEnd.z) / 10) + 1;


		ImGui::Begin("Statusbar");
		ImGui::Text("Selection: (%d,%d) - (%d,%d)", tileMinX, tileMinY, tileMaxX, tileMaxY);
		ImGui::End();

		if (tool == 0) // select rectangular area
		{
			if (tileMinX >= 0 && tileMaxX < gnd->width + 1 && tileMinY >= 0 && tileMaxY < gnd->height + 1)
				for (int x = tileMinX; x < tileMaxX; x++)
				{
					for (int y = tileMinY; y < tileMaxY; y++)
					{
						auto cube = gnd->cubes[x][y];
						verts.push_back(VertexP3T2N3(glm::vec3(10 * x, -cube->h3 + dist, 10 * gnd->height - 10 * y), glm::vec2(0), cube->normals[2]));
						verts.push_back(VertexP3T2N3(glm::vec3(10 * x, -cube->h1 + dist, 10 * gnd->height - 10 * y + 10), glm::vec2(0), cube->normals[0]));
						verts.push_back(VertexP3T2N3(glm::vec3(10 * x + 10, -cube->h4 + dist, 10 * gnd->height - 10 * y), glm::vec2(0), cube->normals[3]));

						verts.push_back(VertexP3T2N3(glm::vec3(10 * x, -cube->h1 + dist, 10 * gnd->height - 10 * y + 10), glm::vec2(0), cube->normals[0]));
						verts.push_back(VertexP3T2N3(glm::vec3(10 * x + 10, -cube->h4 + dist, 10 * gnd->height - 10 * y), glm::vec2(0), cube->normals[3]));
						verts.push_back(VertexP3T2N3(glm::vec3(10 * x + 10, -cube->h2 + dist, 10 * gnd->height - 10 * y + 10), glm::vec2(0), cube->normals[1]));
					}
				}
		}
		else if (tool == 1) // lasso
		{
			if (tileX >= 0 && tileX < gnd->width && tileY >= 0 && tileY < gnd->height)
			{
				if (selectLasso.empty())
					selectLasso.push_back(glm::ivec2(tileX, tileY));
				else
					if (selectLasso[selectLasso.size() - 1] != glm::ivec2(tileX, tileY))
						selectLasso.push_back(glm::ivec2(tileX, tileY));
			}

			for (auto& t : selectLasso)
			{
				auto cube = gnd->cubes[t.x][t.y];
				verts.push_back(VertexP3T2N3(glm::vec3(10 * t.x, -cube->h3 + dist, 10 * gnd->height - 10 * t.y), glm::vec2(0), cube->normals[2]));
				verts.push_back(VertexP3T2N3(glm::vec3(10 * t.x, -cube->h1 + dist, 10 * gnd->height - 10 * t.y + 10), glm::vec2(0), cube->normals[0]));
				verts.push_back(VertexP3T2N3(glm::vec3(10 * t.x + 10, -cube->h4 + dist, 10 * gnd->height - 10 * t.y), glm::vec2(0), cube->normals[3]));

				verts.push_back(VertexP3T2N3(glm::vec3(10 * t.x, -cube->h1 + dist, 10 * gnd->height - 10 * t.y + 10), glm::vec2(0), cube->normals[0]));
				verts.push_back(VertexP3T2N3(glm::vec3(10 * t.x + 10, -cube->h4 + dist, 10 * gnd->height - 10 * t.y), glm::vec2(0), cube->normals[3]));
				verts.push_back(VertexP3T2N3(glm::vec3(10 * t.x + 10, -cube->h2 + dist, 10 * gnd->height - 10 * t.y + 10), glm::vec2(0), cube->normals[1]));
			}
		}

		if (verts.size() > 0)
		{
			glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data);
			glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data + 3);
			glVertexAttribPointer(2, 3, GL_FLOAT, false, sizeof(VertexP3T2N3), verts[0].data + 5);
			simpleShader->setUniform(SimpleShader::Uniforms::color, glm::vec4(1, 0, 0, 0.5f));
			glDrawArrays(GL_TRIANGLES, 0, (int)verts.size());
		}


	}

	glDepthMask(1);






	fbo->unbind();
}