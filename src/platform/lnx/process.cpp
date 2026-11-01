#include <platform/lnx/process.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/uio.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <elf.h>

namespace platform::lnx
{

process_session::~process_session()
{
    detach();
}

bool process_session::attach(std::string_view executable)
{
    detach();
    std::string target(executable);
    if (target.ends_with(".exe"))
        target.resize(target.size() - 4);
    DIR* proc = ::opendir("/proc");
    if (!proc)
        return false;

    struct dirent* entry;
    while ((entry = ::readdir(proc)) != nullptr)
    {
        if (entry->d_type != DT_DIR)
            continue;
        const char* name = entry->d_name;
        if (*name < '0' || *name > '9')
            continue;
        const std::uint32_t pid = static_cast<std::uint32_t>(std::atoi(name));
        char path[256];
        std::snprintf(path, sizeof(path), "/proc/%u/comm", pid);
        std::ifstream comm(path);
        if (!comm.is_open())
            continue;
        std::string comm_name;
        std::getline(comm, comm_name);
        if (comm_name == target || comm_name.find(target) == 0)
        {
            m_pid = pid;
            ::closedir(proc);
            if (!read_maps())
            {
                detach();
                return false;
            }
            return true;
        }
        std::snprintf(path, sizeof(path), "/proc/%u/exe", pid);
        char exe_path[4096];
        const ssize_t len = ::readlink(path, exe_path, sizeof(exe_path) - 1);
        if (len > 0)
        {
            exe_path[len] = '\0';
            const std::string exe_str(exe_path);
            const auto slash = exe_str.rfind('/');
            const std::string basename = slash != std::string::npos ? exe_str.substr(slash + 1) : exe_str;
            if (basename == target || basename.find(target) == 0)
            {
                m_pid = pid;
                ::closedir(proc);
                if (!read_maps())
                {
                    detach();
                    return false;
                }
                return true;
            }
        }
    }
    return false;
}

bool process_session::attach(std::wstring_view executable)
{
    std::string name;
    name.reserve(executable.size());
    for (const wchar_t character : executable)
    {
        if (character > 0x7f)
            return false;
        name.push_back(static_cast<char>(character));
    }
    return attach(name);
}

void process_session::detach() noexcept
{
    m_pid = 0;
    m_modules.clear();
    m_maps_loaded = false;
}

bool process_session::copy(std::uintptr_t address, void* destination, std::size_t bytes) const noexcept
{
    if (!m_pid || bytes == 0)
        return false;
    struct iovec local{destination, bytes};
    struct iovec remote{reinterpret_cast<void*>(address), bytes};
    const ssize_t result = ::process_vm_readv(static_cast<pid_t>(m_pid), &local, 1, &remote, 1, 0);
    return result == static_cast<ssize_t>(bytes);
}

std::string process_session::load_text(std::uintptr_t address, std::size_t capacity) const
{
    std::string buffer(capacity, '\0');
    if (!copy(address, buffer.data(), capacity))
        return {};
    const auto nul = buffer.find('\0');
    if (nul != std::string::npos)
        buffer.resize(nul);
    return buffer;
}

bool process_session::read_maps() const
{
    m_modules.clear();
    m_maps_loaded = true;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/maps", m_pid);
    std::ifstream maps(path);
    if (!maps.is_open())
        return false;

    std::string line;
    while (std::getline(maps, line))
    {
        if (line.find('/') == std::string::npos)
            continue;
        unsigned long start{}, end{};
        char perms[8]{};
        unsigned long offset{};
        unsigned major{}, minor{};
        unsigned long inode{};
        char pathname[4096]{};
        const int parsed = std::sscanf(line.c_str(), "%lx-%lx %7s %lx %x:%x %lu %4095[^\n]",
                                       &start, &end, perms, &offset, &major, &minor, &inode, pathname);
        if (parsed < 7 || line.find('/') == std::string::npos)
            continue;
        std::string trimmed(pathname);
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
            trimmed.erase(trimmed.begin());
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
            trimmed.pop_back();
        if (trimmed.empty())
            continue;
        const auto slash = trimmed.rfind('/');
        const std::string basename = slash != std::string::npos ? trimmed.substr(slash + 1) : trimmed;
        const auto found = std::find_if(m_modules.begin(), m_modules.end(), [&](const module_record& mod)
        {
            return mod.name == basename;
        });
        if (found == m_modules.end())
            m_modules.push_back({basename, start, end - start});
        else
        {
            const auto module_end = std::max(found->base + found->size, static_cast<std::uintptr_t>(end));
            found->base = std::min(found->base, static_cast<std::uintptr_t>(start));
            found->size = module_end - found->base;
        }
    }
    return true;
}

process_session::module_record process_session::find_module(std::string_view name) const
{
    if (!m_maps_loaded && !read_maps())
        return {};
    const std::string target(name);
    for (const auto& mod : m_modules)
    {
        if (mod.name == target)
            return mod;
        if (mod.name.find(target) == 0)
            return mod;
    }
    return {};
}

std::uintptr_t process_session::module_base(std::string_view module) const
{
    return find_module(module).base;
}

std::size_t process_session::module_image_size(std::uintptr_t module) const
{
    for (const auto& mod : m_modules)
        if (mod.base == module)
            return mod.size;
    return 0;
}

std::uintptr_t process_session::scan_signature(std::uintptr_t module, std::string_view signature) const
{
    const auto size = module_image_size(module);
    if (!module || !size || signature.empty())
        return 0;

    struct pattern_byte
    {
        std::uint8_t value{};
        bool wildcard{};
    };
    std::vector<pattern_byte> pattern;
    std::istringstream stream{std::string(signature)};
    std::string token;
    while (stream >> token)
    {
        if (token == "?" || token == "??")
            pattern.push_back({0, true});
        else if (token.size() == 2 && token.find('?') == std::string::npos)
            pattern.push_back({static_cast<std::uint8_t>(std::strtoul(token.c_str(), nullptr, 16)), false});
        else
            return 0;
    }
    if (pattern.empty())
        return 0;

    const std::size_t chunk_size = 4 * 1024 * 1024;
    std::vector<std::uint8_t> buffer(chunk_size + pattern.size() - 1);

    for (std::size_t offset = 0; offset < size; offset += chunk_size)
    {
        const std::size_t read_size = std::min(buffer.size(), size - offset);
        if (!copy(module + offset, buffer.data(), read_size))
            break;
        const std::size_t scan_end = read_size >= pattern.size() ? read_size - pattern.size() + 1 : 0;
        for (std::size_t i = 0; i < scan_end; ++i)
        {
            bool match = true;
            for (std::size_t j = 0; j < pattern.size(); ++j)
            {
                if (!pattern[j].wildcard && buffer[i + j] != pattern[j].value)
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return module + offset + i;
        }
    }
    return 0;
}

std::uintptr_t process_session::scan_code_signature(std::uintptr_t module, std::string_view signature) const
{
    return scan_signature(module, signature);
}

std::uintptr_t process_session::scan_unique_code_signature(std::uintptr_t module, std::string_view signature,
                                                           std::size_t& matches) const
{
    matches = 0;
    if (!module || signature.empty())
        return 0;
    struct pattern_byte
    {
        std::uint8_t value{};
        bool wildcard{};
    };
    std::vector<pattern_byte> pattern;
    std::istringstream stream{std::string(signature)};
    std::string token;
    while (stream >> token)
    {
        if (token == "?" || token == "??")
            pattern.push_back({0, true});
        else if (token.size() == 2 && token.find('?') == std::string::npos)
            pattern.push_back({static_cast<std::uint8_t>(std::strtoul(token.c_str(), nullptr, 16)), false});
        else
            return 0;
    }
    const auto size = module_image_size(module);
    if (pattern.empty() || pattern.size() > size)
        return 0;
    constexpr std::size_t chunk_size = 4 * 1024 * 1024;
    std::vector<std::uint8_t> buffer(chunk_size + pattern.size() - 1);
    std::uintptr_t unique{};
    for (std::size_t offset = 0; offset < size; offset += chunk_size)
    {
        const auto read_size = std::min(buffer.size(), size - offset);
        if (!copy(module + offset, buffer.data(), read_size))
            return 0;
        const auto scan_end = read_size >= pattern.size() ? read_size - pattern.size() + 1 : 0;
        for (std::size_t i = 0; i < scan_end; ++i)
        {
            bool match = true;
            for (std::size_t j = 0; j < pattern.size(); ++j)
                if (!pattern[j].wildcard && buffer[i + j] != pattern[j].value)
                {
                    match = false;
                    break;
                }
            if (!match)
                continue;
            ++matches;
            unique = module + offset + i;
            if (matches > 1)
                return 0;
        }
    }
    return matches == 1 ? unique : 0;
}

std::uintptr_t process_session::find_pointer(std::uintptr_t module, std::uintptr_t value,
                                             std::uint32_t required_characteristics) const
{
    (void)required_characteristics;
    const auto size = module_image_size(module);
    if (!module || !size)
        return 0;
    const std::size_t chunk_size = 4 * 1024 * 1024;
    std::vector<std::uint8_t> buffer(chunk_size);
    for (std::size_t offset = 0; offset < size; offset += chunk_size)
    {
        const std::size_t read_size = std::min(chunk_size, size - offset);
        if (!copy(module + offset, buffer.data(), read_size))
            break;
        for (std::size_t i = 0; i + sizeof(std::uintptr_t) <= read_size; i += sizeof(std::uintptr_t))
        {
            std::uintptr_t candidate{};
            std::memcpy(&candidate, buffer.data() + i, sizeof(candidate));
            if (candidate == value)
                return module + offset + i;
        }
    }
    return 0;
}

std::uintptr_t process_session::locate_vtable(std::uintptr_t module, std::string_view class_name) const
{
    static_cast<void>(module);
    static_cast<void>(class_name);
    return 0;
}

std::uintptr_t process_session::locate_vtable_object(std::uintptr_t module, std::string_view class_name) const
{
    static_cast<void>(module);
    static_cast<void>(class_name);
    return 0;
}

} // namespace platform::lnx
