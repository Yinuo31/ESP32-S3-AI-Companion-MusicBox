#include "music.h"

#include "config.h"

#include <DFRobotDFPlayerMini.h>

namespace {
HardwareSerial g_dfSerial(1);
DFRobotDFPlayerMini g_dfPlayer;
constexpr uint8_t kBootVolume = 3;
constexpr uint8_t kPlayTargetVolume = 10;
constexpr uint16_t kPlayKickDelayMs = 30;
constexpr uint16_t kDuplicatePlayGuardMs = 1200;
}

// 初始化 DFPlayer 串口与默认音量。
void MusicPlayer::begin() {
    Serial.println("[Music] Init DFPlayer Mini...");
    g_dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);

    if (!g_dfPlayer.begin(g_dfSerial, true, false)) {
        Serial.println("[Music] DFPlayer not responding.");
        return;
    }

    volume_ = kPlayTargetVolume;
    g_dfPlayer.volume(kBootVolume);
    Serial.println("[Music] Ready.");
}

// 播放指定曲目，并在重复触发时做短时间去重保护。
void MusicPlayer::playSong(int songNumber) {
    if (songNumber <= 0) {
        return;
    }

    const unsigned long now = millis();
    if (playing_ && !paused_ && currentTrack_ == songNumber && (now - lastPlayCommandMs_) < kDuplicatePlayGuardMs) {
        Serial.printf("[Music] Ignore duplicate play for /MP3/%04d.mp3\n", songNumber);
        return;
    }

    lastPlayCommandMs_ = now;
    currentTrack_ = songNumber;
    playing_ = true;
    paused_ = false;

    // Start from a low volume to reduce the initial burst, then quickly return to target volume.
    g_dfPlayer.volume(kBootVolume);
    Serial.printf("[Music] Play /MP3/%04d.mp3\n", songNumber);
    g_dfPlayer.playMp3Folder(songNumber);
    delay(kPlayKickDelayMs);
    g_dfPlayer.volume(volume_);
}

// 暂停当前播放。
void MusicPlayer::pause() {
    g_dfPlayer.pause();
    paused_ = true;
    Serial.println("[Music] Paused.");
}

// 恢复已暂停播放。
void MusicPlayer::resume() {
    g_dfPlayer.start();
    if (currentTrack_ > 0) {
        playing_ = true;
    }
    paused_ = false;
    Serial.println("[Music] Resumed.");
}

// 停止播放并清除播放状态。
void MusicPlayer::stop() {
    g_dfPlayer.stop();
    playing_ = false;
    paused_ = false;
    Serial.println("[Music] Stopped.");
}

// 设置 DFPlayer 输出音量（范围 0-30）。
void MusicPlayer::setVolume(uint8_t vol) {
    if (vol > 30) {
        vol = 30;
    }
    volume_ = vol;
    g_dfPlayer.volume(vol);
    Serial.printf("[Music] Volume = %u\n", static_cast<unsigned>(vol));
}
