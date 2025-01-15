#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>

#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)

/**
 * @enum    atl_led_behaviour_e
 * @brief   LED behaviour.
 */
typedef enum {
    ATL_LED_DISABLED,
    ATL_LED_ENABLED_FAILS,
    ATL_LED_ENABLED_COMM_FAILS,
    ATL_LED_ENABLED_FULL,    
} atl_led_behaviour_e;

/**
 * @typedef atl_led_rgb_color_t
 * @brief System configuration structure.
 */
typedef struct {
    uint8_t red; /**< LED red value. */
    uint8_t green; /**< LED green value. */
    uint8_t blue; /**< LED blue value. */
} atl_led_rgb_color_t;

/**
 * @brief Get the led behaviour string object
 * @param behaviour 
 * @return Function enum const* 
 */
const char* atl_led_get_behaviour_str(atl_led_behaviour_e behaviour);

/**
 * @brief Get the led behaviour string object
 * @param behaviour_str 
 * @return Function enum
 */
atl_led_behaviour_e atl_led_get_behaviour(char* behaviour_str);

/**
 * @fn atl_led_builtin_init(void)
 * @brief Initialize led builtin task
 * @return Led strip config handle
*/
void atl_led_builtin_init(void);

/**
 * @fn atl_led_builtin_toogle(void)
 * @brief Toggle led builtin
*/
void atl_led_builtin_toogle(void);

/**
 * @fn atl_led_builtin_blink(uint8_t times, uint16_t interval, uint8_t red, uint8_t green, uint8_t blue)
 * @brief Blink led builtin
 * @param [in] times blink times
 * @param [in] interval interval between blinks
 * @param [in] red red value (0..255)
 * @param [in] green green value (0..255)
 * @param [in] blue blue value (0..255)
*/
void atl_led_builtin_blink(uint8_t times, uint16_t interval, uint8_t red, uint8_t green, uint8_t blue);

/**
 * @fn atl_led_set_color(uint8_t red, uint8_t green, uint8_t blue)
 * @brief Set led builtin color
 * @param [in] red red value (0..255)
 * @param [in] green green value (0..255)
 * @param [in] blue blue value (0..255)
*/
void atl_led_set_color(uint8_t red, uint8_t green, uint8_t blue);

/**
 * @fn atl_led_set_enabled(bool status)
 * @brief Enabled/disabled led builtin
 * @param [in] status enabled or disabled led builtin
*/
void atl_led_set_enabled(bool status);

#ifdef __cplusplus
}
#endif