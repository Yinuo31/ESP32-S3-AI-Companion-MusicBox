#ifndef OLED_H
#define OLED_H

#include <Arduino.h>

class OLED {
public:
    void begin();
    void showStatus(
        const String& title,
        const String& subtitle = String(),
        const String& footer = String(),
        const String& moodIcon = String());
    void clear();

private:
    void drawStatusFrame();
    void drawScrollingLine(const String& text, int16_t baselineY, int16_t& offset, unsigned long now);

    String title_;
    String subtitle_;
    String footer_;
    String moodIcon_;
    int16_t subtitleOffset_ = 0;
    int16_t footerOffset_ = 0;
    unsigned long lastSubtitleStepMs_ = 0;
    unsigned long lastFooterStepMs_ = 0;
};

#endif
