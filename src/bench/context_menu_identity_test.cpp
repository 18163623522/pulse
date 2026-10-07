#include "../app/context_menu_prefs.h"
#include <windows.h>
#include <filesystem>
#include <cstdio>

namespace { std::wstring fixture; }
namespace pulse::app { std::wstring GetPulseDataDir() { return fixture; } }

int wmain() {
    using namespace pulse;
    namespace fs = std::filesystem;
    const auto parent = fs::absolute(L"bench_data").lexically_normal();
    const auto base = parent / (L"context-menu-identity-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    fs::create_directories(parent);
    if (!fs::create_directory(base)) return 1;
    fixture = base.wstring();
    int failures = 0;
    auto check = [&](bool ok, const char* label) { printf("[%s] %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; };
    const std::wstring stable = L"pulse:quick-access";
    const auto category = ipc::CtxMenuCategory::Software;
    for (const auto label : {L"Pin to Quick access", L"固定到快速访问", L"固定到快速存取"}) {
        app::ContextMenuPrefs original;
        original.RecordSeen(stable, label, false, category, false);
        original.SetItemEnabled(stable, false);
        check(original.Save(), "save disabled stable command in private configuration");
        app::ContextMenuPrefs loaded;
        check(loaded.Load() && loaded.seen.size() == 1 && loaded.seen[0].key == stable &&
            !loaded.ItemEnabled(stable, category, false), "restart preserves the actual command choice and settings identity");
        loaded.SetItemEnabled(loaded.seen[0].key, true);
        check(loaded.Save(), "settings enable uses the same stable command key");
        app::ContextMenuPrefs restarted;
        check(restarted.Load() && restarted.ItemEnabled(stable, category, false) &&
            !restarted.RecordSeen(stable, L"固定到快速存取", false, category, false) && restarted.seen.size() == 1,
            "language change and another menu opening keep one functional settings row");
        for (int choice = 0; choice < 3; ++choice) {
            app::ContextMenuPrefs damaged;
            const auto alias = ipc::CatalogKey(label, false);
            damaged.RecordSeen(alias, label, false, category, false);
            damaged.SetItemEnabled(alias, false);
            if (choice != 0) {
                damaged.RecordSeen(stable, label, false, category, false);
                damaged.SetItemEnabled(stable, choice == 2);
                damaged.SetItemEnabled(alias, choice != 2);
            }
            app::ContextMenuPrefs repaired;
            check(repaired.FromJson(damaged.ToJson()) && repaired.seen.size() == 1 &&
                repaired.seen[0].key == stable && !repaired.item_enabled.contains(alias) &&
                repaired.ItemEnabled(stable, category, false) == (choice == 2),
                "repair old display alias; explicit stable override takes precedence");
            app::ContextMenuPrefs repeated;
            check(repeated.FromJson(repaired.ToJson()) && repeated.ToJson() == repaired.ToJson(),
                "repeated migration is idempotent");
        }
    }
    app::ContextMenuPrefs other;
    other.RecordSeen(L"pulse:future-command", L"Future translated label", false, category);
    const auto handler = ipc::HandlerCatalogKey(L"{00000000-0000-0000-0000-000000000001}");
    other.RecordSeen(handler, L"Handler", false, category, true);
    other.RecordSeen(L"v:New (N)", L"New (N)", false, category);
    other.RecordSeen(L"f: Open with ", L"Open with", true, category);
    other.SetItemEnabled(L"v:New (N)", false);
    other.MigrateSeenKeys();
    check(other.seen.size() == 4 && other.seen[0].key == L"pulse:future-command" && other.seen[1].key == handler,
        "other command and handler identities stay unchanged");
    check(other.seen[2].key == L"v:new" && other.seen[3].key == L"f:openwith" &&
        !other.ItemEnabled(L"v:new", category, false), "legacy display verbs and flyouts still normalize with their choices");
    app::ContextMenuPrefs com;
    com.RecordSeen(L"v:pintoquickaccess", L"Pin to Quick access", false, category, true);
    com.MigrateSeenKeys();
    check(com.seen[0].key == L"v:pintoquickaccess", "COM command with similar text is not reassigned to Pulse");
    if (base.parent_path() != parent || !base.filename().wstring().starts_with(L"context-menu-identity-")) return 1;
    std::error_code error; fs::remove_all(base, error);
    check(!error && !fs::exists(base), "private preferences fixture removed");
    return failures ? 1 : 0;
}
