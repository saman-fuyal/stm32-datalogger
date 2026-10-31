/*
 * Board: STM32F103C8T6 (Blue Pill), 8 MHz internal clock (HSI), registers only (no HAL).
 * This file holds every register address and bit name, plus the small board functions
 * (pins, LED, 1 ms tick) that every other module uses.
 *
 * Pins:  PA9  = UART TX   PA10 = UART RX     (CP2102)
 *        PB6  = I2C SCL   PB7  = I2C SDA     (BMP280 + MPU6050)
 *        PA4  = SD CS     PA5  = SCK   PA6 = MISO   PA7 = MOSI
 *        PC13 = LED (active low)   PB12 = debug pin (high while the sensors are read)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

/* ---------- clocks and power ---------- */
#define RCC_AHBENR  REG32(0x40021014)
#define RCC_APB2ENR REG32(0x40021018)
#define RCC_APB1ENR REG32(0x4002101C)
#define RCC_BDCR    REG32(0x40021020)
#define RCC_CSR     REG32(0x40021024)   /* reset flags: why the chip restarted */
#define RCC_CSR_RMVF      (1U << 24)    /* write 1: clear the flags */
#define RCC_CSR_PINRSTF   (1U << 26)    /* reset pin (button, or any reset) */
#define RCC_CSR_PORRSTF   (1U << 27)    /* power-on, or the supply dropped too low */
#define RCC_CSR_SFTRSTF   (1U << 28)    /* software reset (our fault handler) */
#define RCC_CSR_IWDGRSTF  (1U << 29)    /* independent watchdog */
#define RCC_CSR_WWDGRSTF  (1U << 30)    /* window watchdog */
#define RCC_CSR_LPWRRSTF  (1U << 31)    /* low-power reset */
#define PWR_CR      REG32(0x40007000)

#define RCC_AHBENR_DMA1EN     (1U << 0)
#define RCC_APB2ENR_AFIOEN    (1U << 0)
#define RCC_APB2ENR_IOPAEN    (1U << 2)
#define RCC_APB2ENR_IOPBEN    (1U << 3)
#define RCC_APB2ENR_IOPCEN    (1U << 4)
#define RCC_APB2ENR_ADC1EN    (1U << 9)
#define RCC_APB2ENR_SPI1EN    (1U << 12)
#define RCC_APB2ENR_USART1EN  (1U << 14)
#define RCC_APB1ENR_I2C1EN    (1U << 21)
#define RCC_APB1ENR_BKPEN     (1U << 27)
#define RCC_APB1ENR_PWREN     (1U << 28)
#define RCC_BDCR_LSEON        (1U << 0)
#define RCC_BDCR_LSERDY       (1U << 1)
#define RCC_BDCR_RTCSEL_LSE   (1U << 8)
#define RCC_BDCR_RTCEN        (1U << 15)
#define RCC_BDCR_BDRST        (1U << 16)
#define PWR_CR_LPDS           (1U << 0)   /* STOP: voltage regulator in low-power mode */
#define PWR_CR_PDDS           (1U << 1)   /* 0 = STOP, 1 = STANDBY when deep sleep */
#define PWR_CR_CWUF           (1U << 2)   /* clear the wake-up flag */
#define PWR_CR_DBP            (1U << 8)

/* ---------- backup register and real time clock ---------- */
#define BKP_DR1     REG32(0x40006C04)
#define BKP_DR2     REG32(0x40006C08)   /* 16 bits each: DR2 + DR3 hold the clock's base time */
#define BKP_DR3     REG32(0x40006C0C)
#define BKP_DR4     REG32(0x40006C10)   /* why our fault handler restarted the chip */
#define BKP_DR5     REG32(0x40006C14)   /* DR5..DR8: address of the last hard fault (PC, LR) */
#define BKP_DR6     REG32(0x40006C18)
#define BKP_DR7     REG32(0x40006C1C)
#define BKP_DR8     REG32(0x40006C20)
#define BKP_DR9     REG32(0x40006C24)   /* fault: CFSR bits 0..15 (memory and bus fault causes) */
#define BKP_DR10    REG32(0x40006C28)   /* fault: which stack, and the first letter of the task */
#define SCB_CFSR    REG32(0xE000ED28)   /* why the fault happened (bits per cause) */
#define RTC_CRH     REG32(0x40002800)
#define RTC_CRL     REG32(0x40002804)
#define RTC_PRLH    REG32(0x40002808)
#define RTC_PRLL    REG32(0x4000280C)
#define RTC_CNTH    REG32(0x40002818)
#define RTC_CNTL    REG32(0x4000281C)
#define RTC_ALRH    REG32(0x40002820)
#define RTC_ALRL    REG32(0x40002824)

