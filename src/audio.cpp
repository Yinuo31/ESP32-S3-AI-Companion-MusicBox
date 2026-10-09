#include "audio.h"

#include "config.h"
#include "network.h"

#include <driver/i2s.h>

namespace {
constexpr i2s_port_t MIC_I2S_PORT = I2S_NUM_0;
constexpr i2s_port_t SPK_I2S_PORT = I2S_NUM_1;
constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;// 音频采样率：16 kHz，适合语音处理和 TTS 播放。
constexpr size_t MIC_DMA_BUFFER_LENGTH = 256;// I2S DMA 缓冲配置，麦克风输入每次读取 256 个样本（约 32ms 音频），扬声器输出每次写入 512 字节（约 16ms 音频）。
constexpr size_t SPEAKER_DMA_BUFFER_LENGTH = 512;// TTS 音频流缓冲配置。
constexpr size_t TTS_RING_BUFFER_BYTES = 32 * 1024;// TTS 音频环形缓冲大小：32 KB，约 1 秒的 16-bit 单声道 PCM 音频数据。
constexpr uint32_t SPEAKER_TASK_STACK_SIZE = 4096;
constexpr UBaseType_t SPEAKER_TASK_PRIORITY = 2;
}

AudioHandler myaudio;

// 初始化音频链路：麦克风、扬声器、TTS 缓冲与播放任务。
void AudioHandler::begin() {
    Serial.println("\n[Audio] Init microphone and speaker...");

    initMicrophone();
    initSpeaker();

    if (ttsRingBuffer_ == nullptr) {
        ttsRingBuffer_ = xRingbufferCreate(TTS_RING_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
        if (ttsRingBuffer_ == nullptr) {
            Serial.println("[Audio] Failed to create TTS ring buffer.");
            return;
        }
    }

    if (speakerTaskHandle_ == nullptr) {
        xTaskCreatePinnedToCore(
            AudioHandler::speakerTaskEntry,
            "tts_speaker",
            SPEAKER_TASK_STACK_SIZE,
            this,
            SPEAKER_TASK_PRIORITY,
            &speakerTaskHandle_,
            tskNO_AFFINITY
        );
    }
}

// 初始化 I2S 麦克风输入。
void AudioHandler::initMicrophone() {
    Serial.println("[Audio] Init INMP441 microphone...");

    const i2s_config_t i2sConfig = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = MIC_DMA_BUFFER_LENGTH,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };

    const i2s_pin_config_t pinConfig = {
        .bck_io_num = I2S_MIC_SCK,
        .ws_io_num = I2S_MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_MIC_SD,
    };

    if (i2s_driver_install(MIC_I2S_PORT, &i2sConfig, 0, nullptr) != ESP_OK) {
        Serial.println("[Audio] Microphone i2s_driver_install failed.");
        return;
    }

    if (i2s_set_pin(MIC_I2S_PORT, &pinConfig) != ESP_OK) {
        Serial.println("[Audio] Microphone i2s_set_pin failed.");
        return;
    }

    i2s_zero_dma_buffer(MIC_I2S_PORT);
    Serial.println("[Audio] Microphone ready.");
}

// 初始化 I2S 扬声器输出。
void AudioHandler::initSpeaker() {
    Serial.println("[Audio] Init MAX98357A speaker...");

    const i2s_config_t i2sConfig = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = SPEAKER_DMA_BUFFER_LENGTH,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0,
    };

    const i2s_pin_config_t pinConfig = {
        .bck_io_num = I2S_SPK_BCLK,
        .ws_io_num = I2S_SPK_LRC,
        .data_out_num = I2S_SPK_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    if (i2s_driver_install(SPK_I2S_PORT, &i2sConfig, 0, nullptr) != ESP_OK) {
        Serial.println("[Audio] Speaker i2s_driver_install failed.");
        return;
    }

    if (i2s_set_pin(SPK_I2S_PORT, &pinConfig) != ESP_OK) {
        Serial.println("[Audio] Speaker i2s_set_pin failed.");
        return;
    }

    i2s_zero_dma_buffer(SPK_I2S_PORT);
    Serial.println("[Audio] Speaker ready.");
}

// 读取一个麦克风样本用于调试观察。
void AudioHandler::testMic() {
    int32_t sample = 0;
    size_t bytesRead = 0;
    i2s_read(MIC_I2S_PORT, &sample, sizeof(sample), &bytesRead, portMAX_DELAY);
    if (bytesRead > 0) {
        sample >>=  14;
        Serial.println(sample);
    }
}

