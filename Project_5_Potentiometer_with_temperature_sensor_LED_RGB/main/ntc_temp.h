#ifndef NTC_TEMP_H
#define NTC_TEMP_H

#include <stdint.h>

void ntc_temp_init(void);
// Devuelve la temperatura en grados Celsius (0..100)
uint8_t ntc_get_temperature_celsius(void); 

#endif // NTC_TEMP_H