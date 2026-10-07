// Read-only memory-mapped file (Win32 MapViewOfFile / POSIX mmap).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace mft {

class MappedFile {
public:
    // Throws FileNotFound if the path does not exist, std::runtime_error otherwise.
    explicit MappedFile(const std::string& path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const std::uint8_t* data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    void* file_ = nullptr;     // Win32 file handle
    void* mapping_ = nullptr;  // Win32 mapping handle
    int fd_ = -1;              // POSIX descriptor
};

}  // namespace mft
