#include <diagnostics/report.hpp>
#include <diagnostics/build_identity.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace diagnostics
{
namespace
{
constexpr std::size_t per_file_limit = 128 * 1024;
constexpr std::size_t total_limit = 1024 * 1024;

bool looks_like_path(std::string_view line) noexcept
{
    if (line.find('/') != std::string_view::npos)
        return true;
    return line.find("~/") != std::string_view::npos;
}

void append_section(std::string& out, const std::filesystem::path& file, std::string_view title)
{
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return;
    std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (text.size() > per_file_limit)
        text.erase(0, text.size() - per_file_limit);
    out += "--- ";
    out += title;
    out += '\n';
    std::size_t start{};
    while (start < text.size())
    {
        auto end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string_view line(text.data() + start, end - start);
        if (looks_like_path(line))
            out += "[line withheld: possible file path]\n";
        else
        {
            out.append(line);
            out += '\n';
        }
        start = end + 1;
    }
}
}

std::filesystem::path desktop_directory()
{
    if (const char* desktop = std::getenv("XDG_DESKTOP_DIR"); desktop && *desktop)
        return desktop;
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / "Desktop";
    return {};
}

report_result export_report(const std::filesystem::path& logs_directory,
                            const std::filesystem::path& destination_directory, std::string_view summary) noexcept
{
    report_result result;
    try
    {
        if (destination_directory.empty())
        {
            result.error = 2;
            return result;
        }
        std::string out = "vesta-diagnostic-report schema=1 version=" VESTA_JOURNAL_VERSION
                          " build=" VESTA_JOURNAL_REVISION "\n";
        if (!summary.empty())
        {
            out += "summary ";
            out.append(summary);
            out += '\n';
        }
        if (!logs_directory.empty())
        {
            append_section(out, logs_directory / "crash.previous.log", "crash.previous.log");
            append_section(out, logs_directory / "crash.log", "crash.log");
            for (int index = 3; index >= 1; --index)
                append_section(out, logs_directory / ("journal." + std::to_string(index) + ".log"),
                               "journal." + std::to_string(index) + ".log");
            append_section(out, logs_directory / "journal.log", "journal.log");
        }
        if (out.size() > total_limit)
            out.erase(0, out.size() - total_limit);
        std::error_code error;
        std::filesystem::create_directories(destination_directory, error);
        if (error)
        {
            result.error = static_cast<std::uint32_t>(error.value());
            return result;
        }
        result.path = destination_directory / report_file_name;
        std::ofstream file(result.path, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            result.error = 5;
            return result;
        }
        file.write(out.data(), static_cast<std::streamsize>(out.size()));
        result.ok = file.good();
        if (!result.ok)
            result.error = 5;
    }
    catch (...)
    {
        result.error = 12;
    }
    return result;
}
} // namespace diagnostics
