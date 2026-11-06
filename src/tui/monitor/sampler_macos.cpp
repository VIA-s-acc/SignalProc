#include <libproc.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>

#include "tui/monitor/sampler.hpp"

namespace signals::tui {

core::Result<ProcessSample> sample_process() {
    ProcessSample s;
    s.wall_seconds = detail::wall_seconds();
    s.cpu_seconds = detail::process_cpu_seconds();
    // resident_size keeps pages malloc has freed as reusable, so after a large load it stays far above what the
    // process holds; top and Activity Monitor report the physical footprint instead.
    rusage_info_v4 usage{};
    if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&usage)) != 0)
        return core::make_error(core::Errc::io_error, "proc_pid_rusage failed");
    s.rss_bytes = usage.ri_phys_footprint;
    s.peak_rss_bytes = std::max(usage.ri_lifetime_max_phys_footprint, usage.ri_phys_footprint);
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t thread_count = 0;
    if (task_threads(mach_task_self(), &threads, &thread_count) == KERN_SUCCESS) {
        s.threads = thread_count;
        for (mach_msg_type_number_t i = 0; i < thread_count; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), thread_count * sizeof(thread_t));
    }
    natural_t cpus = 0;
    processor_info_array_t loads = nullptr;
    mach_msg_type_number_t load_count = 0;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpus, &loads, &load_count) == KERN_SUCCESS) {
        const auto* l = reinterpret_cast<processor_cpu_load_info_t>(loads);
        for (natural_t i = 0; i < cpus; ++i) {
            const std::uint64_t busy = l[i].cpu_ticks[CPU_STATE_USER] + l[i].cpu_ticks[CPU_STATE_SYSTEM] + l[i].cpu_ticks[CPU_STATE_NICE];
            s.core_busy_ticks.push_back(busy);
            s.core_total_ticks.push_back(busy + l[i].cpu_ticks[CPU_STATE_IDLE]);
        }
        vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(loads), load_count * sizeof(integer_t));
    }
    return s;
}

std::uint64_t physical_memory_bytes() {
    std::uint64_t bytes = 0;
    std::size_t size = sizeof bytes;
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
}

}  // namespace signals::tui
