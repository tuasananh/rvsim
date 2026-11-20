#pragma once

// Centralized visual tokens for the rvsim IDE: palette, semantic colors,
// spacing, and the Dear ImGui theme derived from them. Text colors are chosen
// for >= 4.5:1 contrast against the surfaces they are drawn on.

#include <algorithm>

#include "imgui.h"

namespace rvsim::ui {

struct Palette {
  // Surfaces (darkest to lightest).
  ImVec4 surface_base{0.067f, 0.075f, 0.094f, 1.0f};     // #111318
  ImVec4 surface_raised{0.094f, 0.106f, 0.133f, 1.0f};   // #181B22
  ImVec4 surface_overlay{0.133f, 0.149f, 0.184f, 1.0f};  // #22262F
  ImVec4 border_subtle{0.208f, 0.231f, 0.282f, 1.0f};    // #353B48
  ImVec4 border_strong{0.333f, 0.373f, 0.451f, 1.0f};    // #555F73

  // Content.
  ImVec4 text_primary{0.906f, 0.922f, 0.949f, 1.0f};    // #E7EBF2 (14.9:1)
  ImVec4 text_secondary{0.651f, 0.690f, 0.761f, 1.0f};  // #A6B0C2 (8.0:1)
  ImVec4 text_disabled{0.459f, 0.490f, 0.553f, 1.0f};   // #757D8D (4.5:1)

  // Interactive accent (blue) and filled-control surfaces.
  ImVec4 accent{0.380f, 0.663f, 1.000f, 1.0f};  // #61A9FF
  ImVec4 accent_hover{0.502f, 0.737f, 1.000f, 1.0f};
  ImVec4 accent_active{0.243f, 0.541f, 0.910f, 1.0f};
  ImVec4 button{0.165f, 0.188f, 0.235f, 1.0f};
  ImVec4 button_hover{0.212f, 0.243f, 0.302f, 1.0f};
  ImVec4 button_active{0.255f, 0.290f, 0.361f, 1.0f};
  ImVec4 primary_button{0.086f, 0.341f, 0.651f, 1.0f};  // white text 6.9:1
  ImVec4 primary_button_hover{0.106f, 0.396f, 0.741f, 1.0f};
  ImVec4 primary_button_active{0.067f, 0.290f, 0.565f, 1.0f};

  // Semantic.
  ImVec4 success{0.345f, 0.820f, 0.545f, 1.0f};  // #58D18B
  ImVec4 warning{1.000f, 0.741f, 0.290f, 1.0f};  // #FFBD4A
  ImVec4 danger{1.000f, 0.420f, 0.420f, 1.0f};   // #FF6B6B
  ImVec4 info{0.380f, 0.663f, 1.000f, 1.0f};

  // Debugger-specific.
  ImVec4 pc_arrow{1.000f, 0.820f, 0.290f, 1.0f};     // current instruction
  ImVec4 pc_line_bg{1.000f, 0.820f, 0.290f, 0.13f};  // current line tint
  ImVec4 breakpoint{0.937f, 0.267f, 0.267f, 1.0f};   // gutter dot
  ImVec4 breakpoint_hover{0.937f, 0.267f, 0.267f, 0.40f};
  ImVec4 changed_value{1.000f, 0.741f, 0.290f, 1.0f};  // modified register
  ImVec4 changed_bg{1.000f, 0.741f, 0.290f, 0.14f};
  ImVec4 symbol{0.561f, 0.839f, 0.643f, 1.0f};    // labels / symbols
  ImVec4 unmapped{0.365f, 0.392f, 0.443f, 1.0f};  // unallocated memory

