#ifndef LED_H
#define LED_H

#include <Arduino.h>

class LED {
public:
    void begin();
    void setMode(const String& mode, uint8_t r, uint8_t g, uint8_t b, uint16_t speedMs = 120);
    void update();
    void turnOff();

private:
    String mode_ = "idle";
    uint8_t red_ = 0;
    uint8_t green_ = 160;
    uint8_t blue_ = 32;
    uint16_t speedMs_ = 120;
    uint8_t chaseIndex_ = 0;
    uint8_t pulseValue_ = 0;
    uint8_t hueOffset_ = 0;
    int8_t pulseDirection_ = 1;
    unsigned long lastFrameMs_ = 0;
};

#endif
