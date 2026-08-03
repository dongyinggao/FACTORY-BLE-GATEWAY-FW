#include "system_diagnostics.h"

#include <stdio.h>

#include "esp_console.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SYSTEM_DIAGNOSTICS_MAX_TASKS 24U

static void print_memory_line(const char *name, uint32_t caps)
{
    size_t total = heap_caps_get_total_size(caps);
    size_t free = heap_caps_get_free_size(caps);
    size_t used = total >= free ? total - free : 0U;
    size_t largest = heap_caps_get_largest_free_block(caps);
    unsigned int used_tenths = total == 0U ? 0U : (unsigned int)((used * 1000U) / total);

    printf("| %-14.14s | %10u | %5u.%1u | %10u | %10u | %10u |\n", name,
           (unsigned int)used, used_tenths / 10U, used_tenths % 10U,
           (unsigned int)free, (unsigned int)total, (unsigned int)largest);
}

void system_diagnostics_print_memory(void)
{
    printf("\nMemory usage\n");
    printf("+----------------+------------+---------+------------+------------+------------+\n");
    printf("| Pool           |   Used [B] |  Used %% |   Free [B] |  Total [B] |Largest [B] |\n");
    printf("+----------------+------------+---------+------------+------------+------------+\n");
    print_memory_line("internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    print_memory_line("DMA internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    print_memory_line("psram", MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    printf("+----------------+------------+---------+------------+------------+------------+\n");
    printf("DMA internal is a capability subset of internal memory.\n");
}

#if CONFIG_SYSTEM_DIAGNOSTICS_TASKS_ENABLED
static const char *task_state_text(eTaskState state)
{
    static const char *const states[] = { "Running", "Ready", "Blocked", "Suspended", "Deleted", "Invalid" };

    return state <= eInvalid ? states[state] : "Unknown";
}
#endif

bool system_diagnostics_tasks_enabled(void)
{
#if CONFIG_SYSTEM_DIAGNOSTICS_TASKS_ENABLED
    return true;
#else
    return false;
#endif
}

bool system_diagnostics_print_tasks(void)
{
#if CONFIG_SYSTEM_DIAGNOSTICS_TASKS_ENABLED
    static TaskStatus_t tasks[SYSTEM_DIAGNOSTICS_MAX_TASKS];
    configRUN_TIME_COUNTER_TYPE total_runtime = 0;
    UBaseType_t count = uxTaskGetSystemState(tasks, SYSTEM_DIAGNOSTICS_MAX_TASKS, &total_runtime);

    printf("\nTask snapshot\n");
    printf("+------------------+------------+----------+---------+------------------+\n");
    printf("| Name             | State      | Priority | CPU %%   |Stack min free [B]|\n");
    printf("+------------------+------------+----------+---------+------------------+\n");
    for (UBaseType_t index = 0; index < count; ++index) {
        unsigned int cpu_tenths = total_runtime == 0U ? 0U :
                                  (unsigned int)(((uint64_t)tasks[index].ulRunTimeCounter * 1000U) /
                                                 total_runtime);

        printf("| %-16.16s | %-10.10s | %8u | %5u.%1u | %16u |\n", tasks[index].pcTaskName,
               task_state_text(tasks[index].eCurrentState),
               (unsigned int)tasks[index].uxCurrentPriority,
               cpu_tenths / 10U, cpu_tenths % 10U,
               (unsigned int)tasks[index].usStackHighWaterMark);
    }
    printf("+------------------+------------+----------+---------+------------------+\n");
    printf("CPU %% is the runtime-statistics share since boot; totals may exceed 100%% on two cores.\n");
    printf("Stack min free is the minimum remaining stack since each task was created.\n");
    if (count == SYSTEM_DIAGNOSTICS_MAX_TASKS) {
        printf("warning=task snapshot reached limit %u\n", SYSTEM_DIAGNOSTICS_MAX_TASKS);
    }
    return true;
#else
    return false;
#endif
}

static int system_memory_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    system_diagnostics_print_memory();
    return 0;
}

void system_diagnostics_register_console(void)
{
    const esp_console_cmd_t memory_command_definition = {
        .command = "sysmem",
        .help = "generic system diagnostics: memory snapshot",
        .func = &system_memory_command,
    };

    (void)esp_console_cmd_register(&memory_command_definition);
}
