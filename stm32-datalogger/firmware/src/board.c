/* Board set-up: clocks, pins, LED, debug pin, and time helpers on top of the FreeRTOS tick.
   SysTick now belongs to FreeRTOS (port.c sets it to 1 ms), so there is no handler here. */
#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

#define SPIN_MS 2U                  /* wait_bits: busy-wait this long before it starts sleeping */

int rtos_running(void)
{
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

/* Before the scheduler runs there is no tick, so time comes from the cycle counter.
   (Nothing waits that early, but this keeps every timeout working anyway.) */
uint32_t tick_ms(void)
{
    if (rtos_running()) {
        return (uint32_t)xTaskGetTickCount();
    }
    return DWT_CYCCNT / (CPU_HZ / 1000U);
}

void wait_ms(uint32_t ms)
{
    if (rtos_running()) {
        vTaskDelay(pdMS_TO_TICKS(ms));          /* this task sleeps, others run */
    } else {
        uint32_t t0 = tick_ms();
        while ((tick_ms() - t0) < ms) { }
    }
}

int wait_bits(volatile uint32_t *reg, uint32_t mask, uint8_t want_set, uint32_t timeout_ms)
{
    uint32_t t0 = tick_ms();
    while (((*reg & mask) != 0U) != (want_set != 0U)) {
        uint32_t waited = tick_ms() - t0;
        if (waited >= timeout_ms) {
            return 0;
        }
        if (waited >= SPIN_MS && rtos_running()) {
            vTaskDelay(1);                      /* a slow wait: let other tasks use the CPU */
        }
    }
    return 1;
}

/* slot = pin number inside its register (0..7 for CRL, pin - 8 for CRH) */
void gpio_config(volatile uint32_t *cr, uint8_t slot, uint32_t mode)
{
    *cr = (*cr & ~(0xFU << (4U * slot))) | (mode << (4U * slot));
}

void led_on(void)     { GPIOC_ODR &= ~LED_PIN; }     /* the LED is active-low */
void led_off(void)    { GPIOC_ODR |= LED_PIN; }
void led_toggle(void) { GPIOC_ODR ^= LED_PIN; }

void debug_pin(int high)
{
    GPIOB_BSRR = high ? DEBUG_PIN : (DEBUG_PIN << 16);
}

void board_init(void)
{
    RCC_APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_IOPCEN |
                   RCC_APB2ENR_USART1EN | RCC_APB2ENR_SPI1EN;
    RCC_APB1ENR |= RCC_APB1ENR_I2C1EN;
    RCC_AHBENR  |= RCC_AHBENR_DMA1EN;                       /* DMA1: ADC and UART transmit */

    gpio_config(&GPIOA_CRH, 1, GPIO_AF_PP_50MHZ);            /* PA9  = UART TX */
    GPIOA_BSRR = (1U << 10);                                 /* pull-up: no false wake-ups */
    gpio_config(&GPIOA_CRH, 2, GPIO_IN_PULL);                /* PA10 = UART RX, idle high */
    gpio_config(&GPIOB_CRL, 6, GPIO_AF_OD_50MHZ);            /* PB6  = I2C SCL */
    gpio_config(&GPIOB_CRL, 7, GPIO_AF_OD_50MHZ);            /* PB7  = I2C SDA */

    GPIOA_BSRR = (1U << SD_CS_PIN);                          /* CS high before it becomes an output */
    gpio_config(&GPIOA_CRL, SD_CS_PIN, GPIO_OUT_PP_2MHZ);    /* PA4 = SD CS */
    gpio_config(&GPIOA_CRL, 5, GPIO_AF_PP_50MHZ);            /* PA5 = SCK */
    GPIOA_BSRR = (1U << SPI_MISO_PIN);                       /* pull-up: no card reads 0xFF */
    gpio_config(&GPIOA_CRL, SPI_MISO_PIN, GPIO_IN_PULL);     /* PA6 = MISO */
    gpio_config(&GPIOA_CRL, 7, GPIO_AF_PP_50MHZ);            /* PA7 = MOSI */

    gpio_config(&GPIOC_CRH, 5, GPIO_OUT_PP_2MHZ);            /* PC13 = LED */
    led_off();
    debug_pin(0);
    gpio_config(&GPIOB_CRH, 4, GPIO_OUT_PP_2MHZ);            /* PB12 = debug pin */

    /* A debugger session (CubeIDE Run/Debug) can leave hardware breakpoints, watchpoints and
       "vector catch" switched on; a normal reset does not clear them, only power-off does.
       Without a debugger attached, hitting one of them is a hard fault. So: clear them. */
    if (!(DHCSR & DHCSR_C_DEBUGEN)) {
        uint8_t n;
        FP_CTRL = FP_CTRL_KEY;                               /* breakpoint unit off */
        for (n = 0; n < 4U; n++) {
            DWT_FUNCTION(n) = 0;                             /* watchpoints off */
        }
        DEMCR &= ~(DEMCR_VC_MASK | DEMCR_MON_MASK);
    }
    DEMCR |= DEMCR_TRCENA;                                   /* cycle counter for timing */
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
}