  // State pills: filled backgrounds behind dark text.
  ImVec4 pill_running{0.345f, 0.820f, 0.545f, 1.0f};
  ImVec4 pill_paused{1.000f, 0.741f, 0.290f, 1.0f};
  ImVec4 pill_waiting{0.380f, 0.663f, 1.000f, 1.0f};
  ImVec4 pill_halted{0.651f, 0.690f, 0.761f, 1.0f};
  ImVec4 pill_error{1.000f, 0.420f, 0.420f, 1.0f};
  ImVec4 pill_text{0.067f, 0.075f, 0.094f, 1.0f};
};

inline const Palette& Colors() {
  static const Palette palette;
  return palette;
}

inline ImU32 U32(const ImVec4& color) {
  return ImGui::ColorConvertFloat4ToU32(color);
}

// Spacing scale (multiples of a 4 px unit, scaled by DPI via style sizes).
namespace spacing {
inline constexpr float kUnit = 4.0f;
inline constexpr float kXs = kUnit;
inline constexpr float kSm = kUnit * 2.0f;
inline constexpr float kMd = kUnit * 3.0f;
inline constexpr float kLg = kUnit * 4.0f;
}  // namespace spacing

// Applies the rvsim theme. Call at startup (not every frame) after creating
// the ImGui context; `dpi_scale` scales sizes and padding.
inline void ApplyTheme(float dpi_scale) {
  const Palette& c = Colors();
  ImGuiStyle& style = ImGui::GetStyle();
  const float font_scale_dpi = style.FontScaleDpi;
  style = ImGuiStyle{};

  style.WindowPadding = ImVec2(spacing::kSm, spacing::kSm);
  style.FramePadding = ImVec2(spacing::kSm, spacing::kUnit * 1.25f);
  style.CellPadding = ImVec2(spacing::kSm, spacing::kUnit * 0.5f);
  style.ItemSpacing = ImVec2(spacing::kSm, spacing::kUnit * 1.5f);
  style.ItemInnerSpacing = ImVec2(spacing::kUnit * 1.5f, spacing::kUnit);
  style.IndentSpacing = spacing::kLg + spacing::kUnit;
  style.ScrollbarSize = spacing::kMd + 1.0f;
  style.GrabMinSize = spacing::kSm + spacing::kUnit;
  style.WindowRounding = 4.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 3.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 3.0f;
  style.TabRounding = 3.0f;
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

  ImVec4* colors = style.Colors;
  colors[ImGuiCol_Text] = c.text_primary;
  colors[ImGuiCol_TextDisabled] = c.text_disabled;
  colors[ImGuiCol_WindowBg] = c.surface_base;
  colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_PopupBg] = c.surface_overlay;
  colors[ImGuiCol_Border] = c.border_subtle;
  colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_FrameBg] = c.surface_raised;
  colors[ImGuiCol_FrameBgHovered] = c.surface_overlay;
  colors[ImGuiCol_FrameBgActive] = c.button_active;
  colors[ImGuiCol_TitleBg] = c.surface_base;
  colors[ImGuiCol_TitleBgActive] = c.surface_raised;
  colors[ImGuiCol_TitleBgCollapsed] = c.surface_base;
  colors[ImGuiCol_MenuBarBg] = c.surface_raised;
  colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ScrollbarGrab] = c.border_subtle;
  colors[ImGuiCol_ScrollbarGrabHovered] = c.border_strong;
  colors[ImGuiCol_ScrollbarGrabActive] = c.accent_active;
  colors[ImGuiCol_CheckMark] = c.accent;
  colors[ImGuiCol_SliderGrab] = c.accent;
  colors[ImGuiCol_SliderGrabActive] = c.accent_hover;
  colors[ImGuiCol_Button] = c.button;
  colors[ImGuiCol_ButtonHovered] = c.button_hover;
  colors[ImGuiCol_ButtonActive] = c.button_active;
  colors[ImGuiCol_Header] = ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.20f);
  colors[ImGuiCol_HeaderHovered] =
      ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.32f);
  colors[ImGuiCol_HeaderActive] =
      ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.45f);
  colors[ImGuiCol_Separator] = c.border_subtle;
  colors[ImGuiCol_SeparatorHovered] = c.accent_hover;
  colors[ImGuiCol_SeparatorActive] = c.accent;
  colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_ResizeGripHovered] =
      ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.55f);
  colors[ImGuiCol_ResizeGripActive] = c.accent;
  colors[ImGuiCol_Tab] = c.surface_raised;
  colors[ImGuiCol_TabHovered] = c.surface_overlay;
  colors[ImGuiCol_TabSelected] = c.surface_overlay;
  colors[ImGuiCol_TabSelectedOverline] = c.accent;
  colors[ImGuiCol_TabDimmed] = c.surface_base;
  colors[ImGuiCol_TabDimmedSelected] = c.surface_raised;
  colors[ImGuiCol_TabDimmedSelectedOverline] = c.border_strong;
  colors[ImGuiCol_DockingPreview] =
      ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.55f);
  colors[ImGuiCol_DockingEmptyBg] = c.surface_base;
  colors[ImGuiCol_TableHeaderBg] = c.surface_raised;
  colors[ImGuiCol_TableBorderStrong] = c.border_subtle;
  colors[ImGuiCol_TableBorderLight] = c.border_subtle;
  colors[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  colors[ImGuiCol_TableRowBgAlt] =
      ImVec4(c.surface_raised.x, c.surface_raised.y, c.surface_raised.z, 0.6f);
  colors[ImGuiCol_TextSelectedBg] =
      ImVec4(c.accent.x, c.accent.y, c.accent.z, 0.35f);
  colors[ImGuiCol_NavCursor] = c.accent_hover;
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);

  style.ScaleAllSizes(std::clamp(dpi_scale, 0.75f, 4.0f));
  style.FontScaleDpi = font_scale_dpi;
}

}  // namespace rvsim::ui
