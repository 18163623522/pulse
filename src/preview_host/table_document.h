// table_document.h — Quick Look table payload for CSV/TSV and XLSX.
//
// Payload (UTF-16, one record per line, fields separated by tabs; text fields
// escape backslash, tab and newline as \\ \t \n):
//   PULSETBL\t1
//   S\t<kind>\t<name>\t<columns>\t<rows>\t<truncated>\t<detail>\t<header>
//       kind: "csv" or "xlsx"; rows/columns: what follows in R records;
//       truncated: 1 when the file has more rows/columns than were sent;
//       detail: CSV delimiter name ("comma", "semicolon", "tab", "pipe");
//       header: 1 when the first row is a header row (CSV heuristic).
//   R\t<cell>\t<cell>...   one per row of the preceding sheet; a cell that
//       starts with "\B" is bold (XLSX styles).
//   X\t<source>            CSV only: the decoded text for the source view.
#pragma once

#include <cstdint>
#include <string>

#include "../ipc/preview_protocol.h"

namespace pulse::preview {

// Reads up to max_bytes of a text file and decodes it (BOM, UTF-8, else the
// ANSI code page). cut: the file is longer; the text then ends at a line break.
bool ReadTextFile(const std::wstring& path, size_t max_bytes, std::wstring& text, bool& cut,
                  uint32_t& bytes_read, ipc::PreviewTextEncoding& encoding);

bool IsTableExtension(std::wstring_view extension);
bool IsSpreadsheetExtension(std::wstring_view extension);  // .xlsx / .xlsm

bool MakeCsvTable(const std::wstring& path, std::wstring_view extension, std::wstring& payload,
                  uint32_t& bytes_read, bool& truncated, ipc::PreviewTextEncoding& encoding);
// Needs the system libarchive (archiveint.dll, Windows 10 1803+) via ReadZipEntry.
bool MakeXlsxTable(const std::wstring& path, std::wstring& payload, uint32_t& bytes_read);

}  // namespace pulse::preview
