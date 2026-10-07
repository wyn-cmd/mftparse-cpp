// mftparse: print a timeline from a raw $MFT file.
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>

#include "mft.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void usage(std::FILE* to) {
    std::fputs(
        "usage: mftparse [-n N] [--format text|csv] [file]\n"
        "  file            raw $MFT dump (default: MFT.raw)\n"
        "  -n, --limit N   stop after N records; 0 = all (default: 500, as main.py)\n"
        "  --format        text (default, same layout as the Python tool) or csv\n",
        to);
}

class Out {
public:
    void write(const std::string& s) {
        buf_ += s;
        if (buf_.size() >= (1u << 16)) flush();
    }
    void flush() {
        if (!buf_.empty()) std::fwrite(buf_.data(), 1, buf_.size(), stdout);
        buf_.clear();
    }

private:
    std::string buf_;
};

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);  // emit plain \n
#endif
    std::string path = "MFT.raw";
    std::size_t limit = 500;
    bool csv = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(stdout);
            return 0;
        } else if (a == "-n" || a == "--limit") {
            if (i + 1 >= argc) { usage(stderr); return 2; }
            const char* v = argv[++i];
            auto r = std::from_chars(v, v + std::strlen(v), limit);
            if (r.ec != std::errc{} || *r.ptr != '\0') {
                std::fprintf(stderr, "error: invalid limit '%s'\n", v);
                return 2;
            }
        } else if (a == "--format") {
            if (i + 1 >= argc) { usage(stderr); return 2; }
            const std::string v = argv[++i];
            if (v == "csv") csv = true;
            else if (v == "text") csv = false;
            else { std::fprintf(stderr, "error: unknown format '%s'\n", v.c_str()); return 2; }
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s'\n", a.c_str());
            usage(stderr);
            return 2;
        } else {
            path = a;
        }
    }

    Out out;
    std::size_t count = 0;
    try {
        if (csv) out.write(mft::csv_header());
        mft::for_each_item(path, limit, [&](const mft::TimelineItem& it) {
            ++count;
            out.write(csv ? mft::format_csv(it) : mft::format_text(it));
        });
    } catch (const mft::FileNotFound&) {
        out.flush();
        std::fprintf(stderr,
                     "error: %s not found. Extract the $MFT from an NTFS volume and point "
                     "mftparse at it.\n",
                     path.c_str());
        return 1;
    } catch (const std::exception& e) {
        out.flush();
        std::fprintf(stderr, "error building timeline: %s\n", e.what());
        return 1;
    }
    if (count == 0 && !csv) out.write("[!] No NTFS artifacts found.\n");
    out.flush();
    return 0;
}
