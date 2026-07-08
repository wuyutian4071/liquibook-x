#include "file_reader.hpp"

#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "byteorder.hpp"

namespace liquibook::itch {

ItchFileReader::ItchFileReader(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw std::system_error(
            errno, std::generic_category(), "ItchFileReader: failed to open " + path.string());
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        const int saved_errno = errno;
        ::close(fd);
        throw std::system_error(saved_errno,
                                std::generic_category(),
                                "ItchFileReader: fstat failed for " + path.string());
    }
    size_ = static_cast<std::size_t>(st.st_size);

    if (size_ == 0) {
        ::close(fd);
        return; // empty file: next_raw_message() returns nullopt immediately
    }

    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    const int mmap_errno = errno;
    ::close(fd); // safe once mmap has returned; the mapping itself remains valid
    if (mapped == MAP_FAILED) {
        throw std::system_error(mmap_errno,
                                std::generic_category(),
                                "ItchFileReader: mmap failed for " + path.string());
    }
    data_ = static_cast<const std::byte*>(mapped);
}

ItchFileReader::~ItchFileReader() {
    reset();
}

void ItchFileReader::reset() noexcept {
    if (data_ != nullptr) {
        ::munmap(const_cast<void*>(static_cast<const void*>(data_)), size_);
    }
    data_ = nullptr;
    size_ = 0;
    offset_ = 0;
}

ItchFileReader::ItchFileReader(ItchFileReader&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
      offset_(std::exchange(other.offset_, 0)) {}

ItchFileReader& ItchFileReader::operator=(ItchFileReader&& other) noexcept {
    if (this != &other) {
        reset();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        offset_ = std::exchange(other.offset_, 0);
    }
    return *this;
}

std::optional<std::span<const std::byte>> ItchFileReader::next_raw_message() noexcept {
    if (data_ == nullptr || offset_ + 2 > size_) {
        return std::nullopt;
    }
    const std::uint16_t msg_len = read_u16_be(data_ + offset_);
    const std::size_t msg_start = offset_ + 2;
    if (msg_start + msg_len > size_) {
        return std::nullopt; // truncated trailing record; stop cleanly rather than read OOB
    }
    offset_ = msg_start + msg_len;
    return std::span<const std::byte>(data_ + msg_start, msg_len);
}

std::optional<DecodedMessage> ItchFileReader::next_message() noexcept {
    const auto raw = next_raw_message();
    if (!raw) {
        return std::nullopt;
    }
    return decode(*raw);
}

} // namespace liquibook::itch
