// Unit tests for mft.hpp. No framework: CHECK counts failures, main reports.
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "mft.hpp"

using namespace mft;
using Bytes = std::vector<std::uint8_t>;

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct TestCase { const char* name; void (*fn)(); };
static std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Reg { Reg(const char* n, void (*f)()) { registry().push_back({n, f}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

// Filetimes computed independently with Python's datetime arithmetic.
constexpr std::uint64_t FT2020 = 132223104000000000ULL, FT2021 = 132539328000000000ULL,
                        FT2021B = 132540192000000000ULL, FT2021C = 132541056000000000ULL,
                        FTUNIX = 116444736000000000ULL,
                        FTMAX = 2650467743999999999ULL,   // 9999-12-31 23:59:59.999999
                        FTOVER = 2650467744000000000ULL;  // one microsecond later

static void put16(Bytes& b, std::size_t o, std::uint16_t v) { b[o] = static_cast<std::uint8_t>(v & 0xFF); b[o + 1] = static_cast<std::uint8_t>(v >> 8); }
static void put32(Bytes& b, std::size_t o, std::uint32_t v) { put16(b, o, v & 0xFFFF); put16(b, o + 2, static_cast<std::uint16_t>(v >> 16)); }
static void put64(Bytes& b, std::size_t o, std::uint64_t v) { put32(b, o, static_cast<std::uint32_t>(v)); put32(b, o + 4, static_cast<std::uint32_t>(v >> 32)); }

static Bytes blank_entry() {
    Bytes raw(kEntrySize, 0);
    raw[0] = 'F'; raw[1] = 'I'; raw[2] = 'L'; raw[3] = 'E';
    put16(raw, 20, 56);
    return raw;
}

// Writes one attribute at off and returns the offset after it.
static std::size_t add_attr(Bytes& raw, std::size_t off, std::uint32_t type, const Bytes& content, bool resident = true) {
    const std::uint32_t len = static_cast<std::uint32_t>(24 + content.size());
    put32(raw, off, type);
    put32(raw, off + 4, len);
    raw[off + 8] = resident ? 0 : 1;
    put32(raw, off + 16, static_cast<std::uint32_t>(content.size()));
    put16(raw, off + 20, 24);
    for (std::size_t i = 0; i < content.size(); ++i) raw[off + 24 + i] = content[i];
    return off + len;
}

static Bytes si_content(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) {
    Bytes x(32, 0);
    put64(x, 0, a); put64(x, 8, b); put64(x, 16, c); put64(x, 24, d);
    return x;
}

static Bytes utf16(const std::u16string& s) {
    Bytes b;
    for (char16_t ch : s) { b.push_back(static_cast<std::uint8_t>(ch & 0xFF)); b.push_back(static_cast<std::uint8_t>(ch >> 8)); }
    return b;
}

static Bytes fn_content(const std::u16string& name, std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) {
    Bytes x(66, 0);
    put64(x, 8, a); put64(x, 16, b); put64(x, 24, c); put64(x, 32, d);
    x[64] = static_cast<std::uint8_t>(name.size());
    Bytes n = utf16(name);
    x.insert(x.end(), n.begin(), n.end());
    return x;
}

static Bytes standard_entry(const std::u16string& name) {
    Bytes raw = blank_entry();
    std::size_t off = add_attr(raw, 56, 0x10, si_content(FT2020, FT2021, FT2021B, FT2021C));
    off = add_attr(raw, off, 0x30, fn_content(name, FT2020, FT2021, FT2021B, FT2021C));
    put32(raw, off, 0xFFFFFFFF);
    return raw;
}

TEST(entry_yields_standard_info_and_file_name) {
    Entry e = parse_entry(standard_entry(u"hello.txt").data());
    CHECK(e.valid);
    CHECK(e.attributes.size() == 2);
    CHECK(e.attributes[0].kind == AttrKind::StandardInformation);
    CHECK(e.attributes[1].kind == AttrKind::FileName);
}

TEST(file_name_decodes) {
    Entry e = parse_entry(standard_entry(u"report.docx").data());
    CHECK(e.attributes[1].name == "report.docx");
}

TEST(timestamps_round_trip) {
    Entry e = parse_entry(standard_entry(u"x").data());
    const Attribute& si = e.attributes[0];
    CHECK(si.created && si.created->str() == "2020-01-01 00:00:00");
    CHECK(si.modified && si.modified->str() == "2021-01-01 00:00:00");
    CHECK(si.mft_modified && si.mft_modified->str() == "2021-01-02 00:00:00");
    CHECK(si.accessed && si.accessed->str() == "2021-01-03 00:00:00");
}

TEST(entry_without_magic_is_invalid) {
    Bytes raw(kEntrySize, 0);
    raw[0] = 'X'; raw[1] = 'X'; raw[2] = 'X'; raw[3] = 'X';
    Entry e = parse_entry(raw.data());
    CHECK(!e.valid);
    CHECK(e.attributes.empty());
}

TEST(filetime_edges) {
    CHECK(!filetime_to_dt(0));
    CHECK(!filetime_to_dt(~0ULL));
    CHECK(!filetime_to_dt(FTOVER));
    auto mx = filetime_to_dt(FTMAX);
    CHECK(mx && mx->str() == "9999-12-31 23:59:59.999999");
    auto unix_epoch = filetime_to_dt(FTUNIX);
    CHECK(unix_epoch && unix_epoch->str() == "1970-01-01 00:00:00");
    auto one_us = filetime_to_dt(FTUNIX + 10);
    CHECK(one_us && one_us->str() == "1970-01-01 00:00:00.000001");
    auto sub_us = filetime_to_dt(FTUNIX + 9);  // sub-microsecond ticks truncate, like // 10
    CHECK(sub_us && sub_us->str() == "1970-01-01 00:00:00");
    auto first = filetime_to_dt(1);
    CHECK(first && first->str() == "1601-01-01 00:00:00");
    auto leap = filetime_to_dt(FT2020 + 59ULL * 86400ULL * 10'000'000ULL);
    CHECK(leap && leap->str() == "2020-02-29 00:00:00");
}

TEST(short_standard_info_has_no_timestamps) {
    Bytes raw = blank_entry();
    std::size_t off = add_attr(raw, 56, 0x10, Bytes(31, 0xFF));
    put32(raw, off, 0xFFFFFFFF);
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.size() == 1);
    CHECK(!e.attributes[0].created && !e.attributes[0].accessed);
}

TEST(short_file_name_has_no_name) {
    Bytes raw = blank_entry();
    std::size_t off = add_attr(raw, 56, 0x30, Bytes(65, 1));
    put32(raw, off, 0xFFFFFFFF);
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.size() == 1);
    CHECK(!e.attributes[0].name && !e.attributes[0].created);
}

TEST(tiny_attr_len_stops_parsing) {
    Bytes raw = blank_entry();
    std::size_t off = add_attr(raw, 56, 0x10, si_content(FT2020, FT2020, FT2020, FT2020));
    put32(raw, off, 0x30);
    put32(raw, off + 4, 23);  // < 24: parser must stop here
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.size() == 1);
}

