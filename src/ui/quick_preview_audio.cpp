// Quick Look audio preview: cover art, tags and a waveform above the shared
// playback bar. Playback itself is the MFPlay VideoPreview (audio-only files
// never show its child window); the waveform comes from AudioWaveform.
#include "quick_preview_window.h"
#include "../ipc/preview_protocol.h"
#include <d2d1helper.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace pulse::ui {
namespace {
bool Inside(const D2D1_RECT_F& rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right &&
        point.y >= rect.top && point.y < rect.bottom;
}

std::wstring PropertyValue(const std::vector<PreviewProperty>& properties, const wchar_t* label) {
    for (const auto& property : properties)
        if (property.label == label) return property.value;
    return {};
}

// Centered single-line text; restores the shared format's alignment afterwards.
void DrawCentered(ID2D1DeviceContext* dc, IDWriteTextFormat* format, const std::wstring& text,
                  const D2D1_RECT_F& rect, ID2D1Brush* brush) {
    if (!format || text.empty()) return;
    const auto text_alignment = format->GetTextAlignment();
    const auto paragraph_alignment = format->GetParagraphAlignment();
    const auto wrapping = format->GetWordWrapping();
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    dc->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, rect, brush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP);
    format->SetTextAlignment(text_alignment);
    format->SetParagraphAlignment(paragraph_alignment);
    format->SetWordWrapping(wrapping);
}
}  // namespace

bool QuickPreviewWindow::IsAudioPreview() const {
    return video_.active() && VideoPreview::IsAudio(item_.path);
}