#define RTC_CRH_ALRIE (1U << 1)
#define RTC_CRL_ALRF  (1U << 1)
#define RTC_CRL_RSF   (1U << 3)
#define RTC_CRL_CNF   (1U << 4)
#define RTC_CRL_RTOFF (1U << 5)

/* ---------- GPIO ---------- */
#define GPIOA_CRL   REG32(0x40010800)
#define GPIOA_CRH   REG32(0x40010804)
#define GPIOA_ODR   REG32(0x4001080C)
#define GPIOA_BSRR  REG32(0x40010810)   /* low half sets a pin, high half clears it */
#define GPIOB_CRL   REG32(0x40010C00)
#define GPIOB_CRH   REG32(0x40010C04)
#define GPIOB_IDR   REG32(0x40010C08)
#define GPIOB_BSRR  REG32(0x40010C10)
#define GPIOC_CRH   REG32(0x40011004)
#define GPIOC_ODR   REG32(0x4001100C)

#define GPIO_IN_FLOATING  0x4U
#define GPIO_IN_PULL      0x8U          /* pull-up if the ODR bit is 1 */
#define GPIO_OUT_PP_2MHZ  0x2U
#define GPIO_OUT_OD_2MHZ  0x6U
#define GPIO_AF_PP_50MHZ  0xBU
#define GPIO_AF_OD_50MHZ  0xFU
#define LED_PIN           (1U << 13)
#define SD_CS_PIN         4U            /* PA4 */
#define SPI_MISO_PIN      6U            /* PA6 */
#define DEBUG_PIN         (1U << 12)    /* PB12 */

/* ---------- UART ---------- */
#define USART1_SR   REG32(0x40013800)
#define USART1_DR   REG32(0x40013804)
#define USART1_BRR  REG32(0x40013808)
#define USART1_CR1  REG32(0x4001380C)
#define USART1_CR3  REG32(0x40013814)
#define USART1_DR_ADDR 0x40013804U

#define USART_SR_ERRORS   0x07U         /* parity, framing, noise */
#define USART_SR_ORE      (1U << 3)
#define USART_SR_RXNE     (1U << 5)
#define USART_SR_TC       (1U << 6)     /* transmission complete: the last bit has left */
#define USART_SR_TXE      (1U << 7)
#define USART_CR1_RE      (1U << 2)
#define USART_CR1_TE      (1U << 3)
#define USART_CR1_RXNEIE  (1U << 5)
#define USART_CR1_UE      (1U << 13)
#define USART_CR3_DMAT    (1U << 7)     /* the UART asks the DMA for the next byte */

/* ---------- interrupt controller (NVIC) and system control ---------- */
#define NVIC_ISER0  REG32(0xE000E100)   /* enable bits for interrupts 0..31 */
#define NVIC_ISER1  REG32(0xE000E104)   /* enable bits for interrupts 32..63 */
/* one priority byte per interrupt; only the top 4 bits are used on the STM32F1 */
#define NVIC_IPR(n) (*(volatile uint8_t *)(0xE000E400U + (n)))
#define IRQ_DMA1_CH4      14U
#define IRQ_USART1        37U
#define IRQ_EXTI15_10     40U
#define IRQ_RTC_ALARM     41U
#define SCB_ICSR    REG32(0xE000ED04)
#define SCB_ICSR_PENDSTCLR (1U << 25)   /* forget a pending SysTick interrupt */
#define SCB_SCR     REG32(0xE000ED10)
#define SCB_SCR_SLEEPDEEP  (1U << 2)    /* WFI enters STOP instead of SLEEP */
#define SCB_AIRCR   REG32(0xE000ED0C)
#define SCB_AIRCR_RESET   0x05FA0004U   /* key + SYSRESETREQ: restart the chip */

