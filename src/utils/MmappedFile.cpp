#include "MmappedFile.hpp"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

#include <utility>

namespace utils {

MmappedFile::~MmappedFile() {
    close();
}

MmappedFile::MmappedFile(MmappedFile&& other) noexcept
    : m_addr(other.m_addr),
      m_size(other.m_size) {
#ifdef _WIN32
    m_fileHandle = other.m_fileHandle;
    m_mapHandle  = other.m_mapHandle;
    other.m_fileHandle = nullptr;
    other.m_mapHandle  = nullptr;
#else
    m_fd = other.m_fd;
    other.m_fd = -1;
#endif
    other.m_addr = nullptr;
    other.m_size = 0;
}

MmappedFile& MmappedFile::operator=(MmappedFile&& other) noexcept {
    if (this != &other) {
        close();
        m_addr = other.m_addr;
        m_size = other.m_size;
#ifdef _WIN32
        m_fileHandle = other.m_fileHandle;
        m_mapHandle  = other.m_mapHandle;
        other.m_fileHandle = nullptr;
        other.m_mapHandle  = nullptr;
#else
        m_fd = other.m_fd;
        other.m_fd = -1;
#endif
        other.m_addr = nullptr;
        other.m_size = 0;
    }
    return *this;
}

bool MmappedFile::open(const std::string& path) {
    close();
#ifdef _WIN32
    HANDLE fh = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (fh == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(fh, &sz) || sz.QuadPart <= 0) {
        CloseHandle(fh);
        return false;
    }
    HANDLE mh = CreateFileMappingW(fh, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mh == nullptr) {
        CloseHandle(fh);
        return false;
    }
    void* addr = MapViewOfFile(mh, FILE_MAP_READ, 0, 0, 0);
    if (addr == nullptr) {
        CloseHandle(mh);
        CloseHandle(fh);
        return false;
    }
    m_fileHandle = fh;
    m_mapHandle  = mh;
    m_addr = addr;
    m_size = static_cast<size_t>(sz.QuadPart);
    return true;
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st{};
    if (::fstat(fd, &st) < 0 || st.st_size <= 0) {
        ::close(fd);
        return false;
    }
    void* addr = ::mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        ::close(fd);
        return false;
    }
    m_fd   = fd;
    m_addr = addr;
    m_size = static_cast<size_t>(st.st_size);
    return true;
#endif
}

void MmappedFile::close() {
#ifdef _WIN32
    if (m_addr) { UnmapViewOfFile(m_addr); m_addr = nullptr; }
    if (m_mapHandle) { CloseHandle(m_mapHandle); m_mapHandle = nullptr; }
    if (m_fileHandle) { CloseHandle(m_fileHandle); m_fileHandle = nullptr; }
#else
    if (m_addr && m_size > 0) { ::munmap(m_addr, m_size); m_addr = nullptr; }
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
#endif
    m_size = 0;
}

} // namespace utils
