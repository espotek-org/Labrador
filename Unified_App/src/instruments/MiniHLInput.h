// MiniHLInput.h - single-line highlightable input for Dear ImGui
// Requires: imgui.h and imgui_internal.h (for InputText scroll/cursor access)

#pragma once
#include "imgui.h"
#include "imgui_internal.h"
#include <string>
#include <vector>
#include <cctype>

struct MiniHLRule {
    std::string token;      // e.g. "sin", "==", "osc1"
    ImU32       color;      // IM_COL32(r,g,b,a)
    std::string tooltip;    // optional; shown on hover
    bool        whole_word = true;      // names true, symbols false
    bool        case_insensitive = true;
    bool allow_digit_before = false;
};

struct MiniHLToken {
    int start = 0, len = 0;
    ImU32 color = 0;
    const MiniHLRule* rule = nullptr;
};

static inline bool mini_is_word_char(char c) {
    unsigned char u = (unsigned char)c;
    return (u == '_') || (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

static inline bool mini_ieq(char a, char b) {
    return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
}

static inline bool mini_is_ident_char(char c) {
    unsigned char u = (unsigned char)c;
    return (u == '_') || (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}
static inline bool mini_is_alpha_or_underscore(char c) {
    unsigned char u = (unsigned char)c;
    return (u == '_') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

static int mini_scan_number(const std::string& s, int pos) {
    const int N = (int)s.size();
    if (pos >= N) return 0;
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };

    int i = pos;

    // optional sign if not glued to an identifier on the left
    extern bool mini_is_ident_char(char); // you already have this helper
    if ((s[i] == '+' || s[i] == '-') && (pos == 0 || !mini_is_ident_char(s[pos - 1]))) {
        i++;
        if (i >= N) return 0;
    }

    int digits_before = 0, digits_after = 0;

    while (i < N && is_digit(s[i])) { ++i; ++digits_before; }

    if (i < N && s[i] == '.') {
        ++i;
        while (i < N && is_digit(s[i])) { ++i; ++digits_after; }
    }

    if (digits_before == 0 && digits_after == 0) return 0;

    // exponent: e[+/-]?digits
    if (i < N && (s[i] == 'e' || s[i] == 'E')) {
        int j = i + 1;
        if (j < N && (s[j] == '+' || s[j] == '-')) ++j;
        int exp_digits = 0;
        while (j < N && is_digit(s[j])) { ++j; ++exp_digits; }
        if (exp_digits > 0) i = j; // accept exponent only if it has digits
    }

    return i - pos;
}

static bool mini_match_at(const std::string& s, int pos, const MiniHLRule& r) {
    const int n = (int)r.token.size();
    if (pos + n > (int)s.size()) return false;
    for (int i = 0; i < n; ++i) {
        if (r.case_insensitive ? !mini_ieq(s[pos + i], r.token[i]) : (s[pos + i] != r.token[i]))
            return false;
    }
    if (r.whole_word) {
        bool left_ok = (pos == 0);
        if (!left_ok) {
            char L = s[pos - 1];
            // If we allow a digit/paren before (for implicit multiplication),
            // only block when the left char is alpha/_ (so "asin" blocks "sin")
            left_ok = r.allow_digit_before
                ? !mini_is_alpha_or_underscore(L)
                : !mini_is_ident_char(L); // strict: digits also block (for "pi","e")
        }
        int end = pos + n;
        bool right_ok = (end >= (int)s.size()) || !mini_is_ident_char(s[end]);
        if (!(left_ok && right_ok)) return false;
    }
    return true;
}

static void mini_tokenize(const std::string& s,
    const std::vector<MiniHLRule>& rules,
    ImU32 default_col,
    ImU32 number_col,  // 0 = no special color, still masks
    std::vector<MiniHLToken>& out)
{
    out.clear();
    int i = 0, N = (int)s.size();
    while (i < N) {
        // 1) numbers (mask punctuation inside, e.g., '1e-3')
        int nlen = mini_scan_number(s, i);
        if (nlen > 0) {
            ImU32 col = number_col ? number_col : default_col;
            out.push_back({ i, nlen, col, nullptr });
            i += nlen;
            continue;
        }

        // 2) your explicit rules (longest match wins)
        const MiniHLRule* best = nullptr; int best_len = 0;
        for (const auto& r : rules) {
            if (mini_match_at(s, i, r)) {
                int len = (int)r.token.size();
                if (len > best_len) { best = &r; best_len = len; }
            }
        }
        if (best) { out.push_back({ i, best_len, best->color, best }); i += best_len; continue; }

        // 3) default run
        int j = i + 1;
        while (j < N) {
            if (mini_scan_number(s, j) > 0) break;
            bool hit = false;
            for (const auto& r : rules) { if (mini_match_at(s, j, r)) { hit = true; break; } }
            if (hit) break;
            ++j;
        }
        out.push_back({ i, j - i, default_col, nullptr });
        i = j;
    }
}

static bool MiniHLInput(const char* label,
    std::string& buffer,
    const std::vector<MiniHLRule>& rules,
    ImU32 default_text_color = 0,      // 0 = use ImGuiCol_Text
    ImU32 bg_color = 0, // 0 = leave as is
    std::string hint = "")                
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    bool changed = false;

    // We�ll render InputText with transparent text, then paint our own colored text on top
    ImGui::PushID(label);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
    if (bg_color) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(bg_color));
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0)); // hide built-in text

    // Keep a stable buffer for InputText
    static std::string tmp;
    tmp = buffer; // InputText edits 'tmp'; we copy back if changed
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_AutoSelectAll;
    // Render the input
    changed = ImGui::InputTextWithHint("##mini_hl", hint.c_str(), &tmp, flags);

    ImGuiID id = ImGui::GetItemID();
    ImRect  r = ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
    bool    item_hovered = ImGui::IsItemHovered();

    // Access cursor & scroll from internal state for perfect alignment
    ImGuiInputTextState* state = ImGui::GetInputTextState(id);
    const float scroll_x = state ? state->Scroll.x : 0.0f; // 1.92: ScrollX became ImVec2 Scroll

    ImGui::PopStyleColor(); // text
    if (bg_color) ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Draw colored overlay
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 text_pos = { r.Min.x + ImGui::GetStyle().FramePadding.x,
                        r.Min.y + ImGui::GetStyle().FramePadding.y };

    ImU32 def_col = default_text_color ? default_text_color : ImGui::GetColorU32(ImGuiCol_Text);
    ImU32 num_col = ImU32(ImColor(float(181. / 255), float(206. / 255), float(168. / 255), float(1))); // <-- mask only; set to a color if you later want them highlighted
    std::vector<MiniHLToken> toks;
    mini_tokenize(tmp, rules, def_col, num_col, toks);

    // Clip to the input box
    dl->PushClipRect(r.Min, r.Max, true);

    // Draw tokens
    float x = text_pos.x - scroll_x;
    float y = text_pos.y;
    ImFont* font = ImGui::GetFont(); // 1.92: CalcTextSizeA is non-const (may bake glyphs lazily)
    const float  fs = ImGui::GetFontSize();

    int hover_token_idx = -1;
    if (!toks.empty()) {
        for (int ti = 0; ti < (int)toks.size(); ++ti) {
            const auto& t = toks[ti];
            const char* s = tmp.c_str() + t.start;
            const char* e = s + t.len;
            ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, s, e, nullptr);
            ImVec2 p = ImVec2(x, y);
            dl->AddText(font, fs, p, t.color, s, e);
            // hover detection
            if (item_hovered && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + sz.x, p.y + sz.y))) {
                hover_token_idx = ti;
            }
            x += sz.x;
        }
    }

    // Custom caret (since we hid default text, default caret is invisible)
    if (state) {
        const int cursor = state->GetCursorPos(); // byte index
        float cx = text_pos.x - scroll_x;
        if (cursor > 0) {
            ImVec2 w = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, tmp.c_str(), tmp.c_str() + cursor, nullptr);
            cx += w.x;
        }
        dl->AddLine(ImVec2(cx, r.Min.y + 2), ImVec2(cx, r.Max.y - 2),
            ImGui::GetColorU32(ImGuiCol_Text));
    }

    dl->PopClipRect();

    // Tooltip if hovered token has tooltip
    if (hover_token_idx >= 0) {
        const auto* rule = toks[hover_token_idx].rule;
        if (rule && !rule->tooltip.empty()) ImGui::SetTooltip("%s", rule->tooltip.c_str());
    }

    // Copy back if user edited
    if (changed && tmp != buffer) buffer = tmp;

    ImGui::PopID();
    return changed;
}
#pragma once
