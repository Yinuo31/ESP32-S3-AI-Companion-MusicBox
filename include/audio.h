#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

class AudioHandler {
public:
    void begin();
    void testMic();
    bool recordAndSend();
    void initSpeaker();
    void onTtsBegin(size_t totalBytes, size_t chunkBytes);
    bool enqueueTtsAudio(const uint8_t* data, size_t length);
    void onTtsEnd();

private:
    void initMicrophone();
    void flushTtsRingBuffer();
    void speakerTaskLoop();
    static void speakerTaskEntry(void* parameter);

    RingbufHandle_t ttsRingBuffer_ = nullptr;
    TaskHandle_t speakerTaskHandle_ = nullptr;
    volatile bool ttsStreamActive_ = false;
    volatile bool ttsStreamFinished_ = false;
    volatile size_t expectedTtsBytes_ = 0;
    volatile size_t receivedTtsBytes_ = 0;
    volatile size_t playedTtsBytes_ = 0;
    volatile size_t expectedChunkBytes_ = 0;
};

extern AudioHandler myaudio;

#endif
