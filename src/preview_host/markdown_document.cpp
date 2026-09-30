// markdown_document.cpp — see markdown_document.h.
#include "markdown_document.h"
#include "../ipc/preview_protocol.h"
#include <windows.h>
#include "../../third_party/md4c/md4c.h"
#include <string>
#include <vector>

namespace pulse::preview {
namespace {

enum : unsigned { kBold = 1, kItalic = 2, kCode = 4, kStrike = 8, kLink = 16, kImage = 32, kUnderline = 64 };

void AppendEscaped(std::wstring& out, const wchar_t* text, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        const wchar_t c = text[i];
        if (c == L'\\') out += L"\\\\";
        else if (c == L'\t') out += L"\\t";
        else if (c == L'\n') out += L"\\n";
        else if (c != L'\r') out += c;
    }
}

void AppendTarget(std::wstring& out, const std::wstring& target) {
    std::wstring encoded;
    for (wchar_t c : target) {
        if (c == L'%') encoded += L"%25";
        else if (c == L',') encoded += L"%2C";
        else if (c == L';') encoded += L"%3B";
        else encoded += c;
    }
    AppendEscaped(out, encoded.data(), encoded.size());
}

std::wstring AttributeText(const MD_ATTRIBUTE& attribute) {
    return attribute.text && attribute.size ? std::wstring(attribute.text, attribute.size) : std::wstring();
}

void AppendCodePoint(std::wstring& out, unsigned long cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { out += L'\xFFFD'; return; }
    if (cp >= 0x10000) {
        cp -= 0x10000;
        out += static_cast<wchar_t>(0xD800 + (cp >> 10));
        out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
    } else {
        out += static_cast<wchar_t>(cp);
    }
}

void AppendEntity(std::wstring& out, const wchar_t* text, size_t size) {
    const std::wstring e(text, size);
    if (e.size() > 3 && e[1] == L'#') {
        const bool hex = e[2] == L'x' || e[2] == L'X';
        const unsigned long cp = wcstoul(e.c_str() + (hex ? 3 : 2), nullptr, hex ? 16 : 10);
        AppendCodePoint(out, cp);
        return;
    }
    static const struct { const wchar_t* name; wchar_t ch; } kNamed[] = {
        {L"&amp;", L'&'}, {L"&lt;", L'<'}, {L"&gt;", L'>'}, {L"&quot;", L'"'}, {L"&apos;", L'\''},
        {L"&nbsp;", L'\xA0'}, {L"&copy;", L'\xA9'}, {L"&reg;", L'\xAE'}, {L"&trade;", L'\x2122'},
        {L"&hellip;", L'\x2026'}, {L"&mdash;", L'\x2014'}, {L"&ndash;", L'\x2013'},
        {L"&laquo;", L'\xAB'}, {L"&raquo;", L'\xBB'}, {L"&times;", L'\xD7'}, {L"&middot;", L'\xB7'},
    };
    for (const auto& n : kNamed) if (e == n.name) { out += n.ch; return; }
    out += e;
}

struct Builder {
    struct Run { size_t start, length; unsigned flags; std::wstring target; };
    struct OpenSpan { size_t start; unsigned flags; std::wstring target; };
    struct List { bool ordered; unsigned next; };

    std::wstring out;
    size_t limit = 0;
    bool full = false;
    // Current leaf block.
    bool open = false;
    wchar_t kind = L'p';
    std::wstring arg, marker, text;
    std::vector<Run> runs;
    std::vector<OpenSpan> spans;
    // Context.
    int quote = 0;
    std::vector<List> lists;
    std::wstring pending_marker;

