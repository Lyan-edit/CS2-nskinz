#pragma once

#include <algorithm>
#include <cstdint>
#include <istream>
#include <limits>
#include <string>

namespace nskinzcs2
{
// Validate against the opened file, before trusting a VPK allocation size.
inline bool SeekToAvailableRange(std::istream& stream, const std::uint64_t offset,
    const std::uint64_t length)
{
    if (!stream || offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) return false;
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0 || offset > static_cast<std::uint64_t>(end) ||
        length > static_cast<std::uint64_t>(end) - offset) return false;
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    return static_cast<bool>(stream);
}

inline bool ReadVpkTreeString(std::istream& stream, std::string& value,
    const std::uint64_t tree_end)
{
    value.clear();
    const auto position = stream.tellg();
    if (position < 0 || static_cast<std::uint64_t>(position) >= tree_end) return false;
    const auto available = std::min<std::uint64_t>(tree_end - static_cast<std::uint64_t>(position), 4097);
    char character = 0;
    for (std::uint64_t index = 0; index < available && stream.get(character); ++index)
    {
        if (character == '\0') return true;
        if (value.size() == 4096) return false;
        value.push_back(character);
    }
    return false;
}
}
