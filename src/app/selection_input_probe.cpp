#include "app_input.h"
#include "app_commands.h"
#include "app_navigation.h"
#include "app_sidebar_refresh.h"
#include "app_runtime.h"
#include <algorithm>
#include <cstdio>

using namespace pulse;
namespace {
template<class Check>
void CheckFilteredRanges(Check check) {
    // In-memory rows only: validate the same path list consumed by operations,
    // without dispatching delete/move or touching files/preferences.
    for (bool descending : {false, true}) {
        for (int mode = 0; mode < 5; ++mode) {
            app::Pane pane;
            auto& tab = pane.view;
            tab.current_path = L"C:\\pulse-selection-fixture";
            auto entries = std::make_shared<std::vector<fs::DirEntry>>(3);
            const wchar_t* names[] = {L"a.keep", L"b.secret", L"c.keep"};
            for (int i = 0; i < 3; ++i) {
                auto& entry = (*entries)[i];
                entry.name = names[i];
                entry.full_path = tab.current_path + L"\\" + names[i];
                entry.size = i == 1 ? 1000 : 10;
                entry.attrs = mode == 4 && i == 1 ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL;
            }
            if (descending) std::reverse(entries->begin(), entries->end());
            tab.SetSnapshot(entries);
            app::PlacesCatalog places;
            places.persist = false;
            app::ColorTag tag;
            tag.name = L"keep";
            tag.paths = {(*entries)[0].full_path, (*entries)[2].full_path};
            places.tags.push_back(std::move(tag));
            places.TagsReordered();
            if (mode == 0) tab.filter_text = L"*.keep";
            if (mode == 1) tab.filter_text = L"#keep";
            if (mode == 2 || mode == 3) {
                tab.compare_marks = std::make_shared<const std::vector<uint8_t>>(std::initializer_list<uint8_t>{1, 0, 1});
                tab.compare_diff_only = true;
            }
            if (mode == 3) tab.filter_text = L"*.keep #keep";
            ui::PaneViewModel view;
            app::FillPaneViewModel(view, pane, &places);
            check(view.EntryCount() == 2 && view.SourceIndex(0) == 0 && view.SourceIndex(1) == 2,
                  "filter pipeline exposes only expected rows (name/tag/diff/combined/hidden, ascending/descending)");
            for (bool reverse : {false, true}) {
                const int from = reverse ? 2 : 0;
                const int to = reverse ? 0 : 2;
                tab.SelectOnly(from);
                tab.SelectRange(from, to, &view);
                auto paths = SelectedFullPaths(tab);
                auto expected = std::vector<std::wstring>{(*entries)[0].full_path, (*entries)[2].full_path};
                std::sort(paths.begin(), paths.end());
                std::sort(expected.begin(), expected.end());
                uint64_t bytes = 0;
                int files = 0, folders = 0;
                tab.SelectionSizeSummary(&bytes, &files, &folders);
                check(paths == expected && tab.SelectedCount() == 2 && bytes == 20 && files == 2 && folders == 0,
                      "forward/reverse Shift range exports only visible operation paths and matching status totals");
                tab.SelectOnly(from);
                tab.MoveFocus(to, true, &view);
                check(tab.SelectedIndices() == std::vector<int>({0, 2}) && tab.selected_index == to,
                      "keyboard focus extension uses the displayed range");
            }
            tab.SelectOnly(1);
            tab.MoveFocus(2, true, &view);
            check(tab.SelectedIndices() == std::vector<int>({2}) && tab.selection_anchor == 2,
                  "removed anchor restarts range at visible target");
        }
    }
    app::Pane pane;
    auto entries = std::make_shared<std::vector<fs::DirEntry>>(3);
    for (auto& entry : *entries) entry.attrs = FILE_ATTRIBUTE_NORMAL;
    pane.view.SetSnapshot(entries);
    ui::PaneViewModel view;
    app::FillPaneViewModel(view, pane);
    auto groups = std::make_shared<ui::ListGroups>(3);
    for (int i = 0; i < 3; ++i) { (*groups)[i].first = i; (*groups)[i].count = 1; }
    (*groups)[1].collapsed = true;
    view.groups = groups;
    view.view_mode = ui::ViewMode::Details;
    pane.view.SelectRange(0, 2, &view);
    check(pane.view.SelectedIndices() == std::vector<int>({0, 2}), "range excludes collapsed group members");
    pane.view.SelectOnly(1);
    pane.view.MoveFocus(2, true, &view);
    check(pane.view.SelectedIndices() == std::vector<int>({2}), "collapsed anchor restarts range");
    view.filter_map = std::make_shared<const ui::PaneViewModel::FilterMap>();
    pane.view.SelectRange(0, 2, &view);
    check(pane.view.SelectedCount() == 0, "empty filtered view cannot select source rows");
}
}
// Runs only in an explicitly isolated --shot test instance with fixture files.
int RunSelectionInputProbe(AppState& s, const wchar_t* output) {
    FILE* log = nullptr;
    if (_wfopen_s(&log, output, L"w") || !log) return 2;
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        std::fprintf(log, "[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(log);
        failures += !ok;
    };
    CheckFilteredRanges(check);
    if (GetEnvironmentVariableW(L"PULSE_TEST_FILTERED_SELECTION_ONLY", nullptr, 0)) {
        extern int RunNavigationAuditProbe(AppState&, FILE*);
        failures += RunNavigationAuditProbe(s, log);
        std::fclose(log);
        return failures ? 1 : 0;
    }
    const auto pump_sidebar = [&](auto ready) {
        const auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline) {
            TickSidebarRefresh(s, GetTickCount64());
            if (ready()) return true;
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        return false;
    };
    check(pump_sidebar([&] { return !s.sidebar.drives.empty(); }), "sidebar loads asynchronously after worker startup");
    if (!s.sidebar.drives.empty()) {
        s.sidebar.drives.front().detail = L"stale-capacity";
        RequestSidebarRefresh(s);
        check(pump_sidebar([&] { return s.sidebar.drives.front().detail != L"stale-capacity"; }),
              "background capacity request replaces stale display without rebuilding sidebar");
    }
    auto* tab = ActiveTab(s);
    if (!tab || !tab->snapshot || tab->EntryCount() < 3) { std::fclose(log); return 3; }
    tab->ClearSelection();
    SetFocus(s.hwnd);
    SendMessageW(s.hwnd, WM_CHAR, L'a', 0);
    check(tab->selected_index >= 0 && tab->EntryAt(tab->selected_index).name == L"apple.txt",
          "WM_CHAR selects first matching fixture");
    SendMessageW(s.hwnd, WM_CHAR, L'a', 0);
    check(tab->selected_index >= 0 && tab->EntryAt(tab->selected_index).name == L"apricot.txt",
          "WM_CHAR cycles to next matching fixture");
    ToggleQuickPreview(s);
    const auto original = s.quickPreview.item().path;
    check(s.quickPreview.visible() && original.find(L"apricot.txt") != std::wstring::npos,
          "preview opens selected fixture");
    SetFocus(s.hwnd);
    s.listTypeAhead = {};
    SendMessageW(s.hwnd, WM_CHAR, L'b', 0);
    const auto now = GetTickCount64();
    TickQuickPreviewSelection(s, now);
    check(s.quickPreview.item().path == original, "selection update is debounced");
    TickQuickPreviewSelection(s, now + 121);
    check(s.quickPreview.item().path.find(L"banana.txt") != std::wstring::npos,
          "open preview follows the newly selected file");
    ui::QuickPreviewItem internal = s.quickPreview.item();
    internal.path = original;
    internal.name = L"apricot.txt";
    s.quickPreview.Update(internal);
    TickQuickPreviewSelection(s, now + 300);
    check(s.quickPreview.item().path == original, "unchanged list selection preserves preview navigation");
    tab->ClearSelection();
    TickQuickPreviewSelection(s, now + 400);
    TickQuickPreviewSelection(s, now + 521);
    check(s.quickPreview.visible(), "empty selection keeps the preview accessible");
    s.quickPreview.Close();
    std::fclose(log);
    return failures ? 1 : 0;
}