bool QuickPreviewWindow::AudioMouseDown(POINT client) {
    if (!IsAudioPreview() || !Inside(audio_wave_rect_, client)) return false;
    const float width = (std::max)(1.0f, audio_wave_rect_.right - audio_wave_rect_.left);
    video_.Seek(std::clamp((static_cast<float>(client.x) - audio_wave_rect_.left) / width, 0.0f, 1.0f));
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void QuickPreviewWindow::DrawAudio(ID2D1DeviceContext* dc, const VideoPreview::State& state,
                                   ID2D1SolidColorBrush* text_brush,
                                   ID2D1SolidColorBrush* secondary_brush) {
    if (!dc) return;
    const D2D1_RECT_F content = ContentRect();
    const float w = content.right - content.left;
    const float h = content.bottom - content.top;
    const float pad = 24.0f * scale_;
    const float title_h = 28.0f * scale_;
    const float line_h = 20.0f * scale_;
    const float wave_h = 52.0f * scale_;
    const float text_block = 12.0f * scale_ + title_h + line_h * 2.0f + 16.0f * scale_ + wave_h;
    const float cover = std::clamp((std::min)(w - pad * 2.0f, h - pad * 2.0f - text_block),
                                   0.0f, 240.0f * scale_);
    const bool show_cover = cover >= 64.0f * scale_;
    const float total = (show_cover ? cover : 0.0f) + text_block;
    float y = content.top + (std::max)(pad * 0.5f, (h - total) * 0.5f);
    const float cx = content.left + w * 0.5f;

    ComPtr<ID2D1SolidColorBrush> accent;
    dc->CreateSolidColorBrush(dark_ ? D2D1::ColorF(0x60CDFF) : D2D1::ColorF(0x005FB8), &accent);

    // --- Cover: embedded album art through the preview host, else a tile. ---
    if (show_cover) {
        const D2D1_RECT_F rect = D2D1::RectF(cx - cover * 0.5f, y, cx + cover * 0.5f, y + cover);
        const uint32_t pixels = ipc::BucketPreviewPixelSize(static_cast<uint32_t>(cover));
        const PreviewDrawResult result = thumbnails_.Draw(dc, rect, item_.path, item_.attrs,
            pixels, generation_, item_.modified, item_.size, 1.0f, nullptr, nullptr, nullptr, true);
        if (result != PreviewDrawResult::Bitmap) {
            const D2D1_GRADIENT_STOP stops[] = {
                {0.0f, D2D1::ColorF(0xF6C26B)}, {0.55f, D2D1::ColorF(0xD9655B)},
                {1.0f, D2D1::ColorF(0x6B4AA8)}};
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> gradient;
            dc->CreateGradientStopCollection(stops, 3, &collection);
            if (collection.get())
                dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(
                    D2D1::Point2F(rect.left, rect.top), D2D1::Point2F(rect.right, rect.bottom)),
                    collection.get(), &gradient);
            const float r = 12.0f * scale_;
            if (gradient.get())
                dc->FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), gradient.get());
            // Vector eighth note, centered.
            ComPtr<ID2D1SolidColorBrush> white;
            dc->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, 0.92f), &white);
            if (white.get()) {
                const float u = cover / 10.0f;
                const float nx = cx - u * 0.6f, ny = y + cover * 0.66f;
                dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(nx, ny), u * 1.05f, u * 0.8f), white.get());
                const float stem_x = nx + u * 0.95f;
                dc->FillRectangle(D2D1::RectF(stem_x - u * 0.16f, y + cover * 0.26f, stem_x + u * 0.16f, ny),
                                  white.get());
                dc->FillRectangle(D2D1::RectF(stem_x, y + cover * 0.26f, stem_x + u * 1.3f,
                                              y + cover * 0.26f + u * 0.45f), white.get());
            }
        }
        y += cover;
    }
    y += 12.0f * scale_;

    // --- Tags. ---
    std::vector<PreviewProperty> properties;
    thumbnails_.Properties(item_.path, item_.attrs, generation_, item_.modified, item_.size,
                           properties);
    std::wstring title = PropertyValue(properties, L"标题");
    if (title.empty()) {
        title = item_.name;
        if (const size_t dot = title.find_last_of(L'.'); dot != std::wstring::npos && dot > 0)
            title.resize(dot);
    }
    std::wstring subtitle = PropertyValue(properties, L"艺术家");
    if (const std::wstring album = PropertyValue(properties, L"专辑"); !album.empty())
        subtitle += (subtitle.empty() ? L"" : L" \x00B7 ") + album;
    std::wstring tech;
    if (const size_t dot = item_.name.find_last_of(L'.'); dot != std::wstring::npos) {
        tech = item_.name.substr(dot + 1);
        for (auto& c : tech) c = static_cast<wchar_t>(std::towupper(c));
    }
    for (const wchar_t* label : {L"比特率", L"采样率"})
        if (const std::wstring value = PropertyValue(properties, label); !value.empty())
            tech += (tech.empty() ? L"" : L" \x00B7 ") + value;
    const D2D1_RECT_F text_rect = D2D1::RectF(content.left + pad, y, content.right - pad, y + title_h);
    DrawCentered(dc, compositor_.HeaderFormat(), title, text_rect, text_brush);
    y += title_h;
    DrawCentered(dc, compositor_.TextFormat(), subtitle,
                 D2D1::RectF(text_rect.left, y, text_rect.right, y + line_h), secondary_brush);
    y += line_h;
    DrawCentered(dc, compositor_.SmallFormat(), tech,
                 D2D1::RectF(text_rect.left, y, text_rect.right, y + line_h), secondary_brush);
    y += line_h + 16.0f * scale_;

    // --- Waveform with the played part highlighted. ---
    const float wave_w = (std::min)(w - pad * 2.0f, 560.0f * scale_);
    const D2D1_RECT_F wave = D2D1::RectF(cx - wave_w * 0.5f, y, cx + wave_w * 0.5f, y + wave_h);
    audio_wave_rect_ = wave;
    const float played = state.duration > 0
        ? std::clamp(static_cast<float>(state.position) / static_cast<float>(state.duration), 0.0f, 1.0f)
        : 0.0f;
    ComPtr<ID2D1SolidColorBrush> rest;
    dc->CreateSolidColorBrush(dark_ ? D2D1::ColorF(0xFFFFFF, 0.28f) : D2D1::ColorF(0x000000, 0.22f),
                              &rest);
    if (!accent.get() || !rest.get() || wave_w <= 8.0f) return;
    std::vector<float> peaks;
    float progress = 0.0f;
    bool failed = true;
    if (!waveform_.Snapshot(peaks, progress, failed)) failed = true;
    const float mid = (wave.top + wave.bottom) * 0.5f;
    if (failed || peaks.empty()) {
        const float t = 1.5f * scale_;
        dc->FillRectangle(D2D1::RectF(wave.left, mid - t, wave.right, mid + t), rest.get());
        dc->FillRectangle(D2D1::RectF(wave.left, mid - t, wave.left + wave_w * played, mid + t),
                          accent.get());
        return;
    }
    const float step = 3.5f * scale_;
    const float bar = 2.0f * scale_;
    const size_t bars = (std::max)(size_t{1}, static_cast<size_t>(wave_w / step));
    for (size_t i = 0; i < bars; ++i) {
        const size_t from = i * peaks.size() / bars;
        const size_t to = (std::max)(from + 1, (i + 1) * peaks.size() / bars);
        float peak = 0.0f;
        for (size_t k = from; k < to && k < peaks.size(); ++k) peak = (std::max)(peak, peaks[k]);
        const float bh = (std::max)(2.0f * scale_, peak * wave_h);
        const float x = wave.left + static_cast<float>(i) * step;
        const bool done = static_cast<float>(i) / static_cast<float>(bars) < played;
        dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, mid - bh * 0.5f, x + bar,
                                                               mid + bh * 0.5f), bar * 0.5f, bar * 0.5f),
                                 done ? accent.get() : rest.get());
    }
}

}  // namespace pulse::ui
