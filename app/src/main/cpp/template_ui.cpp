#include "template_ui.h"
#include <algorithm>
#include <utility>

static EditorRequest editorRequest;
void SetEditorRequest(EditorRequest callback) { editorRequest = std::move(callback); }

bool InputTextAndroid(const char* label, char* buffer, size_t capacity, int field, TemplateState& state) {
    ImGui::TextUnformatted(label);
    ImGui::PushID(field);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##android_text", buffer, capacity, ImGuiInputTextFlags_ReadOnly);
    bool clicked = ImGui::IsItemClicked();
    if (clicked && state.editingField != field && editorRequest) {
        state.editingField = field;
        ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        editorRequest(field, buffer, min, {max.x - min.x, max.y - min.y}, static_cast<int>(capacity - 1));
    }
    ImGui::PopID();
    bool changed = state.changedField == field;
    if (changed) state.changedField = -1;
    return changed;
}

void DrawPanel(TemplateState& state) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##panel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                 | ImGuiWindowFlags_NoSavedSettings);
    ImGui::Text("Vulkan / Dear ImGui %s", ImGui::GetVersion());
    ImGui::Separator();
    ImGui::Checkbox("绘制线条", &state.showLines);
    ImGui::Checkbox("绘制矩形", &state.showRectangle);
    ImGui::Checkbox("绘制文字", &state.showText);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##width", &state.lineWidth, 1, 10, "线宽 %.1f");
    ImGui::ColorEdit4("颜色", state.color, ImGuiColorEditFlags_NoInputs);
    InputTextAndroid("文字", state.text, sizeof(state.text), 0, state);
    if (ImGui::Button("计数")) ++state.clicks;
    ImGui::SameLine(); ImGui::Text("%d", state.clicks);
    ImGui::Text("%.0f FPS", io.Framerate);
    ImGui::End();
}

void DrawOverlay(ImDrawList& draw, const OverlayMetrics& m, const TemplateState& state) {
    float d = m.density;
    float x = m.left + 28 * d, y = m.top + 28 * d;
    float right = std::max(x, static_cast<float>(m.width - m.right) - 28 * d);
    float bottom = std::max(y, static_cast<float>(m.height - m.bottom) - 28 * d);
    float w = std::min(180 * d, right - x), h = std::min(100 * d, bottom - y);
    ImU32 color = ImGui::ColorConvertFloat4ToU32({state.color[0], state.color[1], state.color[2], state.color[3]});
    if (state.showRectangle) draw.AddRect({x, y}, {x + w, y + h}, color, 0, 0, state.lineWidth * d);
    if (state.showLines) {
        draw.AddLine({x, y}, {x + w, y + h}, color, state.lineWidth * d);
        draw.AddLine({x + w, y}, {x, y + h}, color, state.lineWidth * d);
    }
    if (state.showText) draw.AddText({x, std::min(bottom, y + h + 12 * d)}, color, state.text);
}