    void Record(const std::wstring& line) {
        if (full) return;
        if (out.size() + line.size() + 1 > limit) { full = true; return; }
        out += line;
        out += L'\n';
    }
    int base_indent = 0;  // notebook cells sit right of the In/Out gutter
    std::wstring Context() const {
        return std::to_wstring(quote) + L'\t' + std::to_wstring(lists.size() + static_cast<size_t>(base_indent));
    }
    void Open(wchar_t k, std::wstring a) {
        Flush();
        open = true;
        kind = k;
        arg = std::move(a);
        marker.swap(pending_marker);
        pending_marker.clear();
        text.clear();
        runs.clear();
        spans.clear();
    }
    void Flush() {
        if (!open) return;
        open = false;
        if (kind == L'c' || kind == L'x')
            while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
        std::wstring line = L"B\t";
        line += kind;
        line += L'\t';
        AppendEscaped(line, arg.data(), arg.size());
        line += L'\t' + Context() + L'\t' + marker + L'\t';
        AppendEscaped(line, text.data(), text.size());
        line += L'\t';
        for (const Run& run : runs) {
            line += std::to_wstring(run.start) + L',' + std::to_wstring(run.length) + L',' +
                    std::to_wstring(run.flags);
            if (!run.target.empty()) { line += L','; AppendTarget(line, run.target); }
            line += L';';
        }
        Record(line);
    }
    void EnsureOpen() { if (!open) Open(L'p', L""); }  // tight list items carry no P block
};

int EnterBlock(MD_BLOCKTYPE type, void* detail, void* user) {
    Builder& b = *static_cast<Builder*>(user);
    switch (type) {
    case MD_BLOCK_QUOTE: b.Flush(); ++b.quote; break;
    case MD_BLOCK_UL: b.Flush(); b.lists.push_back({false, 1}); break;
    case MD_BLOCK_OL:
        b.Flush();
        b.lists.push_back({true, static_cast<const MD_BLOCK_OL_DETAIL*>(detail)->start});
        break;
    case MD_BLOCK_LI: {
        b.Flush();
        const auto* li = static_cast<const MD_BLOCK_LI_DETAIL*>(detail);
        if (li->is_task) b.pending_marker = li->task_mark == L' ' ? L"t0" : L"t1";
        else if (!b.lists.empty() && b.lists.back().ordered) b.pending_marker = L"o" + std::to_wstring(b.lists.back().next++);
        else b.pending_marker = L"u";
        break;
    }
    case MD_BLOCK_HR: b.Open(L'r', L""); b.Flush(); break;
    case MD_BLOCK_H:
        b.Open(L'h', std::to_wstring(static_cast<const MD_BLOCK_H_DETAIL*>(detail)->level));
        break;
    case MD_BLOCK_CODE: b.Open(L'c', AttributeText(static_cast<const MD_BLOCK_CODE_DETAIL*>(detail)->lang)); break;
    case MD_BLOCK_HTML: b.Open(L'x', L""); break;
    case MD_BLOCK_P: b.Open(L'p', L""); break;
    case MD_BLOCK_TABLE:
        b.Flush();
        b.Record(L"T\t" + std::to_wstring(static_cast<const MD_BLOCK_TABLE_DETAIL*>(detail)->col_count) +
                 L'\t' + b.Context());
        break;
    case MD_BLOCK_TR: b.Flush(); b.Record(L"R"); break;
    case MD_BLOCK_TH:
    case MD_BLOCK_TD: {
        const MD_ALIGN align = static_cast<const MD_BLOCK_TD_DETAIL*>(detail)->align;
        b.Open(L't', align == MD_ALIGN_LEFT ? L"l" : align == MD_ALIGN_CENTER ? L"c"
                   : align == MD_ALIGN_RIGHT ? L"r" : L"-");
        b.marker = type == MD_BLOCK_TH ? L"h" : L"";
        break;
    }
    default: break;
    }
    return 0;
}

int LeaveBlock(MD_BLOCKTYPE type, void*, void* user) {
    Builder& b = *static_cast<Builder*>(user);
    switch (type) {
    case MD_BLOCK_QUOTE: b.Flush(); if (b.quote > 0) --b.quote; break;
    case MD_BLOCK_UL:
    case MD_BLOCK_OL: b.Flush(); if (!b.lists.empty()) b.lists.pop_back(); b.pending_marker.clear(); break;
    case MD_BLOCK_LI: b.Flush(); b.pending_marker.clear(); break;
    case MD_BLOCK_TABLE: b.Flush(); b.Record(L"E"); break;
    case MD_BLOCK_H: case MD_BLOCK_CODE: case MD_BLOCK_HTML: case MD_BLOCK_P:
    case MD_BLOCK_TH: case MD_BLOCK_TD: b.Flush(); break;
    default: break;
    }
    return 0;
}

int EnterSpan(MD_SPANTYPE type, void* detail, void* user) {
    Builder& b = *static_cast<Builder*>(user);
    b.EnsureOpen();
    Builder::OpenSpan span{b.text.size(), 0, {}};
    switch (type) {
    case MD_SPAN_EM: span.flags = kItalic; break;
    case MD_SPAN_STRONG: span.flags = kBold; break;
    case MD_SPAN_A:
        span.flags = kLink;
        span.target = AttributeText(static_cast<const MD_SPAN_A_DETAIL*>(detail)->href);
        break;
    case MD_SPAN_IMG:
        span.flags = kImage;
        span.target = AttributeText(static_cast<const MD_SPAN_IMG_DETAIL*>(detail)->src);
        break;
    case MD_SPAN_CODE: case MD_SPAN_LATEXMATH: case MD_SPAN_LATEXMATH_DISPLAY: span.flags = kCode; break;
    case MD_SPAN_DEL: span.flags = kStrike; break;
    case MD_SPAN_U: span.flags = kUnderline; break;
    case MD_SPAN_WIKILINK:
        span.flags = kLink;
        span.target = AttributeText(static_cast<const MD_SPAN_WIKILINK_DETAIL*>(detail)->target);
        break;
    default: break;
    }
    b.spans.push_back(std::move(span));
    return 0;
}

int LeaveSpan(MD_SPANTYPE, void*, void* user) {
    Builder& b = *static_cast<Builder*>(user);
    if (b.spans.empty()) return 0;
    Builder::OpenSpan span = std::move(b.spans.back());
    b.spans.pop_back();
    if ((span.flags & kImage) && b.text.size() == span.start) b.text += L"image";
    if (span.flags && b.text.size() > span.start)
        b.runs.push_back({span.start, b.text.size() - span.start, span.flags, std::move(span.target)});
    return 0;
}

int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* user) {
    Builder& b = *static_cast<Builder*>(user);
    b.EnsureOpen();
    switch (type) {
    case MD_TEXT_NULLCHAR: b.text += L'\xFFFD'; break;
    case MD_TEXT_BR: b.text += L'\n'; break;
    case MD_TEXT_SOFTBR:
        // CJK prose wraps lines without spaces between them.
        if (b.text.empty() || b.text.back() < 0x2E80) b.text += L' ';
        break;
    case MD_TEXT_ENTITY: AppendEntity(b.text, text, size); break;
    default: b.text.append(text, size); break;
    }
    return 0;
}

} // namespace