// 录制约 3 秒语音并以二进制分片上传到云端。
bool AudioHandler::recordAndSend() {
    if (!network.isConnected()) {
        Serial.println("[Audio] Skip recording upload because websocket is offline.");
        return false;
    }

    Serial.println("\n[Audio] Start recording 3 seconds...");
    if (!network.sendText("CMD:START_RECORD")) {
        return false;
    }

    const int samplesToRead = AUDIO_SAMPLE_RATE * 3;
    int samplesReadTotal = 0;
    int32_t i2sReadBuffer[MIC_DMA_BUFFER_LENGTH];
    int16_t pcmSendBuffer[MIC_DMA_BUFFER_LENGTH];
    static float dcOffset = 0.0f;

    i2s_zero_dma_buffer(MIC_I2S_PORT);

    while (samplesReadTotal < samplesToRead) {
        size_t bytesRead = 0;
        i2s_read(MIC_I2S_PORT, &i2sReadBuffer, sizeof(i2sReadBuffer), &bytesRead, portMAX_DELAY);

        const int samplesRead = bytesRead / sizeof(int32_t);
        for (int index = 0; index < samplesRead; ++index) {
            const int32_t rawSample = i2sReadBuffer[index] >> 12;
            dcOffset = 0.995f * dcOffset + 0.005f * rawSample;

            const int32_t cleanSample = rawSample - static_cast<int32_t>(dcOffset);
            int32_t amplified = cleanSample * 3;
            if (amplified > 32767) amplified = 32767;
            if (amplified < -32768) amplified = -32768;
            pcmSendBuffer[index] = static_cast<int16_t>(amplified);
        }

        if (!network.sendBinary(reinterpret_cast<const char*>(pcmSendBuffer), samplesRead * sizeof(int16_t))) {
            Serial.println("[Audio] Binary upload failed during recording.");
            network.sendText("CMD:STOP_RECORD");
            return false;
        }

        samplesReadTotal += samplesRead;
        network.loop();
    }

    const bool stopSent = network.sendText("CMD:STOP_RECORD");
    Serial.println("[Audio] Recording upload finished.");
    return stopSent;
}

// TTS 流开始：重置播放统计与缓冲状态。
void AudioHandler::onTtsBegin(size_t totalBytes, size_t chunkBytes) {
    flushTtsRingBuffer();
    i2s_zero_dma_buffer(SPK_I2S_PORT);

    expectedTtsBytes_ = totalBytes;
    expectedChunkBytes_ = chunkBytes;
    receivedTtsBytes_ = 0;
    playedTtsBytes_ = 0;
    ttsStreamFinished_ = false;
    ttsStreamActive_ = true;

    Serial.printf("[Audio] TTS begin, total=%u bytes, chunk=%u bytes\n",
        static_cast<unsigned>(totalBytes),
        static_cast<unsigned>(chunkBytes));
}

// 将收到的 TTS PCM 数据写入环形缓冲，供扬声器任务消费。
bool AudioHandler::enqueueTtsAudio(const uint8_t* data, size_t length) {
    if (ttsRingBuffer_ == nullptr || data == nullptr || length == 0) {
        return false;
    }

    const size_t alignedLength = length & ~static_cast<size_t>(0x01);
    if (alignedLength == 0) {
        return true;
    }

    if (!ttsStreamActive_) {
        onTtsBegin(0, alignedLength);
    }

    const BaseType_t queued = xRingbufferSend(ttsRingBuffer_, data, alignedLength, pdMS_TO_TICKS(150));
    if (queued != pdTRUE) {
        Serial.printf("[Audio] Ring buffer full, failed to queue %u bytes.\n",
            static_cast<unsigned>(alignedLength));
        return false;
    }

    receivedTtsBytes_ += alignedLength;
    return true;
}

// 标记 TTS 流结束，等待播放任务清空缓冲。
void AudioHandler::onTtsEnd() {
    ttsStreamFinished_ = true;
    Serial.printf("[Audio] TTS stream finished, queued=%u/%u bytes.\n",
        static_cast<unsigned>(receivedTtsBytes_),
        static_cast<unsigned>(expectedTtsBytes_));
}

// 清空未播放的 TTS 缓冲数据。
void AudioHandler::flushTtsRingBuffer() {
    if (ttsRingBuffer_ == nullptr) {
        return;
    }

    size_t itemSize = 0;
    void* item = xRingbufferReceive(ttsRingBuffer_, &itemSize, 0);
    while (item != nullptr) {
        vRingbufferReturnItem(ttsRingBuffer_, item);
        item = xRingbufferReceive(ttsRingBuffer_, &itemSize, 0);
    }
}

// 扬声器任务静态入口：转发到成员循环。
void AudioHandler::speakerTaskEntry(void* parameter) {
    static_cast<AudioHandler*>(parameter)->speakerTaskLoop();
}

// 扬声器任务循环：持续从缓冲取数据并写入 I2S。
void AudioHandler::speakerTaskLoop() {
    for (;;) {
        if (ttsRingBuffer_ == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t itemSize = 0;
        uint8_t* item = static_cast<uint8_t*>(xRingbufferReceive(ttsRingBuffer_, &itemSize, pdMS_TO_TICKS(100)));
        if (item != nullptr) {
            size_t offset = 0;
            while (offset < itemSize) {
                size_t bytesWritten = 0;
                const esp_err_t result = i2s_write(
                    SPK_I2S_PORT,
                    item + offset,
                    itemSize - offset,
                    &bytesWritten,
                    portMAX_DELAY);

                if (result != ESP_OK || bytesWritten == 0) {
                    Serial.printf("[Audio] i2s_write failed: %d\n", static_cast<int>(result));
                    break;
                }

                offset += bytesWritten;
                playedTtsBytes_ += bytesWritten;
            }

            vRingbufferReturnItem(ttsRingBuffer_, item);
            continue;
        }

        if (ttsStreamActive_ && ttsStreamFinished_ && playedTtsBytes_ >= receivedTtsBytes_) {
            i2s_zero_dma_buffer(SPK_I2S_PORT);
            ttsStreamActive_ = false;
            ttsStreamFinished_ = false;
            expectedTtsBytes_ = 0;
            expectedChunkBytes_ = 0;
            receivedTtsBytes_ = 0;
            playedTtsBytes_ = 0;
            Serial.println("[Audio] TTS playback complete.");
        }
    }
}
