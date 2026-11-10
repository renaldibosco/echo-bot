// ============================================================
//  Echo Bot - settings you can change
// ============================================================
#pragma once

// ---------- Wi-Fi made by Echo (your phone connects to this) ----------
#define ECHO_WIFI_NAME   "Echo-Bot"
#define ECHO_WIFI_PASS   "echo1234"      // at least 8 characters
#define OWNER_NAME       "Reno"

// ---------- Buzzer type ----------
// true  = passive buzzer (plays notes / melodies, needed for Air Piano)
// false = active buzzer (only one beep sound)
#define BUZZER_PASSIVE   true

// ---------- Touch input ----------
// 1 = ESP32 built-in touch (a wire to a coin / foil / copper tape on top)
// 0 = push button or TTP223 module (button between pin and GND)
#define USE_TOUCH_PIN    1

// ---------- Pins ----------
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define PIN_SDA     8
  #define PIN_SCL     9
  #define PIN_TRIG    5
  #define PIN_ECHO    6
  #define PIN_TOUCH   4
  #define PIN_BUZZER  7
  #define PIN_BATT    1
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
  #undef  USE_TOUCH_PIN
  #define USE_TOUCH_PIN 0               // C3 has no touch pins
  #define PIN_SDA     8
  #define PIN_SCL     9
  #define PIN_TRIG    5
  #define PIN_ECHO    6
  #define PIN_TOUCH   3                 // push button to GND
  #define PIN_BUZZER  7
  #define PIN_BATT    1
#else                                   // normal ESP32 DevKit (WROOM-32)
  #define PIN_SDA     21
  #define PIN_SCL     22
  #define PIN_TRIG    5
  #define PIN_ECHO    18                // through a 10k resistor
  #define PIN_TOUCH   4                 // T0
  #define PIN_BUZZER  25
  #define PIN_BATT    34                // middle of 10k + 10k divider
#endif

// ---------- Behaviour ----------
#define SLEEP_AFTER_MS     (3UL * 60UL * 1000UL)   // sleepy eyes after 3 min alone
#define PRESENCE_CM        60                      // "Reno is here" distance
#define LONG_PRESS_MS      650                     // hold to open the menu
#define BATT_DIVIDER       2.0f                    // 10k + 10k = 2.0
