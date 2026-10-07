#include "mft.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace mft {
namespace {

std::uint16_t rd16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
std::uint32_t rd32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t rd64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(rd32(p)) | (static_cast<std::uint64_t>(rd32(p + 4)) << 32);
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Four consecutive filetimes: created, modified, mft_modified, accessed.
void read_times(const std::uint8_t* p, Attribute& a) {
    a.created = filetime_to_dt(rd64(p));
    a.modified = filetime_to_dt(rd64(p + 8));
    a.mft_modified = filetime_to_dt(rd64(p + 16));
    a.accessed = filetime_to_dt(rd64(p + 24));
}

Attribute parse_standard_info(const std::uint8_t* c, std::size_t len) {
    Attribute a{AttrKind::StandardInformation, {}, {}, {}, {}, {}};
    if (len < 32) return a;  // too short for four filetimes
    read_times(c, a);
    return a;
}

Attribute parse_file_name(const std::uint8_t* c, std::size_t len) {
    Attribute a{AttrKind::FileName, {}, {}, {}, {}, {}};
    if (len < 66) return a;  // too short for the fixed header
    read_times(c + 8, a);
    const std::size_t name_bytes = static_cast<std::size_t>(c[64]) * 2;
    const std::size_t avail = len - 66;
    a.name = read_utf16le_string(c + 66, name_bytes < avail ? name_bytes : avail);
    return a;
}

}  // namespace

void DateTime::append_to(std::string& out) const {
    const auto digit = [](int v) { return static_cast<char>('0' + v); };
    const auto two = [&](int v) { out.push_back(digit(v / 10)); out.push_back(digit(v % 10)); };
    out.push_back(digit(year / 1000));
    out.push_back(digit(year / 100 % 10));
    out.push_back(digit(year / 10 % 10));
    out.push_back(digit(year % 10));
    out.push_back('-'); two(month);
    out.push_back('-'); two(day);
    out.push_back(' '); two(hour);
    out.push_back(':'); two(minute);
    out.push_back(':'); two(second);
    if (microsecond != 0) {
        out.push_back('.');
        int div = 100000;
        for (int i = 0; i < 6; ++i, div /= 10) out.push_back(digit(microsecond / div % 10));
    }
}

std::string DateTime::str() const {
    std::string s;
    append_to(s);
    return s;
}

std::optional<DateTime> filetime_to_dt(std::uint64_t filetime) {
    if (filetime == 0) return std::nullopt;
    constexpr std::uint64_t kUsPerDay = 86'400'000'000ULL;
    const std::uint64_t total_us = filetime / 10;
    const std::uint64_t days = total_us / kUsPerDay;
    std::uint64_t rem = total_us % kUsPerDay;

    // Days since 1601-01-01 -> civil date (Hinnant's algorithm, shifted epoch).
    const std::int64_t z = static_cast<std::int64_t>(days) - 134774 + 719468;
    const std::int64_t era = z / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    if (month <= 2) ++y;
    if (y > 9999) return std::nullopt;

    DateTime dt{};
    dt.year = static_cast<int>(y);
    dt.month = static_cast<int>(month);
    dt.day = static_cast<int>(day);
    dt.hour = static_cast<int>(rem / 3'600'000'000ULL);
    rem %= 3'600'000'000ULL;
    dt.minute = static_cast<int>(rem / 60'000'000ULL);
    rem %= 60'000'000ULL;
    dt.second = static_cast<int>(rem / 1'000'000ULL);
    dt.microsecond = static_cast<int>(rem % 1'000'000ULL);
    return dt;
}

std::string read_utf16le_string(const std::uint8_t* data, std::size_t len) {
    if (len % 2 != 0) return {};
    std::string out;
    out.reserve(len);
    for (std::size_t i = 0; i < len; i += 2) {
        const std::uint32_t unit = rd16(data + i);
        std::uint32_t cp = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 4 > len) return {};
            const std::uint32_t low = rd16(data + i + 2);
            if (low < 0xDC00 || low > 0xDFFF) return {};
            cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
            i += 2;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return {};
        }
        append_utf8(out, cp);
    }
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

const char* kind_name(AttrKind kind) {
    return kind == AttrKind::StandardInformation ? "STANDARD_INFORMATION" : "FILE_NAME";
}

