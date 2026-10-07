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
        "  -t, --threads N worker threads; 0 = all cores (default)\n"
        "  --format        text (default, same layout as the Python tool) or csv\n",
        to);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);  // emit plain \n
#endif
    std::string path = "MFT.raw";
    std::size_t limit = 500;
    unsigned threads = 0;
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
        } else if (a == "-t" || a == "--threads") {
            if (i + 1 >= argc) { usage(stderr); return 2; }
            const char* v = argv[++i];
            auto r = std::from_chars(v, v + std::strlen(v), threads);
            if (r.ec != std::errc{} || *r.ptr != '\0') {
                std::fprintf(stderr, "error: invalid thread count '%s'\n", v);
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

    std::size_t count = 0;
    try {
        if (csv) std::fputs(mft::csv_header().c_str(), stdout);
        count = mft::render_file(path, {csv, limit, threads}, [](const std::string& text) {
            std::fwrite(text.data(), 1, text.size(), stdout);
        });
    } catch (const mft::FileNotFound&) {
        std::fflush(stdout);
        std::fprintf(stderr,
                     "error: %s not found. Extract the $MFT from an NTFS volume and point "
                     "mftparse at it.\n",
                     path.c_str());
        return 1;
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "error building timeline: %s\n", e.what());
        return 1;
    }
    if (count == 0 && !csv) std::fputs("[!] No NTFS artifacts found.\n", stdout);
    std::fflush(stdout);
    return 0;
}
