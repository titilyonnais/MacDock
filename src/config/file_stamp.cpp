#include "file_stamp.h"

#include <windows.h>

namespace md {

FileStamp fileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return {};
    FileStamp s;
    s.exists = true;
    s.written = (std::uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    s.size = (std::uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return s;
}

std::vector<FileStamp> ConfigWatch::stamps() const {
    std::vector<FileStamp> out;
    out.reserve(files_.size());
    for (const auto& f : files_) out.push_back(fileStamp(f));
    return out;
}

} // namespace md
