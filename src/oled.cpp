#include "oled.h"

#include "config.h"

#include <U8g2lib.h>
#include <Wire.h>

namespace {
U8G2_SSD1306_128X64_NONAME_F_HW_I2C g_oled(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
constexpr int16_t kTextAreaWidth = 92;
constexpr int16_t kIconX = 98;
constexpr int16_t kIconY = 14;
constexpr unsigned long kScrollStepMs = 90;
constexpr unsigned long kPauseMs = 600;

// 根据情绪标签绘制右侧动态表情图标。
void drawEmotionIcon(const String& moodIcon, bool altFrame) {
    const int x = kIconX;
    const int y = kIconY;
    g_oled.drawCircle(x + 12, y + 12, 11);

    if (moodIcon == "boot") {
        g_oled.drawCircle(x + 12, y + 12, 7);
        g_oled.drawDisc(x + 8, y + 10, 1);
        g_oled.drawDisc(x + 16, y + 10, 1);
        g_oled.drawCircle(x + 12, y + 15, 4, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        g_oled.drawLine(x + 12, y + (altFrame ? 0 : 1), x + 12, y - 4);
        g_oled.drawLine(x + 7 + (altFrame ? -1 : 0), y + 4, x + 4, y + 1);
        g_oled.drawLine(x + 17 + (altFrame ? 1 : 0), y + 4, x + 20, y + 1);
        g_oled.drawPixel(x + 12, y - 6);
        g_oled.drawPixel(x + 4 + (altFrame ? -1 : 0), y);
        g_oled.drawPixel(x + 20 + (altFrame ? 1 : 0), y);
    } else if (moodIcon == "online") {
        g_oled.drawDisc(x + 8, y + 9, 1);
        g_oled.drawDisc(x + 16, y + 9, 1);
        g_oled.drawCircle(x + 12, y + 16, 4, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        g_oled.drawCircle(x + 12, y + 12, 15, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        if (altFrame) {
            g_oled.drawCircle(x + 12, y + 12, 18, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        } else {
            g_oled.drawPixel(x + 3, y + 2);
            g_oled.drawPixel(x + 21, y + 2);
        }
    } else if (moodIcon == "happy") {
        if (altFrame) {
            g_oled.drawLine(x + 6, y + 8, x + 10, y + 10);
            g_oled.drawLine(x + 14, y + 10, x + 18, y + 8);
        } else {
            g_oled.drawDisc(x + 8, y + 9, 1);
            g_oled.drawDisc(x + 16, y + 9, 1);
        }
        g_oled.drawCircle(x + 12, y + 14 + (altFrame ? 1 : 0), 6, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
        g_oled.drawPixel(x + 4 + (altFrame ? 0 : 1), y + 5);
        g_oled.drawPixel(x + 20 + (altFrame ? 0 : -1), y + 5);
        g_oled.drawPixel(x + 3, y + 7);
        g_oled.drawPixel(x + 21, y + 7);
    } else if (moodIcon == "calm") {
        g_oled.drawLine(x + 6, y + 9, x + 10, y + 9);
        g_oled.drawLine(x + 14, y + 9, x + 18, y + 9);
        g_oled.drawArc(x + 12, y + 17, 4, 0, 180);
        g_oled.drawCircle(x + 3, y + 7 + (altFrame ? 1 : 0), 2, U8G2_DRAW_ALL);
        g_oled.drawCircle(x + 22, y + 17 - (altFrame ? 1 : 0), 1, U8G2_DRAW_ALL);
    } else if (moodIcon == "focus") {
        g_oled.drawLine(x + 5, y + 6 + (altFrame ? 1 : 0), x + 10, y + 7);
        g_oled.drawLine(x + 14, y + 7, x + 19, y + 6 + (altFrame ? 1 : 0));
        g_oled.drawDisc(x + 8, y + 10, 1);
        g_oled.drawDisc(x + 16, y + 10, 1);
        g_oled.drawLine(x + 7, y + 17, x + 17, y + 17);
        g_oled.drawLine(x + 11 + (altFrame ? 1 : -1), y + 4, x + 13 + (altFrame ? 1 : -1), y + 2);
        g_oled.drawLine(x + 12, y + 1, x + 12, y - 3);
        g_oled.drawPixel(x + 12, y - 5);
    } else if (moodIcon == "tired") {
        g_oled.drawLine(x + 6, y + 9, x + 10, y + 10 + (altFrame ? 1 : 0));
        g_oled.drawLine(x + 14, y + 10 + (altFrame ? 1 : 0), x + 18, y + 9);
        g_oled.drawCircle(x + 12, y + 18, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        g_oled.drawPixel(x + 17, y + 15 + (altFrame ? 1 : 0));
        g_oled.drawPixel(x + 18, y + 17 + (altFrame ? 1 : 0));
        g_oled.drawLine(x + 3, y + 4, x + 5, y + 2);
    } else if (moodIcon == "sad") {
        if (altFrame) {
            g_oled.drawLine(x + 6, y + 9, x + 10, y + 10);
            g_oled.drawLine(x + 14, y + 10, x + 18, y + 9);
        } else {
            g_oled.drawDisc(x + 8, y + 9, 1);
            g_oled.drawDisc(x + 16, y + 9, 1);
        }
        g_oled.drawCircle(x + 12, y + 20, 5, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        g_oled.drawPixel(x + 18, y + 14 + (altFrame ? 1 : 0));
        g_oled.drawPixel(x + 19, y + 15 + (altFrame ? 1 : 0));
    } else {
        if (altFrame) {
            g_oled.drawLine(x + 6, y + 9, x + 10, y + 9);
            g_oled.drawLine(x + 14, y + 9, x + 18, y + 9);
        } else {
            g_oled.drawDisc(x + 8, y + 9, 1);
            g_oled.drawDisc(x + 16, y + 9, 1);
        }
        g_oled.drawArc(x + 12, y + 18, 4, 0, 180);
    }
}
}

// 初始化 OLED 并清屏。
void OLED::begin() {
    Serial.println("[OLED] Init SSD1306...");
    g_oled.begin();
    g_oled.enableUTF8Print();
    clear();
    Serial.println("[OLED] Ready.");
}

// 更新显示状态文本与情绪图标，并在内容变化时重置滚动偏移。
void OLED::showStatus(const String& title, const String& subtitle, const String& footer, const String& moodIcon) {
    const bool changed =
        title != title_ || subtitle != subtitle_ || footer != footer || moodIcon != moodIcon_;

    title_ = title;
    subtitle_ = subtitle;
    footer_ = footer;
    moodIcon_ = moodIcon;
    if (changed) {
        subtitleOffset_ = 0;
        footerOffset_ = 0;
        lastSubtitleStepMs_ = 0;
        lastFooterStepMs_ = 0;
    }

    drawStatusFrame();
}

// 绘制整帧 UI：标题、两行文本区域、分隔线与图标。
void OLED::drawStatusFrame() {
    const unsigned long now = millis();
    const bool altFrame = ((now / 320) % 2) == 1;
    g_oled.clearBuffer();
    g_oled.setFont(u8g2_font_wqy12_t_gb2312);
    g_oled.setCursor(0, 13);
    g_oled.print(title_);

    drawScrollingLine(subtitle_, 33, subtitleOffset_, now);
    drawScrollingLine(footer_, 53, footerOffset_, now);

    g_oled.drawVLine(95, 4, 56);
    if (!moodIcon_.isEmpty()) {
        drawEmotionIcon(moodIcon_, altFrame);
    }
    g_oled.sendBuffer();
}

// 在固定区域绘制文本，超宽时启用循环滚动。
void OLED::drawScrollingLine(const String& text, int16_t baselineY, int16_t& offset, unsigned long now) {
    if (text.isEmpty()) {
        return;
    }

    const int16_t textWidth = g_oled.getUTF8Width(text.c_str());
    if (textWidth <= kTextAreaWidth) {
        g_oled.setCursor(0, baselineY);
        g_oled.print(text);
        return;
    }

    if (offset == 0 && now - lastSubtitleStepMs_ < kPauseMs && baselineY == 33) {
        // hold at the beginning
    } else if (offset == 0 && now - lastFooterStepMs_ < kPauseMs && baselineY == 53) {
        // hold at the beginning
    } else {
        unsigned long& lastStepMs = (baselineY == 33) ? lastSubtitleStepMs_ : lastFooterStepMs_;
        if (lastStepMs == 0 || now - lastStepMs >= kScrollStepMs) {
            lastStepMs = now;
            offset--;
            if (offset < -(textWidth + 12)) {
                offset = 0;
            }
        }
    }

    g_oled.setClipWindow(0, baselineY - 13, kTextAreaWidth, baselineY + 2);
    g_oled.setCursor(offset, baselineY);
    g_oled.print(text);
    g_oled.setCursor(offset + textWidth + 16, baselineY);
    g_oled.print(text);
    g_oled.setMaxClipWindow();
}

// 清空 OLED 缓冲并提交到屏幕。
void OLED::clear() {
    g_oled.clearBuffer();
    g_oled.sendBuffer();
}
