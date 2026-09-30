#pragma once

#include "../ui/view_layout.h"
#include <map>
#include <optional>
#include <string>

namespace pulse::app {

// Normalized key for per-folder preferences; empty for virtual views and
// nonabsolute paths, which are never persisted.
std::wstring FolderPrefKey(const std::wstring& path);

class FolderViewPrefs {
public:
    std::optional<ui::ViewMode> Find(const std::wstring& path) const;
    bool Set(const std::wstring& path, ui::ViewMode mode);
    // Mode for folders without a saved choice.
    ui::ViewMode Default() const { return default_; }
    // "Apply to all folders": `mode` becomes the default, per-folder choices go.
    void ApplyToAll(ui::ViewMode mode);
    void Clear() { views_.clear(); default_ = ui::ViewMode::Details; }
    void AppendJson(std::wstring& out) const;
    void ReadJson(const std::wstring& json);

private:
    struct PathLess {
        bool operator()(const std::wstring& a, const std::wstring& b) const;
    };
    std::map<std::wstring, ui::ViewMode, PathLess> views_;
    ui::ViewMode default_ = ui::ViewMode::Details;
};

} // namespace pulse::app
