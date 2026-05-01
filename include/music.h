#ifndef MUSIC_H
#define MUSIC_H

#include <Arduino.h>

class MusicPlayer {
public:
    void begin();
    void playSong(int songNumber);
    void pause();
    void resume();
    void stop();
    void setVolume(uint8_t vol);
    int currentTrack() const { return currentTrack_; }
    bool isPlaying() const { return playing_; }
    bool isPaused() const { return paused_; }

private:
    int currentTrack_ = 0;
    uint8_t volume_ = 10;
    bool playing_ = false;
    bool paused_ = false;
    unsigned long lastPlayCommandMs_ = 0;
};

#endif
