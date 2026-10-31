/*
 * FreeRTOS settings for the data logger (STM32F103C8, 8 MHz internal clock).
 * Kernel: FreeRTOS V11.3.1 (FreeRTOS 202604.01 LTS), port GCC/ARM_CM3, memory: heap_4 (one static array).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

/* ---------- clock and scheduler ---------- */
#define configCPU_CLOCK_HZ                      8000000UL
#define configTICK_RATE_HZ                      1000U       /* 1 tick = 1 ms */
#define configUSE_PREEMPTION                    1           /* a higher priority task runs at once */
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configMAX_PRIORITIES                    5           /* 0 = idle ... 4 = start-up task */
#define configMINIMAL_STACK_SIZE                128U        /* words (4 bytes), used by the idle task */
#define configMAX_TASK_NAME_LEN                 8
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configSTACK_DEPTH_TYPE                  uint16_t

/* ---------- low power: tickless idle, our own sleep function (power.c) ---------- */
#define configUSE_TICKLESS_IDLE                 2
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP   2
void power_idle(uint32_t expected_ms);
#define portSUPPRESS_TICKS_AND_SLEEP(x)         power_idle((uint32_t)(x))

/* ---------- memory: heap_4 uses one array of this size, no malloc from the C library ---------- */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   (10U * 1024U)
#define configUSE_MALLOC_FAILED_HOOK            1

/* ---------- features used ---------- */
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1           /* a task may take the same lock twice */
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_STREAM_BUFFERS                1
#define configUSE_TIMERS                        0
#define configUSE_CO_ROUTINES                   0
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0

/* ---------- checks ---------- */
/* method 2: on every task switch, check that the last 16 bytes of the stack still hold the
   fill pattern. Catches most overflows; the hook prints the task name and resets. */
#define configCHECK_FOR_STACK_OVERFLOW          2
void app_assert_failed(const char *file, int line);
#define configASSERT(x)  do { if (!(x)) { app_assert_failed(__FILE__, __LINE__); } } while (0)

/* ---------- 'tasks' command: state, stack and CPU time of every task ---------- */
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0           /* we print the table ourselves */
#define configRECORD_STACK_HIGH_ADDRESS         1           /* lets CubeIDE's FreeRTOS views show stack use */
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t    /* CPU cycles, never wraps */
uint64_t timing_runtime_counter(void);                      /* timing.c, from the DWT cycle counter */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()            /* already started in board_init */
#define portGET_RUN_TIME_COUNTER_VALUE()        timing_runtime_counter()

/* ---------- interrupt priorities (STM32F1: 4 bits, 0 = most urgent, 15 = least) ---------- */
#define configPRIO_BITS                         4
/* the kernel's own interrupts (SysTick, PendSV) run at the lowest priority */
#define configKERNEL_INTERRUPT_PRIORITY         (15U << (8U - configPRIO_BITS))
/* interrupts that call ...FromISR() functions must have priority 5..15 (never 0..4) */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (5U << (8U - configPRIO_BITS))

/* ---------- functions included ---------- */
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1   /* needed so portMAX_DELAY waits forever */
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xSemaphoreGetMutexHolder        1
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0

/* ---------- the port's interrupt handlers use the vector table names ---------- */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif
