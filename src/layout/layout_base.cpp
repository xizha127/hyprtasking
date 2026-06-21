#include <any>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <vector>

#define private public
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/ClearPassElement.hpp>
#include <hyprland/src/render/pass/RectPassElement.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#undef private

#include "../globals.hpp"
#include "../config.hpp"
#include "../pass/pass_element.hpp"
#include "../render.hpp"
#include "../types.hpp"
#include "layout_base.hpp"

namespace {
using std::optional;

enum class LabelPos {
    TopLeft,
    Top,
    TopRight,
    MidLeft,
    Mid,
    MidRight,
    BottomLeft,
    Bottom,
    BottomRight,
};

LabelPos parse_label_pos(std::string pos) {
    for (char& c : pos) {
        if (c == '-')
            c = '_';
        else
            c = (char)std::tolower((unsigned char)c);
    }

    if (pos == "top")
        return LabelPos::Top;
    if (pos == "top_right")
        return LabelPos::TopRight;
    if (pos == "mid_left")
        return LabelPos::MidLeft;
    if (pos == "mid" || pos == "center" || pos == "middle")
        return LabelPos::Mid;
    if (pos == "mid_right")
        return LabelPos::MidRight;
    if (pos == "bottom_left")
        return LabelPos::BottomLeft;
    if (pos == "bottom")
        return LabelPos::Bottom;
    if (pos == "bottom_right")
        return LabelPos::BottomRight;
    return LabelPos::TopLeft;
}

Vector2D label_origin(const CBox& box, LabelPos pos, const Vector2D& size, float pad) {
    const auto x = [&](float value) {
        return std::clamp(value, 0.f, std::max(0.f, (float)(box.w - size.x)));
    };
    const auto y = [&](float value) {
        return std::clamp(value, 0.f, std::max(0.f, (float)(box.h - size.y)));
    };

    switch (pos) {
        case LabelPos::Top:
            return Vector2D {x((box.w - size.x) / 2.f), y(pad)};
        case LabelPos::TopRight:
            return Vector2D {x(box.w - size.x - pad), y(pad)};
        case LabelPos::MidLeft:
            return Vector2D {x(pad), y((box.h - size.y) / 2.f)};
        case LabelPos::Mid:
            return Vector2D {x((box.w - size.x) / 2.f), y((box.h - size.y) / 2.f)};
        case LabelPos::MidRight:
            return Vector2D {x(box.w - size.x - pad), y((box.h - size.y) / 2.f)};
        case LabelPos::BottomLeft:
            return Vector2D {x(pad), y(box.h - size.y - pad)};
        case LabelPos::Bottom:
            return Vector2D {x((box.w - size.x) / 2.f), y(box.h - size.y - pad)};
        case LabelPos::BottomRight:
            return Vector2D {x(box.w - size.x - pad), y(box.h - size.y - pad)};
        case LabelPos::TopLeft:
        default:
            return Vector2D {x(pad), y(pad)};
    }
}

uint64_t parse_hex_color(const std::string& raw) {
    std::string s = raw;
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), s.end());

    if (s.empty())
        return 0;
    if (s.starts_with("0x") || s.starts_with("0X"))
        s = s.substr(2);
    if (!s.empty() && s[0] == '#')
        s = s.substr(1);

    uint64_t value = 0;
    try {
        value = std::stoull(s, nullptr, 16);
    } catch (...) {
        return 0;
    }
    if (s.size() <= 6)
        value |= 0xFF000000ULL;
    return value;
}

CHyprColor resolve_label_color(
    const std::string& raw,
    const CHyprColor& fallback,
    const CHyprColor& matugen_color,
    bool use_matugen,
    const MatugenPalette& palette
) {
    if (!raw.empty())
        return CHyprColor {parse_hex_color(raw)};
    if (use_matugen && palette.valid)
        return matugen_color;
    return fallback;
}

optional<uint64_t> extract_lua_color_hex(const std::string& text, const std::string& key) {
    const std::regex re(
        key + R"(\s*=\s*["']?(?:rgb|rgba)\(([0-9A-Fa-f]{6,8})\)["']?)"
    );
    std::smatch match;
    if (!std::regex_search(text, match, re) || match.size() < 2)
        return std::nullopt;

    std::string hex = match[1].str();
    if (hex.size() == 6)
        hex += "FF";

    try {
        return std::stoull(hex, nullptr, 16);
    } catch (...) {
        return std::nullopt;
    }
}

optional<MatugenPalette> load_matugen_palette_from_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open())
        return std::nullopt;

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    const auto primary_hex = extract_lua_color_hex(text, "active_border")
        .or_else([&]() { return extract_lua_color_hex(text, "primary"); });
    const auto secondary_hex = extract_lua_color_hex(text, "inactive_border")
        .or_else([&]() { return extract_lua_color_hex(text, "secondary"); });

    if (!primary_hex.has_value())
        return std::nullopt;

    MatugenPalette palette;
    palette.primary = CHyprColor {*primary_hex};
    palette.secondary = secondary_hex.has_value() ? CHyprColor {*secondary_hex} : palette.primary;
    palette.valid = true;
    return palette;
}

