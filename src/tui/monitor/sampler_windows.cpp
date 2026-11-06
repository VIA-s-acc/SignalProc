#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <vector>

#include "tui/monitor/sampler.hpp"

namespace signals::tui {

namespace {

std::uint64_t ticks(FILETIME t) { return static_cast<std::uint64_t>(t.dwHighDateTime) << 32 | t.dwLowDateTime; }

std::uint64_t ticks(LARGE_INTEGER t) { return static_cast<std::uint64_t>(t.QuadPart); }

unsigned process_threads() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    const DWORD self = GetCurrentProcessId();
    unsigned threads = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry))
        if (entry.th32ProcessID == self) {
            threads = entry.cntThreads;
            break;
        }
    CloseHandle(snapshot);
    return threads;
}

}  // namespace

namespace detail {

double process_cpu_seconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    return static_cast<double>(ticks(kernel) + ticks(user)) * 1e-7;
}

}  // namespace detail

core::Result<ProcessSample> sample_process() {
    ProcessSample s;
    s.wall_seconds = detail::wall_seconds();
    s.cpu_seconds = detail::process_cpu_seconds();
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof memory;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof memory))
        return core::make_error(core::Errc::io_error, "GetProcessMemoryInfo failed");
    s.rss_bytes = memory.PrivateUsage;
    s.peak_rss_bytes = std::max<std::size_t>(memory.PeakPagefileUsage, memory.PrivateUsage);
    s.threads = process_threads();
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> cores(std::max<DWORD>(1, info.dwNumberOfProcessors));
    ULONG written = 0;
    if (NtQuerySystemInformation(SystemProcessorPerformanceInformation, cores.data(),
                                 static_cast<ULONG>(cores.size() * sizeof cores[0]), &written) >= 0) {
        cores.resize(written / sizeof cores[0]);
        for (const auto& core : cores) {
            // KernelTime already includes IdleTime.
            const std::uint64_t total = ticks(core.KernelTime) + ticks(core.UserTime);
            s.core_busy_ticks.push_back(total - ticks(core.IdleTime));
            s.core_total_ticks.push_back(total);
        }
    }
    return s;
}

std::uint64_t physical_memory_bytes() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
}

}  // namespace signals::tui
