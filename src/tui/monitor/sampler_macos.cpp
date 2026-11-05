#include <mach/mach.h>

#include "tui/monitor/sampler.hpp"

namespace signals::tui {

core::Result<ProcessSample> sample_process() {
    ProcessSample s;
    s.wall_seconds = detail::wall_seconds();
    s.cpu_seconds = detail::process_cpu_seconds();
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return core::make_error(core::Errc::io_error, "task_info failed");
    s.rss_bytes = info.resident_size;
    s.peak_rss_bytes = info.resident_size_max;
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

}  // namespace signals::tui