/* ---------- SysTick (owned by FreeRTOS; power.c restarts it after STOP) ---------- */
#define SYST_CSR    REG32(0xE000E010)
#define SYST_RVR    REG32(0xE000E014)
#define SYST_CVR    REG32(0xE000E018)
#define SYST_CSR_RUN      7U            /* enable, interrupt on, CPU clock */

/* ---------- debugger present? (C_DEBUGEN is set while a debugger is attached) ---------- */
#define DHCSR       REG32(0xE000EDF0)
#define DHCSR_C_DEBUGEN   (1U << 0)
#define SCB_HFSR    REG32(0xE000ED2C)   /* hard fault status: VECTTBL, FORCED, DEBUGEVT */
#define HFSR_VECTTBL      (1U << 1)
#define HFSR_FORCED       (1U << 30)
#define HFSR_DEBUGEVT     (1U << 31)
#define FP_CTRL     REG32(0xE0002000)   /* flash patch / hardware breakpoint unit */
#define FP_CTRL_KEY       (1U << 1)     /* must be 1 when writing; ENABLE (bit 0) = 0 turns it off */
#define DWT_FUNCTION(n) REG32(0xE0001028U + 16U * (n))   /* watchpoint n: 0 = off */
#define DEMCR_VC_MASK     0x000007F1U   /* vector catch bits (halt on reset/faults) */
#define DEMCR_MON_MASK    0x000F0000U   /* debug monitor bits */

/* ---------- EXTI: external interrupt and wake-up lines ---------- */
#define AFIO_EXTICR3 REG32(0x40010010)  /* picks the port for lines 8..11 (0 = port A) */
#define EXTI_IMR    REG32(0x40010400)
#define EXTI_EMR    REG32(0x40010404)   /* event mask: wakes WFE without an interrupt */
#define EXTI_RTSR   REG32(0x40010408)
#define EXTI_FTSR   REG32(0x4001040C)
#define EXTI_PR     REG32(0x40010414)
#define EXTI_LINE10       (1U << 10)    /* PA10 = UART RX: a key press */
#define EXTI_LINE17       (1U << 17)    /* RTC alarm */

/* ---------- independent watchdog (own 40 kHz clock, keeps running in STOP) ---------- */
#define IWDG_KR     REG32(0x40003000)
#define IWDG_PR     REG32(0x40003004)
#define IWDG_RLR    REG32(0x40003008)
#define IWDG_SR     REG32(0x4000300C)
#define IWDG_KEY_START    0xCCCCU
#define IWDG_KEY_UNLOCK   0x5555U
#define IWDG_KEY_FEED     0xAAAAU
#define DBGMCU_CR   REG32(0xE0042004)
#define DBGMCU_IWDG_STOP  (1U << 8)     /* watchdog pauses while a debugger halts the CPU */

/* ---------- DWT cycle counter (counts every CPU clock: 8 per microsecond) ---------- */
#define DEMCR       REG32(0xE000EDFC)
#define DWT_CTRL    REG32(0xE0001000)
#define DWT_CYCCNT  REG32(0xE0001004)
#define DEMCR_TRCENA       (1U << 24)
#define DWT_CTRL_CYCCNTENA (1U << 0)
#define CPU_HZ             8000000U

/* ---------- I2C1 ---------- */
#define I2C1_CR1    REG32(0x40005400)
#define I2C1_CR2    REG32(0x40005404)
#define I2C1_DR     REG32(0x40005410)
#define I2C1_SR1    REG32(0x40005414)
#define I2C1_SR2    REG32(0x40005418)
#define I2C1_CCR    REG32(0x4000541C)
#define I2C1_TRISE  REG32(0x40005420)

#define I2C_CR1_PE      (1U << 0)
#define I2C_CR1_START   (1U << 8)
#define I2C_CR1_STOP    (1U << 9)
#define I2C_CR1_ACK     (1U << 10)
#define I2C_CR1_SWRST   (1U << 15)
#define I2C_SR1_SB      (1U << 0)
#define I2C_SR1_ADDR    (1U << 1)
#define I2C_SR1_BTF     (1U << 2)
#define I2C_SR1_RXNE    (1U << 6)
#define I2C_SR1_TXE     (1U << 7)
#define I2C_SR1_AF      (1U << 10)

