#include <stdint.h>

#define RCC_APB2ENR (*(volatile uint32_t *)0x40021018)
#define GPIOC_CRH   (*(volatile uint32_t *)0x40011004)
#define GPIOC_ODR   (*(volatile uint32_t *)0x4001100C)

int main(void)
{
    RCC_APB2ENR |= (1U << 4);        /* turn on the clock for GPIO port C */
    GPIOC_CRH   &= ~(0xFU << 20);    /* clear the 4 config bits of PC13   */
    GPIOC_CRH   |=  (0x2U << 20);    /* PC13: output, 2 MHz, push-pull    */

    while (1) {
        GPIOC_ODR ^= (1U << 13);     /* flip PC13 (the blue LED)          */
        for (volatile uint32_t i = 0; i < 400000; i++) { }  /* crude delay */
    }
}
