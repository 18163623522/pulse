// Exercise the production drag implementation with only the native modal loop
// replaced, so no mouse gesture or system clipboard is needed.
#include "../ui/drag_drop.h"
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

HRESULT WINAPI AuditNativeDrag(IDataObject*, IDropSource*, DWORD, DWORD*);
#define DoDragDrop AuditNativeDrag
#include "../ui/drag_drop.cpp"
#undef DoDragDrop

namespace {
int failures = 0;
int passed = 0;
int native_calls = 0;
std::vector<std::wstring> native_paths;
void Check(bool ok, const char* label) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (ok) ++passed; else ++failures;
}
std::string ReadBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}
}

HRESULT WINAPI AuditNativeDrag(IDataObject* data, IDropSource*, DWORD allowed, DWORD* effect) {
    ++native_calls;
    pulse::ui::ExtractHDropPaths(data, native_paths);
    *effect = allowed & DROPEFFECT_COPY;
    return DRAGDROP_S_DROP;
}

int wmain() {
    using namespace pulse::ui;
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (DWORD allowed : { DWORD(DROPEFFECT_NONE), DWORD(DROPEFFECT_COPY),
                           DWORD(DROPEFFECT_MOVE), DWORD(DROPEFFECT_COPY | DROPEFFECT_MOVE) }) {
        for (DWORD keys : { DWORD(0), DWORD(MK_CONTROL), DWORD(MK_SHIFT), DWORD(MK_CONTROL | MK_SHIFT) }) {
            for (bool cross_volume : { false, true }) {
                auto* data = FileDataObject::Create({ L"C:\\fixture\\source.txt" });
                unsigned over_calls = 0, drop_calls = 0;
                DWORD observed = 0;
                DropTargetCallbacks callbacks;
                const auto compute = [&](const std::vector<std::wstring>& paths, POINT,
                                         DWORD state, DWORD mask) {
                    observed = mask;
                    return ComputeDropEffect(state, paths.front(),
                        cross_volume ? L"D:\\target" : L"C:\\target", mask);
                };
                callbacks.drag_over = [&](const auto& paths, POINT point, DWORD state, DWORD mask) {
                    ++over_calls; return compute(paths, point, state, mask);
                };
                callbacks.drop = [&](const auto& paths, POINT point, DWORD state, DWORD mask) {
                    ++drop_calls; return compute(paths, point, state, mask);
                };
                auto* target = new WindowDropTarget(nullptr, std::move(callbacks));
                DWORD expected = allowed;
                if (allowed == (DROPEFFECT_COPY | DROPEFFECT_MOVE)) {
                    expected = keys == MK_SHIFT ? DROPEFFECT_MOVE :
                        (keys != 0 || cross_volume ? DROPEFFECT_COPY : DROPEFFECT_MOVE);
                }
                DWORD effect = allowed;
                target->DragEnter(data, keys, POINTL{}, &effect);
                const bool entered = effect == expected;
                effect = allowed;
                target->DragOver(keys, POINTL{}, &effect);
                const bool hovered = effect == expected;
                effect = allowed;
                target->Drop(data, keys, POINTL{}, &effect);
                const bool callbacks_correct = allowed
                    ? observed == allowed && over_calls == 2 && drop_calls == 1
                    : over_calls == 0 && drop_calls == 0;
                Check(entered && hovered && effect == expected &&
                      data->PerformedEffect() == expected && callbacks_correct,
                      "source effect mask survives enter/over/drop and modifiers");
                target->Release();
                data->Release();
            }
        }
    }
    {
        auto* data = FileDataObject::Create({L"C:\\fixture\\source.txt"});
        DWORD observed = 0;
        DropTargetCallbacks callbacks;
        callbacks.drop = [&](const auto&, POINT, DWORD, DWORD mask) {
            observed = mask; return DROPEFFECT_MOVE;
        };
        auto* target = new WindowDropTarget(nullptr, std::move(callbacks));
        DWORD effect = DROPEFFECT_COPY;
        target->Drop(data, 0, POINTL{}, &effect);
        Check(observed == DROPEFFECT_COPY && effect == DROPEFFECT_NONE &&
              data->PerformedEffect() == DROPEFFECT_NONE,
              "forbidden callback effect is not reported to source");
        target->Release();
        data->Release();
    }
    {
        auto* data = FileDataObject::Create({L"C:\\fixture\\source.txt"});
        unsigned drop_calls = 0, leave_calls = 0;
        DropTargetCallbacks callbacks;
        callbacks.drop = [&](const auto&, POINT, DWORD, DWORD) { ++drop_calls; return DROPEFFECT_COPY; };
        callbacks.drag_leave = [&] { ++leave_calls; };
        auto* target = new WindowDropTarget(nullptr, std::move(callbacks));
        DWORD effect = DROPEFFECT_COPY;
        target->Drop(nullptr, 0, POINTL{}, &effect);
        Check(effect == DROPEFFECT_NONE && drop_calls == 0 && leave_calls == 1,
              "invalid data cannot submit a drop and clears feedback");
        target->Release();
        data->Release();
    }
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto root = std::filesystem::current_path() / L"bench_data" / L"drag_drop_test" /
        std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    const auto original = root / L"original.txt";
    const auto archived = root / L"$Rfixture";
    { std::ofstream out(original, std::ios::binary); out << "new file at original path"; }
    { std::ofstream out(archived, std::ios::binary); out << "old recycled version"; }
    const auto before_original = ReadBytes(original);
    const auto before_archived = ReadBytes(archived);
    Check(!before_original.empty() && !before_archived.empty(), "create isolated recycle identity fixtures");
    const DWORD rejected = DoFileDragDrop({original.wstring()}, DROPEFFECT_COPY | DROPEFFECT_MOVE,
                                         {}, L"pulse:recycle");
    Check(rejected == DROPEFFECT_NONE && native_calls == 0 && native_paths.empty(),
          "recycle row cannot expose recreated original to a native drag");
    Check(ReadBytes(original) == before_original && ReadBytes(archived) == before_archived,
          "rejected recycle drag preserves both versions");
    const DWORD ordinary = DoFileDragDrop({original.wstring()}, DROPEFFECT_COPY, {}, root.wstring());
    Check(ordinary == DROPEFFECT_COPY && native_calls == 1 &&
          native_paths == std::vector<std::wstring>{original.wstring()},
          "ordinary drag still reaches native boundary with selected path");
    DeleteFileW(original.c_str());
    DeleteFileW(archived.c_str());
    RemoveDirectoryW(root.c_str());
    if (SUCCEEDED(initialized)) CoUninitialize();
    printf("Drag/drop: %d passed, %d failed\n", passed, failures);
    return failures ? 1 : 0;
}
