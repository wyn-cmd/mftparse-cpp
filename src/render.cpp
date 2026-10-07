// Parallel renderer: maps the file, splits it into record chunks, formats the chunks on a
// worker pool and hands the finished text to the sink in file order.
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

#include "mapped_file.hpp"
#include "mft.hpp"

namespace mft {
namespace {

constexpr std::size_t kChunkEntries = 512;  // records per work unit

std::size_t render_range(const std::uint8_t* base, std::size_t first, std::size_t count,
                         bool csv, std::string& out) {
    Entry e;  // reused so each record does not reallocate the attribute vector
    std::size_t items = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t num = first + i;
        parse_entry_into(base + num * kEntrySize, e);
        for (const Attribute& a : e.attributes) {
            const TimelineItem it{num, a.kind, a.name.value_or("<no-name>"), a.created,
                                  a.modified, a.accessed, a.mft_modified};
            if (csv) append_csv(out, it);
            else append_text(out, it);
            ++items;
        }
    }
    return items;
}

}  // namespace

std::size_t render_file(const std::string& path, const RenderOptions& opt,
                        const std::function<void(const std::string&)>& sink) {
    const MappedFile file(path);
    std::size_t total = file.size() / kEntrySize;  // partial trailing record ignored
    if (opt.limit != 0 && opt.limit < total) total = opt.limit;
    if (total == 0) return 0;

    unsigned threads = opt.threads != 0 ? opt.threads : std::thread::hardware_concurrency();
    if (threads == 0) threads = 1;
    const std::size_t nchunks = (total + kChunkEntries - 1) / kChunkEntries;
    const auto chunk_range = [&](std::size_t k, std::size_t& first, std::size_t& cnt) {
        first = k * kChunkEntries;
        cnt = std::min(kChunkEntries, total - first);
    };

    std::size_t items = 0;
    if (threads == 1 || nchunks == 1) {  // no pool needed
        std::string out;
        for (std::size_t k = 0; k < nchunks; ++k) {
            std::size_t first, cnt;
            chunk_range(k, first, cnt);
            out.clear();
            items += render_range(file.data(), first, cnt, opt.csv, out);
            sink(out);
        }
        return items;
    }

    // Ring of `window` slots. Workers claim chunks in order and render into slot k % window,
    // but never run more than `window` chunks ahead of the writer (bounds memory). The
    // calling thread drains slots strictly in file order, so output is deterministic and
    // writing overlaps with rendering.
    struct Slot {
        std::string out;
        std::size_t count = 0;
        bool ready = false;
    };
    const std::size_t window = static_cast<std::size_t>(threads) * 4;
    std::vector<Slot> slots(std::min(window, nchunks));
    const std::size_t ring = slots.size();

    std::mutex m;
    std::condition_variable cv_work, cv_done;
    std::size_t next_claim = 0, next_write = 0;
    bool stop = false;
    std::exception_ptr error;

    const auto worker = [&] {
        for (;;) {
            std::size_t k;
            {
                std::unique_lock<std::mutex> lk(m);
                cv_work.wait(lk, [&] { return stop || next_claim >= nchunks || next_claim < next_write + ring; });
                if (stop || next_claim >= nchunks) return;
                k = next_claim++;
            }
            Slot& slot = slots[k % ring];
            try {
                std::size_t first, cnt;
                chunk_range(k, first, cnt);
                slot.out.clear();
                slot.count = render_range(file.data(), first, cnt, opt.csv, slot.out);
            } catch (...) {
                std::lock_guard<std::mutex> lk(m);
                if (!error) error = std::current_exception();
                stop = true;
                cv_done.notify_all();
                cv_work.notify_all();
                return;
            }
            {
                std::lock_guard<std::mutex> lk(m);
                slot.ready = true;
            }
            cv_done.notify_all();
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);

    const auto shutdown = [&] {
        {
            std::lock_guard<std::mutex> lk(m);
            stop = true;
        }
        cv_work.notify_all();
        for (std::thread& t : pool) t.join();
    };

    try {
        for (std::size_t k = 0; k < nchunks; ++k) {
            Slot& slot = slots[k % ring];
            {
                std::unique_lock<std::mutex> lk(m);
                cv_done.wait(lk, [&] { return slot.ready || stop; });
                if (!slot.ready) break;  // a worker failed
            }
            items += slot.count;
            sink(slot.out);
            {
                std::lock_guard<std::mutex> lk(m);
                slot.ready = false;
                ++next_write;
            }
            cv_work.notify_all();
        }
    } catch (...) {
        shutdown();
        throw;
    }
    shutdown();
    if (error) std::rethrow_exception(error);
    return items;
}

}  // namespace mft
