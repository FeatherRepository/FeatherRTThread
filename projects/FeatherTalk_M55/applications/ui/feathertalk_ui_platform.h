#ifndef FEATHERTALK_UI_PLATFORM_H
#define FEATHERTALK_UI_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

bool ft_platform_brightness_available(void);
int ft_platform_set_brightness(uint8_t percent);
uint8_t ft_platform_get_brightness(void);

#endif /* FEATHERTALK_UI_PLATFORM_H */