optional<MatugenPalette> load_matugen_palette() {
    static std::filesystem::file_time_type cached_mtime {};
    static std::filesystem::path cached_path {};
    static optional<MatugenPalette> cached_palette;

    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = xdg != nullptr
        ? std::filesystem::path {xdg}
        : (home != nullptr ? std::filesystem::path {home} / ".config" : std::filesystem::path {});

    const std::vector<std::filesystem::path> candidates = {
        base / "hypr" / "dms" / "colors.lua",
        base / "hypr" / "hyprland" / "colors.lua",
        base / "hypr" / "colors.lua",
    };

    for (const auto& path : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            continue;

        const auto mtime = std::filesystem::last_write_time(path, ec);
        if (!cached_palette.has_value() || path != cached_path || mtime != cached_mtime) {
            cached_palette = load_matugen_palette_from_file(path);
            cached_path = path;
            cached_mtime = mtime;
        }
        return cached_palette;
    }

    cached_palette.reset();
    cached_path.clear();
    return std::nullopt;
}
} // namespace

HTLayoutBase::HTLayoutBase(VIEWID new_view_id) : view_id(new_view_id) {
    ;
}

void HTLayoutBase::on_move_swipe(Vector2D delta) {
    ;
}

WORKSPACEID HTLayoutBase::on_move_swipe_end() {
    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_in_direction(int x, int y, std::string& direction) {
    if (direction == "up") {
        y--;
    } else if (direction == "down") {
        y++;
    } else if (direction == "right") {
        x++;
    } else if (direction == "left") {
        x--;
    } else {
        return WORKSPACE_INVALID;
    }
    return get_ws_id_from_xy(x, y);
}

bool HTLayoutBase::on_mouse_axis(double delta) {
    return false;
}

bool HTLayoutBase::should_manage_mouse() {
    return true;
}

bool HTLayoutBase::should_render_window(PHLWINDOW window) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr || window == nullptr)
        return false;

    return ((should_render_window_t)(should_render_window_hook->m_original))(
        g_pHyprRenderer.get(),
        window,
        monitor
    );
}

float HTLayoutBase::drag_window_scale() {
    return 1.f;
}

void HTLayoutBase::init_position() {
    ;
}

void HTLayoutBase::build_overview_layout(HTViewStage stage) {
    ;
}

void HTLayoutBase::render() {
    CClearPassElement::SClearData data;
    data.color = CHyprColor {0};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CClearPassElement>(data));
}

void HTLayoutBase::render_workspace_label(WORKSPACEID workspace_id, PHLWORKSPACE workspace, const CBox& box) {
    const auto palette = load_matugen_palette();
    if (palette.has_value())
        render_workspace_label(workspace_id, workspace, box, *palette);
    else
        render_workspace_label(workspace_id, workspace, box, MatugenPalette {});
}

void HTLayoutBase::render_workspace_label(
    WORKSPACEID workspace_id,
    PHLWORKSPACE workspace,
    const CBox& box,
    const MatugenPalette& palette
) {
    if (!HTConfig::value<Config::BOOL>("labels:display_label"))
        return;
    if (box.w < 1.f || box.h < 1.f)
        return;

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return;

    const bool use_matugen = HTConfig::value<Config::BOOL>("labels:matugen");
    const int font_size = HTConfig::value<Config::INTEGER>("labels:font_size");
    const int text_opacity =
        std::clamp((int)HTConfig::value<Config::INTEGER>("labels:text_opacity"), 0, 100);
    if (text_opacity <= 0)
        return;

    const std::string font = HTConfig::value<Config::STRING>("labels:font");
    const float pad = 8.f * monitor->m_scale;
    const int max_width = std::max(1, (int)std::floor(box.w - pad * 2.f));
    const std::string text_color_raw = HTConfig::value<Config::STRING>("labels:text_color");
    std::string text = std::to_string(workspace_id);
    if (workspace != nullptr && !workspace->m_name.empty())
        text = workspace->m_name;
    CHyprColor text_color = resolve_label_color(
        text_color_raw,
        CHyprColor {1.f, 1.f, 1.f, 1.f},
        palette.primary,
        use_matugen,
        palette
    );
    text_color = text_color.modifyA(text_color.a * (text_opacity / 100.f));

    const auto tex = g_pHyprRenderer->renderText(
        text,
        text_color,
        font_size,
        false,
        font,
        max_width
    );
    if (tex == nullptr || !tex->ok())
        return;

    const Vector2D content_size = tex->m_size;
    const Vector2D label_size = content_size + Vector2D {pad * 2.f, pad * 2.f};
    const LabelPos pos = parse_label_pos(HTConfig::value<Config::STRING>("labels:position"));
    const Vector2D origin = label_origin(box, pos, label_size, pad);
    const CBox label_box = {box.pos() + origin, label_size};

    if (HTConfig::value<Config::BOOL>("labels:background")) {
        const int background_opacity =
            std::clamp((int)HTConfig::value<Config::INTEGER>("labels:background_opacity"), 0, 100);
        if (background_opacity > 0) {
            const std::string background_color = HTConfig::value<Config::STRING>("labels:background_color");
            CHyprColor color = resolve_label_color(
                background_color,
                CHyprColor {HTConfig::value<Config::INTEGER>("bg_color")}.stripA(),
                palette.secondary,
                use_matugen,
                palette
            ).stripA();
            color = color.modifyA(color.a * (background_opacity / 100.f));

            CRectPassElement::SRectData rect;
            rect.box = label_box;
            rect.color = color;
            rect.round = std::max(2, font_size / 4);
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRectPassElement>(rect));
        }
    }

    CTexPassElement::SRenderData text_data;
    text_data.tex = tex;
    text_data.box = {label_box.pos() + Vector2D {pad, pad}, content_size};
    text_data.damage = {0, 0, (int)monitor->m_transformedSize.x, (int)monitor->m_transformedSize.y};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CTexPassElement>(std::move(text_data)));
}

