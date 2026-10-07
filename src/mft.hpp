// mftparse: NTFS $MFT timestamp parser. C++ port of the Python NTFS-MFT-parser.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mft {

inline constexpr std::size_t kEntrySize = 1024;
inline constexpr std::uint32_t kAttrStandardInformation = 0x10;
inline constexpr std::uint32_t kAttrFileName = 0x30;
inline constexpr std::uint32_t kAttrEnd = 0xFFFFFFFF;

struct DateTime {
    int year, month, day, hour, minute, second, microsecond;
    // Matches Python's str(datetime): fractional part only when non-zero.
    std::string str() const;
    void append_to(std::string& out) const;
    bool operator==(const DateTime&) const = default;
};

// NTFS filetime (100 ns ticks since 1601-01-01). Zero and anything past
// year 9999 yield nullopt, as in the Python version.
std::optional<DateTime> filetime_to_dt(std::uint64_t filetime);

// Strict UTF-16LE -> UTF-8. Odd length or unpaired surrogates give "";
// trailing NULs are stripped.
std::string read_utf16le_string(const std::uint8_t* data, std::size_t len);

enum class AttrKind { StandardInformation, FileName };
const char* kind_name(AttrKind kind);

struct Attribute {
    AttrKind kind;
    std::optional<DateTime> created, modified, mft_modified, accessed;
    std::optional<std::string> name;  // FILE_NAME only
};

struct Entry {
    bool valid = false;
    std::vector<Attribute> attributes;
};

// raw must point at kEntrySize readable bytes.
Entry parse_entry(const std::uint8_t* raw);

struct TimelineItem {
    std::size_t entry;
    AttrKind kind;
    std::string file;
    std::optional<DateTime> created, modified, accessed, mft_modified;
};

class FileNotFound : public std::runtime_error {
public:
    explicit FileNotFound(const std::string& path)
        : std::runtime_error("MFT file not found: " + path) {}
};

// Streams records from path; limit == 0 means no limit. A trailing partial
// record is ignored. Throws FileNotFound or std::runtime_error.
void for_each_item(const std::string& path, std::size_t limit,
                   const std::function<void(const TimelineItem&)>& fn);
std::vector<TimelineItem> build_timeline(const std::string& path, std::size_t limit);

std::string format_text(const TimelineItem& item);
std::string csv_header();
std::string format_csv(const TimelineItem& item);

// Append-style formatters (no temporary strings); format_text/format_csv wrap these.
void append_text(std::string& out, const TimelineItem& item);
void append_csv(std::string& out, const TimelineItem& item);

// Like parse_entry but reuses `out`'s storage.
void parse_entry_into(const std::uint8_t* raw, Entry& out);

struct RenderOptions {
    bool csv = false;
    std::size_t limit = 0;     // 0 = all records
    unsigned threads = 0;      // 0 = hardware concurrency
};

// Memory-maps `path`, formats records on a thread pool and passes the text to `sink` in
// file order. Returns the number of timeline items. Output is identical for any thread
// count. Throws FileNotFound or std::runtime_error.
std::size_t render_file(const std::string& path, const RenderOptions& opt,
                        const std::function<void(const std::string&)>& sink);

}  // namespace mft
