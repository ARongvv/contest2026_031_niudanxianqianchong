#include "smart_home_cpu_debug.h"

#include <nuttx/config.h>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>

void smart_home_cpu_debug_log(const char *role)
{
#if defined(CONFIG_SMART_HOME_DEMO_DEBUG_LOG) && defined(CONFIG_SMP)
    cpu_set_t cpuset;
    unsigned int mask;
    int ret;

    CPU_ZERO(&cpuset);
    ret = pthread_getaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    if (ret != 0) {
        printf("[smart_home_cpu] role=%s cpu=%d affinity=unavailable ret=%d\n",
               role ? role : "unknown", sched_getcpu(), ret);
        return;
    }

    mask = CPU_ISSET(0, &cpuset) ? 1u : 0u;
#if CONFIG_SMP_NCPUS > 1
    if (CPU_ISSET(1, &cpuset)) {
        mask |= 2u;
    }
#endif

    printf("[smart_home_cpu] role=%s cpu=%d mask=0x%x\n",
           role ? role : "unknown",
           sched_getcpu(),
           mask);
#else
    (void)role;
#endif
}