const std::string CLEAR_PASS_ELEMENT_NAME = "CClearPassElement";

void HTLayoutBase::post_render() {
    bool first = true;
    std::erase_if(g_pHyprRenderer->m_renderPass.m_passElements, [&first](const auto& e) {
        bool res = e.element->passName() == CLEAR_PASS_ELEMENT_NAME && !first;
        first = false;
        return res;
    });
    g_pHyprRenderer->m_renderPass.add(makeUnique<HTPassElement>());
    // g_pHyprOpenGL->setDamage(CRegion {CBox {0, 0, INT32_MAX, INT32_MAX}});
}

PHLMONITOR HTLayoutBase::get_monitor() {
    const auto par_view = ht_manager->get_view_from_id(view_id);
    if (par_view == nullptr)
        return nullptr;
    return par_view->get_monitor();
}

WORKSPACEID HTLayoutBase::get_ws_id_from_global(Vector2D pos) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return WORKSPACE_INVALID;

    if (!monitor->logicalBox().containsPoint(pos))
        return WORKSPACE_INVALID;

    Vector2D relative_pos = (pos - monitor->m_position) * monitor->m_scale;
    for (const auto& [id, layout] : overview_layout)
        if (layout.box.containsPoint(relative_pos))
            return id;

    return WORKSPACE_INVALID;
}

WORKSPACEID HTLayoutBase::get_ws_id_from_xy(int x, int y) {
    for (const auto& [id, layout] : overview_layout)
        if (layout.x == x && layout.y == y)
            return id;

    return WORKSPACE_INVALID;
}

CBox HTLayoutBase::get_global_window_box(PHLWINDOW window, WORKSPACEID workspace_id) {
    if (window == nullptr)
        return {};

    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    const PHLWORKSPACE workspace = g_pCompositor->getWorkspaceByID(workspace_id);
    if (workspace == nullptr || workspace->m_monitor != monitor)
        return {};

    const CBox ws_window_box = window->getWindowMainSurfaceBox();

    const Vector2D top_left =
        local_ws_unscaled_to_global(ws_window_box.pos() - monitor->m_position, workspace->m_id);
    const Vector2D bottom_right = local_ws_unscaled_to_global(
        ws_window_box.pos() + ws_window_box.size() - monitor->m_position,
        workspace->m_id
    );

    return {top_left, bottom_right - top_left};
}

CBox HTLayoutBase::get_global_ws_box(WORKSPACEID workspace_id) {
    const CBox scaled_ws_box = overview_layout[workspace_id].box;
    const Vector2D top_left = local_ws_scaled_to_global(scaled_ws_box.pos(), workspace_id);
    const Vector2D bottom_right =
        local_ws_scaled_to_global(scaled_ws_box.pos() + scaled_ws_box.size(), workspace_id);
    return {top_left, bottom_right - top_left};
}

Vector2D HTLayoutBase::global_to_local_ws_unscaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos -= monitor->m_position;
    pos *= monitor->m_scale;
    pos -= workspace_box.pos();
    pos /= monitor->m_scale;
    pos /= workspace_box.w / monitor->m_transformedSize.x;
    return pos;
}

Vector2D HTLayoutBase::global_to_local_ws_scaled(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos = global_to_local_ws_unscaled(pos, workspace_id);
    pos *= monitor->m_scale;
    return pos;
}

Vector2D HTLayoutBase::local_ws_unscaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    CBox workspace_box = overview_layout[workspace_id].box;
    if (workspace_box.empty())
        return {};
    pos *= workspace_box.w / monitor->m_transformedSize.x;
    pos *= monitor->m_scale;
    pos += workspace_box.pos();
    pos /= monitor->m_scale;
    pos += monitor->m_position;
    return pos;
}

Vector2D HTLayoutBase::local_ws_scaled_to_global(Vector2D pos, WORKSPACEID workspace_id) {
    const PHLMONITOR monitor = get_monitor();
    if (monitor == nullptr)
        return {};

    pos /= monitor->m_scale;
    return local_ws_unscaled_to_global(pos, workspace_id);
}
