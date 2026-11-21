// T-Deck: RT-Thread's ulog. Errors and warnings go to USB serial; the rest is dropped.
#pragma once
#include <Arduino.h>
#define LOG_E(fmt, ...) Serial.printf("E/" LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...) Serial.printf("W/" LOG_TAG " " fmt "\n", ##__VA_ARGS__)
#define LOG_I(fmt, ...) do {} while (0)
#define LOG_D(fmt, ...) do {} while (0)