/* ---------- SPI1 ---------- */
#define SPI1_CR1    REG32(0x40013000)
#define SPI1_SR     REG32(0x40013008)
#define SPI1_DR     REG32(0x4001300C)

#define SPI_CR1_MSTR  (1U << 2)
#define SPI_CR1_SPE   (1U << 6)
#define SPI_CR1_SSI   (1U << 8)
#define SPI_CR1_SSM   (1U << 9)
#define SPI_SR_RXNE   (1U << 0)
#define SPI_SR_TXE    (1U << 1)
#define SPI_SR_BSY    (1U << 7)

/* ---------- DMA1 channel 1 and ADC1 ---------- */
#define DMA1_ISR     REG32(0x40020000)
#define DMA1_IFCR    REG32(0x40020004)
#define DMA1_CCR1    REG32(0x40020008)
#define DMA1_CNDTR1  REG32(0x4002000C)
#define DMA1_CPAR1   REG32(0x40020010)
#define DMA1_CMAR1   REG32(0x40020014)
#define DMA1_CCR4    REG32(0x40020044)   /* channel 4 = USART1 TX */
#define DMA1_CNDTR4  REG32(0x40020048)
#define DMA1_CPAR4   REG32(0x4002004C)
#define DMA1_CMAR4   REG32(0x40020050)
#define DMA_CCR_EN       (1U << 0)
#define DMA_CCR_TCIE     (1U << 1)      /* interrupt when the transfer is complete */
#define DMA_CCR_DIR      (1U << 4)      /* 1 = memory to peripheral */
#define DMA_CCR_CIRC     (1U << 5)
#define DMA_CCR_MINC     (1U << 7)
#define DMA_CCR_PSIZE16  (1U << 8)
#define DMA_CCR_MSIZE16  (1U << 10)
#define DMA_ISR_TCIF4    (1U << 13)     /* channel 4 transfer complete */
#define DMA_ISR_TEIF4    (1U << 15)     /* channel 4 transfer error */
#define DMA_IFCR_CGIF4   (1U << 12)     /* clears all channel 4 flags */

#define ADC1_CR1     REG32(0x40012404)
#define ADC1_CR2     REG32(0x40012408)
#define ADC1_SMPR1   REG32(0x4001240C)
#define ADC1_SQR1    REG32(0x4001242C)
#define ADC1_SQR3    REG32(0x40012434)
#define ADC1_DR_ADDR 0x4001244CU
#define ADC_CR1_SCAN            (1U << 8)
#define ADC_CR2_ADON            (1U << 0)
#define ADC_CR2_CONT            (1U << 1)
#define ADC_CR2_CAL             (1U << 2)
#define ADC_CR2_RSTCAL          (1U << 3)
#define ADC_CR2_DMA             (1U << 8)
#define ADC_CR2_EXTSEL_SWSTART  (7U << 17)
#define ADC_CR2_EXTTRIG         (1U << 20)
#define ADC_CR2_SWSTART         (1U << 22)
#define ADC_CR2_TSVREFE         (1U << 23)

/* ---------- board functions (board.c) ---------- */
void     board_init(void);                 /* clocks, pins, LED off, cycle counter on */
void     gpio_config(volatile uint32_t *cr, uint8_t slot, uint32_t mode);
void     led_on(void);
void     led_off(void);
void     led_toggle(void);
void     debug_pin(int high);               /* PB12, for the logic analyzer */
uint32_t tick_ms(void);                    /* milliseconds since power-on (the FreeRTOS tick) */
void     wait_ms(uint32_t ms);              /* sleeps: other tasks run meanwhile */
int      rtos_running(void);                /* 1 once the scheduler has started */
/* wait until (*reg & mask) is non-zero (want_set = 1) or zero (want_set = 0).
   Returns 0 if that did not happen within timeout_ms. Spins for the first 2 ms, then
   sleeps 1 ms between checks so that lower priority tasks can run. */
int      wait_bits(volatile uint32_t *reg, uint32_t mask, uint8_t want_set, uint32_t timeout_ms);

#endif