TEST(content_past_entry_end_stops_parsing) {
    Bytes raw = blank_entry();
    put32(raw, 56, 0x10);
    put32(raw, 60, 40);
    put32(raw, 72, 5000);  // content_len far beyond the record
    put16(raw, 76, 24);
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.empty());
}

TEST(non_resident_is_skipped_but_walk_continues) {
    Bytes raw = blank_entry();
    std::size_t off = add_attr(raw, 56, 0x10, si_content(FT2020, FT2020, FT2020, FT2020), false);
    off = add_attr(raw, off, 0x30, fn_content(u"after.bin", FT2020, FT2020, FT2020, FT2020));
    put32(raw, off, 0xFFFFFFFF);
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.size() == 1);
    CHECK(e.attributes[0].name == "after.bin");
}

TEST(name_decoding) {
    Bytes ok = utf16(u"a\U0001F600\u00e9");
    CHECK(read_utf16le_string(ok.data(), ok.size()) == "a\xF0\x9F\x98\x80\xC3\xA9");
    Bytes lone_high = utf16(std::u16string(1, char16_t(0xD83D)));
    CHECK(read_utf16le_string(lone_high.data(), lone_high.size()).empty());
    Bytes lone_low = utf16(std::u16string(1, char16_t(0xDE00)) + u"x");
    CHECK(read_utf16le_string(lone_low.data(), lone_low.size()).empty());
    Bytes odd(3, 'a');
    CHECK(read_utf16le_string(odd.data(), odd.size()).empty());
    Bytes nuls = utf16(std::u16string(u"ab") + std::u16string(2, char16_t(0)));
    CHECK(read_utf16le_string(nuls.data(), nuls.size()) == "ab");
}

TEST(declared_name_longer_than_content_is_truncated_safely) {
    Bytes raw = blank_entry();
    Bytes c = fn_content(u"abcd", FT2020, FT2020, FT2020, FT2020);
    c[64] = 200;  // claims 200 chars, only 4 present
    std::size_t off = add_attr(raw, 56, 0x30, c);
    put32(raw, off, 0xFFFFFFFF);
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.size() == 1);
    CHECK(e.attributes[0].name == "abcd");
}

