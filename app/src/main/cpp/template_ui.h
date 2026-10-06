#pragma once
#include "imgui.h"
#include <functional>
#include <string>

struct OverlayMetrics {
    int width = 0, height = 0;
    float density = 1.0f;
    int left = 0, top = 0, right = 0, bottom = 0;
};

struct TemplateState {
    bool showLines = true, showRectangle = true, showText = true;
    float lineWidth = 3.0f;
    float color[4] = {0.0f, 0.85f, 0.65f, 1.0f};
    char text[512] = "Hello, ImGui!";
    int clicks = 0;
    int editingField = -1;
    int changedField = -1;
};

using EditorRequest = std::function<void(int, const std::string&, ImVec2, ImVec2, int)>;
void SetEditorRequest(EditorRequest callback);
bool InputTextAndroid(const char* label, char* buffer, size_t capacity, int field, TemplateState& state);
void DrawPanel(TemplateState& state);
void DrawOverlay(ImDrawList& draw, const OverlayMetrics& metrics, const TemplateState& state);