void parse_entry_into(const std::uint8_t* raw, Entry& e) {
    e.attributes.clear();
    e.valid = raw[0] == 'F' && raw[1] == 'I' && raw[2] == 'L' && raw[3] == 'E';
    if (!e.valid) return;

    std::uint64_t off = rd16(raw + 20);
    while (off < kEntrySize - 8) {
        const std::uint32_t type = rd32(raw + off);
        const std::uint64_t attr_len = rd32(raw + off + 4);
        if (type == kAttrEnd) break;
        if (attr_len < 24 || off + attr_len > kEntrySize) break;

        const bool non_resident = raw[off + 8] != 0;
        if (!non_resident) {
            const std::uint64_t content_len = rd32(raw + off + 16);
            const std::uint64_t content_off = rd16(raw + off + 20);
            const std::uint64_t start = off + content_off;
            const std::uint64_t end = start + content_len;
            if (end > kEntrySize) break;

            const std::uint8_t* content = raw + start;
            const std::size_t n = static_cast<std::size_t>(content_len);
            if (type == kAttrStandardInformation) {
                e.attributes.push_back(parse_standard_info(content, n));
            } else if (type == kAttrFileName) {
                e.attributes.push_back(parse_file_name(content, n));
            }
        }
        off += attr_len;
    }
}

Entry parse_entry(const std::uint8_t* raw) {
    Entry e;
    parse_entry_into(raw, e);
    return e;
}

void for_each_item(const std::string& path, std::size_t limit,
                   const std::function<void(const TimelineItem&)>& fn) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) throw FileNotFound(path);
        throw std::runtime_error("Permission denied accessing MFT file: " + path);
    }

    constexpr std::size_t kChunkEntries = 4096;
    std::vector<std::uint8_t> buf(kChunkEntries * kEntrySize);
    std::size_t entry_num = 0;

    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const std::size_t got = static_cast<std::size_t>(in.gcount());
        const std::size_t whole = got / kEntrySize;
        for (std::size_t i = 0; i < whole; ++i) {
            const Entry e = parse_entry(buf.data() + i * kEntrySize);
            for (const Attribute& a : e.attributes) {
                fn(TimelineItem{entry_num, a.kind, a.name.value_or("<no-name>"), a.created,
                                a.modified, a.accessed, a.mft_modified});
            }
            ++entry_num;
            if (limit != 0 && entry_num >= limit) return;
        }
        if (got % kEntrySize != 0 || got == 0) break;  // partial trailing record / EOF
    }
}

std::vector<TimelineItem> build_timeline(const std::string& path, std::size_t limit) {
    std::vector<TimelineItem> items;
    for_each_item(path, limit, [&](const TimelineItem& it) { items.push_back(it); });
    return items;
}

namespace {

void append_opt(std::string& out, const std::optional<DateTime>& dt, const char* none) {
    if (dt) dt->append_to(out);
    else out += none;
}

void append_csv_field(std::string& out, const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) {
        out += s;
        return;
    }
    out.push_back('"');
    for (char ch : s) {
        if (ch == '"') out.push_back('"');
        out.push_back(ch);
    }
    out.push_back('"');
}

}  // namespace

void append_text(std::string& out, const TimelineItem& it) {
    out += "\nEntry #";
    out += std::to_string(it.entry);
    out += " (";
    out += kind_name(it.kind);
    out += ")\nFile          : ";
    out += it.file;
    out += "\nCreated       : ";
    append_opt(out, it.created, "None");
    out += "\nModified      : ";
    append_opt(out, it.modified, "None");
    out += "\nAccessed      : ";
    append_opt(out, it.accessed, "None");
    out += "\nMFT Modified  : ";
    append_opt(out, it.mft_modified, "None");
    out += "\n\n";
}

void append_csv(std::string& out, const TimelineItem& it) {
    out += std::to_string(it.entry);
    out.push_back(',');
    out += kind_name(it.kind);
    out.push_back(',');
    append_csv_field(out, it.file);
    out.push_back(',');
    append_opt(out, it.created, "");
    out.push_back(',');
    append_opt(out, it.modified, "");
    out.push_back(',');
    append_opt(out, it.accessed, "");
    out.push_back(',');
    append_opt(out, it.mft_modified, "");
    out.push_back('\n');
}

std::string format_text(const TimelineItem& it) {
    std::string s;
    append_text(s, it);
    return s;
}

std::string csv_header() { return "entry,type,file,created,modified,accessed,mft_modified\n"; }

std::string format_csv(const TimelineItem& it) {
    std::string s;
    append_csv(s, it);
    return s;
}

}  // namespace mft