TEST(timeline_over_a_file) {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "mftparse_test.raw";
    {
        std::ofstream f(p, std::ios::binary);
        Bytes e = standard_entry(u"one");
        for (int i = 0; i < 3; ++i) f.write(reinterpret_cast<const char*>(e.data()), static_cast<std::streamsize>(e.size()));
        Bytes junk(500, 'J');  // partial trailing record
        f.write(reinterpret_cast<const char*>(junk.data()), static_cast<std::streamsize>(junk.size()));
    }
    auto all = build_timeline(p.string(), 0);
    CHECK(all.size() == 6);  // 3 entries x (SI + FN)
    CHECK(all.back().entry == 2);
    auto two = build_timeline(p.string(), 2);
    CHECK(two.size() == 4 && two.back().entry == 1);
    CHECK(build_timeline(p.string(), 500).size() == 6);
    fs::remove(p);

    bool threw = false;
    try { build_timeline((fs::temp_directory_path() / "no_such_mft.raw").string(), 0); }
    catch (const FileNotFound&) { threw = true; }
    CHECK(threw);
}

TEST(output_formats) {
    TimelineItem it{7, AttrKind::FileName, "a,\"b\"", DateTime{2020, 1, 1, 0, 0, 0, 0}, std::nullopt, std::nullopt, std::nullopt};
    CHECK(format_text(it) ==
          "\nEntry #7 (FILE_NAME)\nFile          : a,\"b\"\nCreated       : 2020-01-01 00:00:00\n"
          "Modified      : None\nAccessed      : None\nMFT Modified  : None\n\n");
    CHECK(format_csv(it) == "7,FILE_NAME,\"a,\"\"b\"\"\",2020-01-01 00:00:00,,,\n");
}

TEST(parallel_render_matches_sequential) {
    namespace fs = std::filesystem;
    const fs::path p = fs::temp_directory_path() / "mftparse_par.raw";
    {
        std::ofstream f(p, std::ios::binary);
        for (int i = 0; i < 1500; ++i) {
            Bytes e = (i % 7 == 3) ? Bytes(kEntrySize, 0)
                                   : standard_entry(i % 2 ? u"odd,\"name\"" : u"even");
            f.write(reinterpret_cast<const char*>(e.data()), static_cast<std::streamsize>(e.size()));
        }
        Bytes junk(100, 'J');
        f.write(reinterpret_cast<const char*>(junk.data()), static_cast<std::streamsize>(junk.size()));
    }
    for (bool csv : {false, true}) {
        for (std::size_t limit : {std::size_t{0}, std::size_t{1}, std::size_t{513}, std::size_t{100000}}) {
            std::string want;
            const auto seq = build_timeline(p.string(), limit);
            for (const auto& it : seq) want += csv ? format_csv(it) : format_text(it);
            for (unsigned t : {1u, 2u, 5u, 16u}) {
                std::string got;
                const std::size_t n = render_file(p.string(), RenderOptions{csv, limit, t},
                                                  [&](const std::string& s) { got += s; });
                CHECK(got == want);
                CHECK(n == seq.size());
            }
        }
    }
    fs::remove(p);

    const fs::path empty = fs::temp_directory_path() / "mftparse_empty.raw";
    { std::ofstream f(empty, std::ios::binary); }
    CHECK(render_file(empty.string(), RenderOptions{}, [](const std::string&) {}) == 0);
    fs::remove(empty);

    bool threw = false;
    try { render_file("no_such_file_xyz.raw", RenderOptions{}, [](const std::string&) {}); }
    catch (const FileNotFound&) { threw = true; }
    CHECK(threw);
}

TEST(fixups_restore_name_across_sector_boundary) {
    Bytes raw = blank_entry();
    std::size_t off = add_attr(raw, 56, 0x10, si_content(FT2020, FT2020, FT2020, FT2020));
    off = add_attr(raw, off, 0x30, fn_content(std::u16string(200, u'a'), FT2020, FT2020, FT2020, FT2020));
    put32(raw, off, 0xFFFFFFFF);
    // The name spans bytes 202..601, so it covers the sector end at 510..511.
    put16(raw, 4, 48); put16(raw, 6, 3);
    put16(raw, 48, 0x1234);                         // check value
    put16(raw, 50, static_cast<std::uint16_t>(raw[510] | (raw[511] << 8)));  // real bytes, sector 1
    put16(raw, 52, 0);                              // real bytes, sector 2
    put16(raw, 510, 0x1234); put16(raw, 1022, 0x1234);
    Entry e = parse_entry(raw.data());
    CHECK(e.attributes.size() == 2);
    CHECK(e.attributes[1].name == std::string(200, 'a'));

    put16(raw, 1022, 0x9999);  // wrong check value: record is left alone, no crash
    Entry torn = parse_entry(raw.data());
    CHECK(torn.valid);
}

int main() {
    int failed_tests = 0;
    for (const TestCase& t : registry()) {
        const int before = g_fail;
        t.fn();
        const bool ok = g_fail == before;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", t.name);
        if (!ok) ++failed_tests;
    }
    std::printf("\n%zu tests, %d checks, %d failed\n", registry().size(), g_checks, failed_tests);
    return failed_tests == 0 ? 0 : 1;
}
