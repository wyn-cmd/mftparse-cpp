#include "mapped_file.hpp"

#include <filesystem>
#include <stdexcept>

#include "mft.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mft {
namespace {

[[noreturn]] void fail_open(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) throw FileNotFound(path);
    throw std::runtime_error("Permission denied accessing MFT file: " + path);
}

}  // namespace

#ifdef _WIN32

MappedFile::MappedFile(const std::string& path) {
    const std::filesystem::path p(path);
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) fail_open(path);
    file_ = f;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(f, &sz)) {
        CloseHandle(f);
        throw std::runtime_error("cannot stat MFT file: " + path);
    }
    size_ = static_cast<std::size_t>(sz.QuadPart);
    if (size_ == 0) return;  // nothing to map

    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) {
        CloseHandle(f);
        throw std::runtime_error("cannot map MFT file: " + path);
    }
    mapping_ = m;
    void* view = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!view) {
        CloseHandle(m);
        CloseHandle(f);
        throw std::runtime_error("cannot map MFT file: " + path);
    }
    data_ = static_cast<const std::uint8_t*>(view);
}

MappedFile::~MappedFile() {
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(mapping_);
    if (file_) CloseHandle(file_);
}

#else

MappedFile::MappedFile(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) fail_open(path);
    struct stat st{};
    if (::fstat(fd_, &st) != 0) {
        ::close(fd_);
        throw std::runtime_error("cannot stat MFT file: " + path);
    }
    size_ = static_cast<std::size_t>(st.st_size);
    if (size_ == 0) return;
    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (p == MAP_FAILED) {
        ::close(fd_);
        throw std::runtime_error("cannot map MFT file: " + path);
    }
    data_ = static_cast<const std::uint8_t*>(p);
}

MappedFile::~MappedFile() {
    if (data_) ::munmap(const_cast<std::uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
}

#endif

}  // namespace mft
