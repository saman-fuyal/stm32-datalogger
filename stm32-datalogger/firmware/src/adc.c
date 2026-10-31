/*
 * The ADC measures the internal temperature sensor (channel 16) and the 1.20 V
 * reference (channel 17). DMA1 channel 1 copies every result into adc_buf without the CPU.
 * Each reading starts the ADC again (see adc_start).
 */
#include "adc.h"
#include "board.h"
#include "config.h"

#define ADC_PAIRS 32U               /* one pair = one temperature + one reference value */

static volatile uint16_t adc_buf[2U * ADC_PAIRS];
static uint8_t  adc_ok = 0;
static int32_t  ts_offset_c100 = TS_OFFSET_C100;
static int32_t  vref_x10 = VREF_X10;

#define RCC_APB2RSTR    REG32(0x4002100C)
#define RCC_ADC1RST     (1U << 9)
#define DMA_ISR_TCIF1   (1U << 1)   /* channel 1 transfer complete */
#define DMA_IFCR_CGIF1  (1U << 0)   /* clears all channel 1 flags */

/* Sets up the ADC and DMA from scratch and fills adc_buf once (64 conversions, about 4 ms).
   This runs for every reading: after the first STOP the ADC did not convert any more (first
   the values froze, then a restart of the conversions alone gave nan). So each reading resets
   the whole ADC, calibrates it, takes 32 pairs and switches it off again (adc_stop). */
static int adc_start(void)
{
    RCC_APB2RSTR |= RCC_ADC1RST;                       /* ADC back to its reset state */
    RCC_APB2RSTR &= ~RCC_ADC1RST;

    /* DMA channel 1: every ADC result goes into adc_buf */
    DMA1_CCR1   = 0;
    DMA1_IFCR   = DMA_IFCR_CGIF1;
    DMA1_CPAR1  = ADC1_DR_ADDR;
    DMA1_CMAR1  = (uint32_t)(uintptr_t)adc_buf;
    DMA1_CNDTR1 = 2U * ADC_PAIRS;
    DMA1_CCR1   = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PSIZE16 | DMA_CCR_MSIZE16;
    DMA1_CCR1  |= DMA_CCR_EN;

    ADC1_CR1   = ADC_CR1_SCAN;                         /* convert the whole list each time */
    ADC1_SMPR1 = (7U << 18) | (7U << 21);              /* channels 16, 17: 239.5 cycles (slow = accurate) */
    ADC1_SQR1  = (1U << 20);                           /* the list has 2 entries */
    ADC1_SQR3  = (16U << 0) | (17U << 5);              /* 1st = temperature sensor, 2nd = 1.20 V reference */
    ADC1_CR2   = ADC_CR2_TSVREFE | ADC_CR2_ADON;       /* switch the sensors on, wake the ADC */
    wait_ms(2);

    ADC1_CR2 |= ADC_CR2_RSTCAL;                        /* the ADC calibrates itself first */
    if (!wait_bits(&ADC1_CR2, ADC_CR2_RSTCAL, 0, 50U)) {
        return 0;
    }
    ADC1_CR2 |= ADC_CR2_CAL;
    if (!wait_bits(&ADC1_CR2, ADC_CR2_CAL, 0, 50U)) {
        return 0;
    }

    ADC1_CR2 |= ADC_CR2_CONT | ADC_CR2_DMA | ADC_CR2_EXTSEL_SWSTART | ADC_CR2_EXTTRIG;
    ADC1_CR2 |= ADC_CR2_SWSTART;                       /* start converting */
    return wait_bits(&DMA1_ISR, DMA_ISR_TCIF1, 1, 20U); /* the buffer is full of new values */
}

/* ADC, temperature sensor and DMA off between readings (also saves current in STOP) */
static void adc_stop(void)
{
    ADC1_CR2  = 0;
    DMA1_CCR1 = 0;
}

int adc_init(void)
{
    RCC_AHBENR  |= RCC_AHBENR_DMA1EN;
    RCC_APB2ENR |= RCC_APB2ENR_ADC1EN;
    adc_ok = (uint8_t)adc_start();
    adc_stop();
    return adc_ok;
}

int adc_running(void)
{
    return adc_ok;
}

int adc_read_chip(int32_t *vdd_mv, int32_t *temp_raw_c100)
{
    uint32_t sum_t = 0, sum_v = 0, vs;
    uint8_t i;

    if (!adc_ok) {
        return 0;
    }
    if (!adc_start()) {
        adc_stop();
        return 0;
    }
    adc_stop();
    for (i = 0; i < ADC_PAIRS; i++) {
        sum_t += adc_buf[2U * i];
        sum_v += adc_buf[2U * i + 1U];
    }
    if (sum_t == 0U || sum_v == 0U) {
        return 0;
    }
    /* the reference is about 1.20 V (VREF_X10), so its reading tells us the real supply voltage */
    *vdd_mv = (int32_t)((uint32_t)vref_x10 * 4095UL * ADC_PAIRS / sum_v / 10UL);
    /* voltage of the temperature sensor in tenths of a millivolt (the supply cancels out) */
    vs = sum_t * 12000UL / sum_v;
    /* datasheet: 1.43 V at 25 C, falling 4.3 mV per degree */
    *temp_raw_c100 = 2500 + ((int32_t)14300 - (int32_t)vs) * 100 / 43;
    return (*vdd_mv > 2000 && *vdd_mv < 3700);
}

int32_t adc_ts_offset(void)
{
    return ts_offset_c100;
}

void adc_set_ts_offset(int32_t c100)
{
    ts_offset_c100 = c100;
}

int32_t adc_vref_x10(void)
{
    return vref_x10;
}

void adc_set_vref_x10(int32_t v)
{
    vref_x10 = v;
}
