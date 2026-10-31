/* ADC1 + DMA1 channel 1: chip temperature and supply voltage, switched on for each reading. */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

int     adc_init(void);         /* 1 = running */
int     adc_running(void);
/* vdd_mv = supply in millivolts, temp_raw_c100 = chip temperature before the offset.
   Returns 0 if the ADC is not running or the values are impossible. */
int     adc_read_chip(int32_t *vdd_mv, int32_t *temp_raw_c100);
int32_t adc_ts_offset(void);    /* hundredths of a degree */
void    adc_set_ts_offset(int32_t c100);
int32_t adc_vref_x10(void);     /* internal reference, tenths of a millivolt ('vddcal') */
void    adc_set_vref_x10(int32_t v);

#endif
