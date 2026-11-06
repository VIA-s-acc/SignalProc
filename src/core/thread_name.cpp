#include "core/thread_name.hpp"

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>

#include <string>
#endif

namespace signals::core {

void set_thread_name(const char* name) {
#if defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), std::string{name}.substr(0, 15).c_str());
#else
    (void)name;
#endif
}

}  // namespace signals::core
