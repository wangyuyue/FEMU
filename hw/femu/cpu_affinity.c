#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

// Function to set CPU affinity
void set_cpu_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);       // Clear the CPU set
    CPU_SET(core_id, &cpuset); // Set the CPU core

    pid_t pid = getpid(); // Get the process ID

    if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
}

void set_cpu_frequency(int core_id, const char* frequency) {
    char command[256];
    snprintf(command, sizeof(command), "sudo cpufreq-set -c %d -f %s", core_id, frequency);
    
    if (system(command) != 0) {
        perror("system");
        exit(EXIT_FAILURE);
    }
}

int main() {
    int core_id = 0; // Specify the core you want to check (e.g., core 0)

    // set_cpu_frequency(core_id, "2.0GHz");
    set_cpu_affinity(core_id);
    while(1) {
        sleep(1);
        printf("Hello from core %d\n", core_id);
    }
}