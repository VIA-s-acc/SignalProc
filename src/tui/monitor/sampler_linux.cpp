#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <unistd.h>

#include "tui/monitor/sampler.hpp"

namespace signals::tui {

namespace {

std::size_t status_kib(const std::string& text, const std::string& key) {
    const auto at = text.find(key + ":");
    if (at == std::string::npos) return 0;
    std::istringstream in{text.substr(at + key.size() + 1)};
    std::size_t kib = 0;
    in >> kib;
    return kib;
}

}  // namespace

core::Result<ProcessSample> sample_process() {
    ProcessSample s;
    s.wall_seconds = detail::wall_seconds();
    s.cpu_seconds = detail::process_cpu_seconds();
    std::ifstream stat_file{"/proc/self/stat"};
    std::string stat{std::istreambuf_iterator<char>{stat_file}, std::istreambuf_iterator<char>{}};
    const auto close = stat.rfind(')');
    if (close == std::string::npos) return core::make_error(core::Errc::io_error, "cannot read /proc/self/stat");
    std::istringstream fields{stat.substr(close + 2)};
    std::string field;
    for (int index = 3; fields >> field; ++index)
        if (index == 20) {
            s.threads = static_cast<unsigned>(std::stoul(field));
            break;
        }
    std::ifstream status_file{"/proc/self/status"};
    const std::string status{std::istreambuf_iterator<char>{status_file}, std::istreambuf_iterator<char>{}};
    s.rss_bytes = status_kib(status, "VmRSS") * 1024;
    s.peak_rss_bytes = status_kib(status, "VmHWM") * 1024;
    const double tick = static_cast<double>(sysconf(_SC_CLK_TCK));
    for (const auto& entry : std::filesystem::directory_iterator{"/proc/self/task", std::filesystem::directory_options::skip_permission_denied}) {
        std::ifstream task_file{entry.path() / "stat"};
        const std::string task{std::istreambuf_iterator<char>{task_file}, std::istreambuf_iterator<char>{}};
        const auto open = task.find('('), shut = task.rfind(')');
        if (open == std::string::npos || shut == std::string::npos || tick <= 0) continue;
        ThreadTime t;
        t.id = std::stoull(entry.path().filename().string());
        t.name = task.substr(open + 1, shut - open - 1);
        std::istringstream rest{task.substr(shut + 2)};
        std::string value;
        std::uint64_t utime = 0, stime = 0;
        for (int index = 3; rest >> value; ++index) {
            if (index == 14) utime = std::stoull(value);
            if (index == 15) {
                stime = std::stoull(value);
                break;
            }
        }
        t.cpu_seconds = static_cast<double>(utime + stime) / tick;
        s.thread_times.push_back(std::move(t));
    }
    std::ifstream io_file{"/proc/self/io"};
    const std::string io{std::istreambuf_iterator<char>{io_file}, std::istreambuf_iterator<char>{}};
    s.disk_read_bytes = status_kib(io, "read_bytes");
    s.disk_written_bytes = status_kib(io, "write_bytes");
    std::ifstream cpu_file{"/proc/stat"};
    for (std::string line; std::getline(cpu_file, line);) {
        if (line.rfind("cpu", 0) != 0 || line.size() < 4 || line[3] < '0' || line[3] > '9') continue;
        std::istringstream in{line};
        std::string name;
        std::uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
        in >> name >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        const std::uint64_t busy = user + nice + system + irq + softirq + steal;
        s.core_busy_ticks.push_back(busy);
        s.core_total_ticks.push_back(busy + idle + iowait);
    }
    return s;
}

std::uint64_t physical_memory_bytes() {
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    return pages > 0 && page > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page) : 0;
}

}  // namespace signals::tui
