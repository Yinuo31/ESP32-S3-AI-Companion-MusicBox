#include "led.h"

#include "config.h"

#include <FastLED.h>

namespace {
CRGB g_leds[NUM_LEDS];
constexpr uint8_t kBrightness = 48;
}

// 初始化灯带并设置默认亮度与熄灭状态。
void LED::begin() {
    FastLED.addLeds<WS2812, LED_PIN, GRB>(g_leds, NUM_LEDS);
    FastLED.setBrightness(kBrightness);
    turnOff();
    Serial.println("[LED] Ready.");
}
// 设置 LED 动效模式、颜色和刷新速度。
void LED::setMode(const String& mode, uint8_t r, uint8_t g, uint8_t b, uint16_t speedMs) {
    mode_ = mode;
    red_ = r;
    green_ = g;
    blue_ = b;
    speedMs_ = speedMs == 0 ? 120 : speedMs;
    lastFrameMs_ = 0;
    chaseIndex_ = 0;
    pulseValue_ = 0;
    hueOffset_ = 0;
    pulseDirection_ = 1;
}
// 按当前模式更新一帧灯效并输出到灯带。
void LED::update() {
    const unsigned long now = millis();
    // 未达到刷新间隔时直接返回。
    if (now - lastFrameMs_ < speedMs_) {
        return;
    }
    lastFrameMs_ = now;
    hueOffset_ += 5;

    if (mode_ == "solid") {
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            const uint8_t level = sin8(hueOffset_ + i * 24);
            g_leds[i] = blend(CRGB::Black, CRGB(red_, green_, blue_), level);
        }
        FastLED.setBrightness(kBrightness);
    } else if (mode_ == "processing") {
        fadeToBlackBy(g_leds, NUM_LEDS, 96);
        g_leds[chaseIndex_] += CRGB(red_, green_, blue_);
        g_leds[(chaseIndex_ + NUM_LEDS - 1) % NUM_LEDS] += CRGB(red_ / 3, green_ / 3, blue_ / 3);
        g_leds[(chaseIndex_ + NUM_LEDS - 2) % NUM_LEDS] += CRGB(red_ / 6, green_ / 6, blue_ / 6);
        chaseIndex_ = (chaseIndex_ + 1) % NUM_LEDS;
        FastLED.setBrightness(kBrightness);
    } else if (mode_ == "speaking") {
        int nextValue = static_cast<int>(pulseValue_) + pulseDirection_ * 20;
        if (nextValue > 220) nextValue = 220;
        if (nextValue < 20) nextValue = 20;
        pulseValue_ = static_cast<uint8_t>(nextValue);
        if (pulseValue_ == 220 || pulseValue_ == 20) {
            pulseDirection_ *= -1;
        }
        fill_solid(g_leds, NUM_LEDS, CRGB(red_, green_, blue_));
        FastLED.setBrightness(pulseValue_);
    } else if (mode_ == "mood_happy") {
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            g_leds[i] = CHSV(hueOffset_ + i * 20, 220, 255);
        }
        g_leds[random8(NUM_LEDS)] = CRGB::White;
        FastLED.setBrightness(96);
    } else if (mode_ == "mood_calm") {
        const uint8_t wave = beatsin8(6, 35, 90);
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            const uint8_t level = qsub8(wave, abs8((NUM_LEDS / 2) - i) * 8);
            g_leds[i] = blend(CRGB(0, 10, 25), CRGB(red_, green_, blue_), level);
        }
        FastLED.setBrightness(72);
    } else if (mode_ == "mood_focus") {
        fadeToBlackBy(g_leds, NUM_LEDS, 110);
        g_leds[chaseIndex_] = CRGB(red_, green_, blue_);
        g_leds[(chaseIndex_ + 1) % NUM_LEDS] = CRGB(red_ / 2, green_ / 2, blue_ / 2);
        g_leds[(chaseIndex_ + NUM_LEDS - 1) % NUM_LEDS] = CRGB(red_ / 2, green_ / 2, blue_ / 2);
        chaseIndex_ = (chaseIndex_ + 1) % NUM_LEDS;
        FastLED.setBrightness(88);
    } else if (mode_ == "mood_tired") {
        const uint8_t breath = beatsin8(4, 20, 80);
        fill_solid(g_leds, NUM_LEDS, blend(CRGB::Black, CRGB(red_, green_, blue_), breath));
        FastLED.setBrightness(52);
    } else if (mode_ == "mood_sad") {
        fadeToBlackBy(g_leds, NUM_LEDS, 80);
        g_leds[chaseIndex_] = CRGB(red_ / 4, green_ / 4, blue_);
        chaseIndex_ = (chaseIndex_ + 1) % NUM_LEDS;
        FastLED.setBrightness(60);
    } else if (mode_ == "voice_happy") {
        const uint8_t pulse = beatsin8(12, 80, 180);
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            g_leds[i] = CHSV(hueOffset_ + i * 18, 200, pulse);
        }
        FastLED.setBrightness(110);
    } else if (mode_ == "voice_calm") {
        const uint8_t breath = beatsin8(7, 40, 120);
        fill_solid(g_leds, NUM_LEDS, blend(CRGB(0, 6, 18), CRGB(red_, green_, blue_), breath));
        FastLED.setBrightness(88);
    } else if (mode_ == "voice_focus") {
        fadeToBlackBy(g_leds, NUM_LEDS, 88);
        g_leds[chaseIndex_] = CRGB(red_, green_, blue_);
        g_leds[(chaseIndex_ + NUM_LEDS / 2) % NUM_LEDS] = CRGB(red_, green_, blue_);
        chaseIndex_ = (chaseIndex_ + 1) % NUM_LEDS;
        FastLED.setBrightness(100);
    } else if (mode_ == "voice_tired") {
        const uint8_t breath = beatsin8(5, 25, 90);
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            const uint8_t level = qsub8(breath, i * 6);
            g_leds[i] = blend(CRGB::Black, CRGB(red_, green_, blue_), level);
        }
        FastLED.setBrightness(70);
    } else if (mode_ == "voice_sad") {
        fadeToBlackBy(g_leds, NUM_LEDS, 100);
        g_leds[chaseIndex_] = CRGB(red_ / 3, green_ / 3, blue_);
        g_leds[(chaseIndex_ + NUM_LEDS - 2) % NUM_LEDS] = CRGB(red_ / 6, green_ / 6, blue_ / 2);
        chaseIndex_ = (chaseIndex_ + 1) % NUM_LEDS;
        FastLED.setBrightness(72);
    } else if (mode_ == "offline") {
        fadeToBlackBy(g_leds, NUM_LEDS, 80);
        g_leds[chaseIndex_] = CRGB(red_, green_, blue_);
        const uint8_t mirrorIndex = (NUM_LEDS - 1 - chaseIndex_);
        g_leds[mirrorIndex] = CRGB(red_, green_, blue_);
        chaseIndex_ = (chaseIndex_ + 1) % NUM_LEDS;
        FastLED.setBrightness(78);
    } else if (mode_ == "error") {
        const bool on = (chaseIndex_++ % 2) == 0;
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            g_leds[i] = ((i % 2) == (on ? 0 : 1)) ? CRGB(red_, green_, blue_) : CRGB::Black;
        }
        FastLED.setBrightness(110);
    } else {
        for (uint8_t i = 0; i < NUM_LEDS; ++i) {
            const uint8_t level = sin8(hueOffset_ + i * 20);
            g_leds[i] = blend(CRGB::Black, CRGB(red_, green_, blue_), level);
        }
        FastLED.setBrightness(80);
    }

    FastLED.show();
}
// 关闭 LED 并恢复默认亮度。
void LED::turnOff() {
    FastLED.setBrightness(kBrightness);
    fill_solid(g_leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
}
