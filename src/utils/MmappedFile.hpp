#pragma once

// =============================================================================
// Portable memory-mapped read-only file wrapper.
//
// Replaces the "read the whole file into a std::vector<uint8_t>" pattern with
// an OS-backed mmap. The kernel page-faults the file into virtual memory on
// demand, so a 4 GB volume doesn't need a 4 GB transient std::ifstream buffer
// during ingest. On move, ownership of the mapping transfers; the destructor
// unmaps + closes the file handle. Read-only, single-mapping, single-process
// use only -- no shared write semantics, no offset/range arguments. That's the
// minimum surface needed for the disk-paging track in DICOM / NIfTI loaders.
//
// Windows path uses CreateFile + CreateFileMappingW + MapViewOfFile.
// POSIX path uses open + mmap. The public API is byte-buffer-shaped (data, size,
// valid) so the caller doesn't see either OS underneath.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace utils {

class MmappedFile {
public:
    MmappedFile() = default;
    ~MmappedFile();

    MmappedFile(MmappedFile&& other) noexcept;
    MmappedFile& operator=(MmappedFile&& other) noexcept;

    MmappedFile(const MmappedFile&) = delete;
    MmappedFile& operator=(const MmappedFile&) = delete;

    // Map the file read-only. Returns false on failure (missing file, mmap
    // error); the object is then left in the default empty state.
    bool open(const std::string& path);

    // Drop the mapping early. Idempotent.
    void close();

    const uint8_t* data() const noexcept { return static_cast<const uint8_t*>(m_addr); }
    size_t         size() const noexcept { return m_size; }
    bool           valid() const noexcept { return m_addr != nullptr; }

private:
    void* m_addr = nullptr;     // start of mapped region (mmap return / MapViewOfFile)
    size_t m_size = 0;          // file length in bytes
#ifdef _WIN32
    void* m_fileHandle = nullptr;   // HANDLE from CreateFile
    void* m_mapHandle  = nullptr;   // HANDLE from CreateFileMappingW
#else
    int m_fd = -1;
#endif
};

} // namespace utils
