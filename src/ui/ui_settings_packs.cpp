#include "../common/windows_compat.h"
#include "ui_renderer.h"
#include "ui_renderer_internal.h"
#include "../common/localization.h"

namespace pulse::ui {

// Settings > 预览增强包 (page 5). Geometry: LayoutSettingsPacks; texts:
// pack_text (settings_layout_sections.h); clicks: SettingsPackAction.
void MainRenderer::DrawSettingsPacks(const WindowViewModel& vm, const D2D1_RECT_F& rect, const Theme& theme) {
    using H = HitTestResult;
    auto* dc = compositor_->Dc();
    const auto lay = MakeSettingsLayout(vm, rect, scale_, title_bar_height_, status_height_, &painter_);
    const float s = scale_;
    auto text = [&](std::wstring_view value, D2D1_RECT_F r, bool is_small = false) {
        painter_.DrawText(value, r, is_small ? compositor_->SmallFormat() : compositor_->TextFormat(),
                          is_small ? theme.text_secondary : theme.text);
    };
    auto card = [&](D2D1_RECT_F r) {
        if (r.bottom <= r.top) return;
        MakeBrush(dc, WithAlpha(theme.surface_card, card_alpha_), brFillInput_);
        MakeBrush(dc, theme.stroke_card, brStrokeCard_);
        dc->FillRoundedRectangle(D2D1::RoundedRect(r, 8*s, 8*s), brFillInput_.get());
        dc->DrawRoundedRectangle(D2D1::RoundedRect(r, 8*s, 8*s), brStrokeCard_.get(), 1);
    };
    auto divider_below = [&](D2D1_RECT_F r) {
        if (r.bottom <= r.top) return;
        MakeBrush(dc, theme.stroke_divider, brStrokeDivider_);
        FillRect(dc, brStrokeDivider_.get(), r.left + 16*s, r.bottom, r.right - r.left - 32*s, 1);
    };
    auto hovered = [&](PackAction action) {
        return IsHovered(vm, H::SettingsPackAction, static_cast<int>(action));
    };
    auto hover_row = [&](D2D1_RECT_F r, PackAction action) {
        if (r.bottom <= r.top || !hovered(action)) return;
        MakeBrush(dc, theme.fill_hover, brFillHover_);
        FillRoundedRect(dc, brFillHover_.get(), r.left + 4*s, r.top + 2*s,
                        r.right - r.left - 8*s, r.bottom - r.top - 4*s, 4*s);
    };
    auto button = [&](D2D1_RECT_F r, std::wstring_view label, PackAction action,
                      fluent::ButtonKind kind = fluent::ButtonKind::Standard) {
        if (r.right <= r.left) return;
        fluent::ControlState state{};
        state.hovered = hovered(action);
        painter_.DrawButton({r, label, {}, kind, state});
    };
    auto toggle = [&](D2D1_RECT_F bounds, bool on, PackAction action) {
        fluent::ControlState state{};
        state.checked = on;
        state.hovered = hovered(action);
        painter_.DrawSwitch(bounds, L"", state);
    };
    auto row_switch = [&](D2D1_RECT_F row) {
        const float cy = (row.top + row.bottom) * 0.5f;
        return D2D1::RectF(row.right - 16*s - 42*s, cy - 16*s, row.right - 16*s, cy + 16*s);
    };
    auto setting = [&](D2D1_RECT_F r, const wchar_t* icon, std::wstring_view title, std::wstring_view desc) {
        const float right = r.right - 72*s;
        DrawIconText(r.left + 16*s, r.top + 20*s, 24*s, 24*s, icon, L"", theme.text_secondary, 0.85f);
        text(title, D2D1::RectF(r.left + 54*s, r.top + 10*s, right, r.top + 34*s));
        text(desc, D2D1::RectF(r.left + 54*s, r.top + 35*s, right, r.top + 56*s), true);
    };

    // Summary: count, disk use and location.
    {
        const auto& r = lay.pack_summary;
        card(r);
        DrawIconText(r.left + 16*s, r.top + 24*s, 24*s, 24*s, L"\xE7B8", L"", theme.text_secondary, 0.85f);
        const float right = lay.pack_open.left - 12*s;
        text(pack_text::Summary(vm), D2D1::RectF(r.left + 54*s, r.top + 14*s, right, r.top + 38*s));
        text(vm.settings_pack_root, D2D1::RectF(r.left + 54*s, r.top + 38*s, right, r.top + 58*s), true);
        button(lay.pack_open, pack_text::OpenFolder(), PackAction::OpenFolder);
    }

    // Media: the FFmpeg pack.
    text(pack_text::Media(), lay.pack_media_section);
    {
        const auto& r = lay.pack_card;
        card(r);
        const float text_left = r.left + 54*s, inner_right = r.right - 16*s;
        DrawIconText(r.left + 16*s, r.top + 18*s, 24*s, 24*s, L"\xE714", L"", theme.accent, 0.9f);
        text(pack_text::MediaTitle(), D2D1::RectF(text_left, r.top + 12*s, lay.pack_badge.left - 12*s, r.top + 36*s));
        const auto badge = pack_text::MediaBadge(vm);
        fluent::BadgeSpec spec{};
        spec.bounds = lay.pack_badge;
        spec.text = badge.text;
        spec.kind = badge.kind;
        painter_.DrawBadge(spec);
        if (pack_text::ShowsEnable(vm)) toggle(lay.pack_enable, vm.settings_pack_ffmpeg_enabled, PackAction::Enable);
        painter_.DrawWrappedCaption(pack_text::MediaDesc(), D2D1::Point2F(text_left, r.top + 40*s),
                                    inner_right - text_left, theme.text_secondary);
        text(pack_text::MediaMeta(), D2D1::RectF(text_left, lay.pack_primary.top + 6*s,
             lay.pack_primary.left - 12*s, lay.pack_primary.bottom - 6*s), true);
        const bool installed = vm.settings_pack_media_installed;
        button(lay.pack_primary, pack_text::Primary(vm), installed ? PackAction::Remove : PackAction::Install,
               installed ? fluent::ButtonKind::Standard : fluent::ButtonKind::Primary);
        if (lay.pack_notice.bottom > lay.pack_notice.top) {
            const auto& n = lay.pack_notice;
            MakeBrush(dc, theme.fill_hover, brFillHover_);
            FillRoundedRect(dc, brFillHover_.get(), n.left, n.top, n.right - n.left, n.bottom - n.top, 6*s);
            MakeBrush(dc, theme.accent, brFillSelected_);
            FillRoundedRect(dc, brFillSelected_.get(), n.left, n.top + 6*s, 3*s, n.bottom - n.top - 12*s, 1.5f*s);
            painter_.DrawWrappedCaption(vm.settings_pack_notice, D2D1::Point2F(n.left + 12*s, n.top + 6*s),
                                        n.right - n.left - 12*s, theme.text);
        }
    }

    // Advanced: an FFmpeg of the user's own; what uninstalling Pulse does.
    text(pack_text::Advanced(), lay.pack_advanced_section);
    card(lay.pack_group);
    hover_row(lay.pack_custom_row, PackAction::UseCustom);
    setting(lay.pack_custom_row, L"\xE943", pack_text::CustomTitle(), pack_text::CustomDesc());
    toggle(row_switch(lay.pack_custom_row), vm.settings_pack_use_custom, PackAction::UseCustom);
    divider_below(lay.pack_custom_row);
    if (lay.pack_path_row.bottom > lay.pack_path_row.top) {
        const auto& r = lay.pack_path_row;
        const float right = (lay.pack_detect.right > lay.pack_detect.left ? lay.pack_detect.left
                                                                           : lay.pack_browse.left) - 12*s;
        const std::wstring path = vm.settings_pack_custom_path.empty()
            ? std::wstring(pack_text::NoCustom()) : vm.settings_pack_custom_path;
        const bool active = vm.settings_pack_use_custom && vm.settings_pack_ffmpeg == 2;
        if (vm.settings_pack_detected_path.empty()) {
            painter_.DrawText(path, D2D1::RectF(r.left + 54*s, r.top + 14*s, right, r.top + 38*s),
                              compositor_->SmallFormat(), active ? theme.text : theme.text_secondary);
        } else {
            painter_.DrawText(path, D2D1::RectF(r.left + 54*s, r.top + 12*s, right, r.top + 34*s),
                              compositor_->SmallFormat(), active ? theme.text : theme.text_secondary);
            text(pack_text::Found(vm.settings_pack_detected_path),
                 D2D1::RectF(r.left + 54*s, r.top + 34*s, right, r.top + 54*s), true);
        }
        button(lay.pack_detect, pack_text::UseDetected(), PackAction::UseDetected);
        button(lay.pack_browse, pack_text::Browse(), PackAction::Browse);
        divider_below(r);
    }
    hover_row(lay.pack_remove_row, PackAction::RemoveOnUninstall);
    setting(lay.pack_remove_row, L"\xE74D", pack_text::RemoveTitle(), pack_text::RemoveDesc());
    toggle(row_switch(lay.pack_remove_row), vm.settings_pack_remove_on_uninstall, PackAction::RemoveOnUninstall);

    painter_.DrawWrappedCaption(pack_text::Note(), D2D1::Point2F(lay.pack_note.left + 4*s, lay.pack_note.top),
                                lay.pack_note.right - lay.pack_note.left - 8*s, theme.text_secondary);
}

} // namespace pulse::ui