bool AppendMarkdownBlocks(const std::wstring& markdown, std::wstring& payload, size_t limit,
                          int base_indent) {
    Builder b;
    b.out.swap(payload);
    b.limit = limit;
    b.base_indent = base_indent;
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = EnterBlock;
    parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan;
    parser.leave_span = LeaveSpan;
    parser.text = Text;
    const bool parsed = md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &b) == 0;
    b.Flush();
    payload.swap(b.out);
    return parsed && !b.full;
}

bool AppendMarkdownBlock(std::wstring& payload, size_t limit, wchar_t kind, const std::wstring& arg,
                         int indent, const std::wstring& marker, const std::wstring& text,
                         const std::wstring& image_target) {
    std::wstring line = L"B\t";
    line += kind;
    line += L'\t';
    AppendEscaped(line, arg.data(), arg.size());
    line += L"\t0\t" + std::to_wstring(indent) + L'\t' + marker + L'\t';
    AppendEscaped(line, text.data(), text.size());
    line += L'\t';
    if (!image_target.empty()) {
        line += L"0," + std::to_wstring(text.size()) + L",32,";
        AppendTarget(line, image_target);
        line += L';';
    }
    if (payload.size() + line.size() + 1 > limit) return false;
    payload += line;
    payload += L'\n';
    return true;
}

bool MakeMarkdownDocument(const std::wstring& source, std::wstring& payload) {
    Builder b;
    const size_t source_cost = source.size() * 2 + 16;
    if (source_cost + 1024 >= ipc::kPreviewMaxArchiveChars) return false;
    b.limit = ipc::kPreviewMaxArchiveChars - source_cost - 64;
    b.out = L"PULSEMD\t1\n";
    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB;
    parser.enter_block = EnterBlock;
    parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan;
    parser.leave_span = LeaveSpan;
    parser.text = Text;
    if (md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &b) != 0) return false;
    b.Flush();
    b.out += L"S\t";
    AppendEscaped(b.out, source.data(), source.size());
    b.out += L'\n';
    payload.swap(b.out);
    return true;
}

} // namespace pulse::preview