#include "app_controller.h"

#include "audio.h"
#include "config.h"
#include "led.h"
#include "music.h"
#include "network.h"
#include "nfc.h"
#include "oled.h"

#include <ArduinoJson.h>
// 事件队列长度、任务栈大小和优先级等系统参数配置。
namespace {
constexpr size_t EVENT_QUEUE_LENGTH = 12;
constexpr size_t RECORD_QUEUE_LENGTH = 4;
constexpr uint32_t NETWORK_TASK_STACK = 4096;
constexpr uint32_t UI_TASK_STACK = 8192;
constexpr uint32_t AUDIO_TASK_STACK = 4096;
constexpr UBaseType_t NETWORK_TASK_PRIORITY = 2;
constexpr UBaseType_t UI_TASK_PRIORITY = 2;
constexpr UBaseType_t AUDIO_TASK_PRIORITY = 2;
constexpr TickType_t NETWORK_LOOP_DELAY = pdMS_TO_TICKS(20);
constexpr TickType_t UI_LOOP_DELAY = pdMS_TO_TICKS(20);

NFC g_nfc;// 全局单例对象：NFC、OLED、LED、音乐播放器等。
OLED g_oled;
LED g_led;
MusicPlayer g_music;

struct MoodTrackProfile {
    const char* label;
    uint8_t startTrack;
    uint8_t endTrack;
};
// 场景配置表：UID 与场景的映射，以及离线模式下的默认音乐。
const SceneProfile kScenes[] = {
    {"12F84906", "focus", "专注卡", "请为用户营造专注学习氛围，给一句简短陪伴，并选择适合专注的灯光。", 1},
    {"E7550207", "sleep", "助眠卡", "请为用户营造助眠氛围，给一句轻柔安抚，并选择适合助眠的灯光。", 2},
    {"75486C06", "healing", "治愈卡", "请给用户一段温柔治愈的短回复，并选择温暖柔和的灯光。", 3},
};
// 情绪标签与本地曲目区间的映射配置。
const MoodTrackProfile kMoodProfiles[] = {
    {"happy", 1, 5},
    {"calm", 6, 10},
    {"focus", 11, 15},
    {"tired", 16, 20},
    {"sad", 21, 25},
};

// 根据情绪标签查找对应的本地曲目区间配置。
const MoodTrackProfile* findMoodProfile(const String& label) {
    for (const auto& mood : kMoodProfiles) {
        if (label == mood.label) {
            return &mood;
        }
    }
    return nullptr;
}
// 根据场景 UID 查找对应的场景配置。
String normalizeLocalCommandText(String text) {
    text.trim();
    text.toLowerCase();
    const char* punctuations[] = {" ", "，", "。", "！", "？", "：", ",", ".", "!", "?", ":", ";", "；", "\r", "\n", "\t"};
    for (const char* token : punctuations) {
        text.replace(token, "");
    }

    text.replace("音樂", "音乐");
    text.replace("音月", "音乐");
    text.replace("音悦", "音乐");
    text.replace("歌曲", "音乐");
    text.replace("來", "来");
    text.replace("切换到", "切到");
    text.replace("接着播放", "继续播放");
    text.replace("恢复播放", "继续播放");
    text.replace("暂停播放", "暂停音乐");
    text.replace("停止播放", "停止音乐");
    text.replace("关掉音乐", "停止音乐");
    text.replace("第一首", "1");
    text.replace("第二首", "2");
    text.replace("第三首", "3");
    text.replace("一首", "1");
    text.replace("二首", "2");
    text.replace("三首", "3");
    return text;
}
// 根据 UID 查找对应的场景配置。
int extractLocalTrackId(const String& normalized) {
    struct ChineseTrackMap {
        const char* text;
        int value;
    };

    static const ChineseTrackMap kChineseTracks[] = {
        {"二十五", 25}, {"二十四", 24}, {"二十三", 23}, {"二十二", 22}, {"二十一", 21},
        {"二十", 20}, {"十九", 19}, {"十八", 18}, {"十七", 17}, {"十六", 16},
        {"十五", 15}, {"十四", 14}, {"十三", 13}, {"十二", 12}, {"十一", 11},
        {"十", 10}, {"九", 9}, {"八", 8}, {"七", 7}, {"六", 6},
        {"五", 5}, {"四", 4}, {"三", 3}, {"二", 2}, {"一", 1},
    };

    for (int track = 25; track >= 1; --track) {
        if (
            normalized.indexOf("音乐" + String(track)) >= 0 ||
            normalized.indexOf("第" + String(track) + "首") >= 0 ||
            normalized.indexOf("播放" + String(track)) >= 0 ||
            normalized.indexOf("来" + String(track)) >= 0 ||
            normalized.indexOf("放" + String(track)) >= 0 ||
            normalized.indexOf(String(track) + "号") >= 0
        ) {
            return track;
        }
    }

    for (const auto& item : kChineseTracks) {
        const String chinese(item.text);
        if (
            normalized.indexOf("音乐" + chinese) >= 0 ||
            normalized.indexOf("第" + chinese + "首") >= 0 ||
            normalized.indexOf("播放" + chinese) >= 0 ||
            normalized.indexOf("来" + chinese) >= 0 ||
            normalized.indexOf("放" + chinese) >= 0 ||
            normalized.indexOf(chinese + "号") >= 0
        ) {
            return item.value;
        }
    }

    return 0;
}
}

AppController app;

// 系统启动入口：初始化各模块、创建任务并进入空闲态。
void AppController::begin() {
    Serial.println("[App] Booting controller...");
    randomSeed(micros());
    eventQueue_ = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(AppEvent));
    recordQueue_ = xQueueCreate(RECORD_QUEUE_LENGTH, sizeof(uint8_t));

    g_led.begin();
    g_oled.begin();
    g_nfc.begin();
    g_music.begin();
    myaudio.begin();
    network.begin();

    setUiState(UiState::Booting, "系统启动", "正在初始化", "Arduino + FreeRTOS");
    createTasks();
    setIdleState();
    Serial.println("[App] Controller ready.");
}

// 创建网络/UI/录音采集三个 FreeRTOS 任务。
void AppController::createTasks() {
    if (networkTaskHandle_ == nullptr) {
        xTaskCreatePinnedToCore(AppController::networkTaskEntry, "network_task", NETWORK_TASK_STACK, this, NETWORK_TASK_PRIORITY, &networkTaskHandle_, tskNO_AFFINITY);
    }
    if (uiTaskHandle_ == nullptr) {
        xTaskCreatePinnedToCore(AppController::uiTaskEntry, "ui_state", UI_TASK_STACK, this, UI_TASK_PRIORITY, &uiTaskHandle_, tskNO_AFFINITY);
    }
    if (audioTaskHandle_ == nullptr) {
        xTaskCreatePinnedToCore(AppController::audioCaptureTaskEntry, "audio_capture", AUDIO_TASK_STACK, this, AUDIO_TASK_PRIORITY, &audioTaskHandle_, tskNO_AFFINITY);
    }
}

// 向事件队列投递事件，供 UI 任务统一处理。
void AppController::postEvent(EventType type, const String& text) {
    if (eventQueue_ == nullptr) {
        return;
    }
    AppEvent event{};
    event.type = type;
    text.substring(0, sizeof(event.text) - 1).toCharArray(event.text, sizeof(event.text));
    xQueueSend(eventQueue_, &event, 0);
}

// 网络连通回调：上报在线事件。
void AppController::onNetworkConnected() { postEvent(EventType::NetworkOnline); }
// 网络断开回调：上报离线事件。
void AppController::onNetworkDisconnected() { postEvent(EventType::NetworkOffline); }
// 云端文本回调：上报文本回复事件。
void AppController::onIncomingText(const String& text) { postEvent(EventType::TextReply, text); }
// 云端控制回调：上报控制 JSON 事件。
void AppController::onIncomingControl(const String& json) { postEvent(EventType::ControlJson, json); }
// TTS 开始回调：上报语音播报开始事件。
void AppController::onTtsBegin() { postEvent(EventType::TtsBegin); }
// TTS 结束回调：上报语音播报结束事件。
void AppController::onTtsEnd() { postEvent(EventType::TtsEnd); }
// 云端异常回调：上报错误事件。
void AppController::onCloudError(const String& errorText) { postEvent(EventType::Error, errorText); }

// 网络任务静态入口：转发到成员循环函数。
void AppController::networkTaskEntry(void* parameter) {
    static_cast<AppController*>(parameter)->networkTaskLoop();
}

// UI 任务静态入口：转发到成员循环函数。
void AppController::uiTaskEntry(void* parameter) {
    static_cast<AppController*>(parameter)->uiTaskLoop();
}

// 录音采集任务静态入口：转发到成员循环函数。
void AppController::audioCaptureTaskEntry(void* parameter) {
    static_cast<AppController*>(parameter)->audioCaptureTaskLoop();
}

// 网络任务主循环：持续驱动 WebSocket 状态机。
void AppController::networkTaskLoop() {
    for (;;) {
        network.loop();
        vTaskDelay(NETWORK_LOOP_DELAY);
    }
}

// UI 任务主循环：处理事件、输入与显示刷新。
void AppController::uiTaskLoop() {
    for (;;) {
        AppEvent event{};
        while (xQueueReceive(eventQueue_, &event, 0) == pdTRUE) {
            handleEvent(event);
        }

        handleSerialInput();
        handleButtonInput();
        handleNfcInput();
        refreshIndicators();
        vTaskDelay(UI_LOOP_DELAY);
    }
}

// 录音采集任务循环：等待触发并执行录音上传。
void AppController::audioCaptureTaskLoop() {
    uint8_t token = 0;
    for (;;) {
        if (xQueueReceive(recordQueue_, &token, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        postEvent(EventType::RecordStarted);
        const bool ok = myaudio.recordAndSend();
        if (!ok) {
            postEvent(EventType::Error, "录音上行失败，已切到离线模式");
            continue;
        }
        postEvent(EventType::RecordUploaded);
    }
}

// 统一事件处理：根据事件类型切换 UI、灯光和离线状态。
void AppController::handleEvent(const AppEvent& event) {
    const String payload(event.text);

    switch (event.type) {
        case EventType::NetworkOnline:
            networkOnline_ = true;
            exitOfflineMode();
            break;
        case EventType::NetworkOffline:
            networkOnline_ = false;
            clearPendingMoodMusic();
            enterOfflineMode("云端离线");
            break;
        case EventType::TextReply:
            lastReplyText_ = payload;
            if (!ttsPlaying_) {
                setUiState(UiState::Reply, lastDisplayTitle_.isEmpty() ? "AI 回复" : lastDisplayTitle_, payload, currentScene_ ? currentScene_->sceneName : "文本已收到");
            }
            Serial.print("[AI] ");
            Serial.println(payload);
            break;
        case EventType::ControlJson:
            applyControlJson(payload);
            break;
        case EventType::TtsBegin:
            ttsPlaying_ = true;
            setUiState(UiState::Speaking, lastDisplayTitle_.isEmpty() ? "语音播报" : lastDisplayTitle_, lastReplyText_, currentScene_ ? currentScene_->sceneName : "TTS PCM");
            break;
        case EventType::TtsEnd:
            ttsPlaying_ = false;
            if (offlineMode_) {
                setUiState(UiState::Offline, "Offline Mode", currentScene_ ? currentScene_->sceneName : "等待联网恢复", "DFPlayer 本地回退");
            } else if (!lastReplyText_.isEmpty()) {
                setUiState(UiState::Reply, lastDisplayTitle_.isEmpty() ? "AI 回复" : lastDisplayTitle_, lastReplyText_, currentScene_ ? currentScene_->sceneName : "交互完成");
            } else {
                setIdleState();
            }
            if (pendingMoodMusicSwitch_ && !offlineMode_) {
                playMoodTrackAfterReply();
            }
            break;
        case EventType::Error:
            enterOfflineMode(payload.isEmpty() ? "云端异常" : payload);
            break;
        case EventType::RecordStarted:
            setUiState(UiState::Recording, "语音输入", "录音采集中", "3 秒 PCM 上传");
            break;
        case EventType::RecordUploaded:
            setUiState(UiState::Processing, "云端处理中", "正在识别与生成", currentScene_ ? currentScene_->sceneName : "STT -> LLM -> TTS");
            break;
    }
}

// 串口输入处理：支持调试命令和文本提问。
void AppController::handleSerialInput() {
    if (!Serial.available()) {
        return;
    }

    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.isEmpty()) {
        return;
    }

    if (input == "r") {
        startRecording();
        return;
    }

    if (input == "m1") {
        Serial.println("[Debug] Manual play test: 0001.mp3");
        g_music.playSong(1);
        return;
    }

    if (input == "m2") {
        Serial.println("[Debug] Manual play test: 0002.mp3");
        g_music.playSong(2);
        return;
    }

    if (input == "m3") {
        Serial.println("[Debug] Manual play test: 0003.mp3");
        g_music.playSong(3);
        return;
    }

    if (tryHandleLocalCommand(input)) {
        return;
    }

    sendTextQuery(input);
}

bool AppController::tryHandleLocalCommand(const String& text) {
    String normalized = normalizeLocalCommandText(text);
    if (normalized.isEmpty()) {
        return false;
    }

    auto showLocalControl = [&](const String& subtitle) {
        lastDisplayTitle_ = "音乐控制";
        lastReplyText_ = subtitle;
        setUiState(UiState::Reply, "音乐控制", subtitle, network.isConnected() ? "本地优先执行" : "离线本地执行");
    };

    if (normalized == "暂停" || normalized.indexOf("暂停音乐") >= 0 || normalized == "先停" || normalized == "停一下" || normalized == "停一停") {
        clearPendingMoodMusic();
        g_music.pause();
        showLocalControl("已暂停播放");
        return true;
    }

    if (normalized == "继续" || normalized.indexOf("继续播放") >= 0 || normalized == "恢复" || normalized == "接着播" || normalized == "接着放") {
        clearPendingMoodMusic();
        g_music.resume();
        showLocalControl("已继续播放");
        return true;
    }

    if (normalized == "停止" || normalized.indexOf("停止音乐") >= 0 || normalized == "别播了" || normalized == "别放了" || normalized == "关掉") {
        clearPendingMoodMusic();
        g_music.stop();
        showLocalControl("已停止播放");
        return true;
    }

    if (normalized.indexOf("音量最大") >= 0 || normalized.indexOf("最大音量") >= 0 || normalized.indexOf("声音最大") >= 0 || normalized.indexOf("调到最大") >= 0) {
        localMusicVolume_ = 24;
        g_music.setVolume(localMusicVolume_);
        showLocalControl("音量已调大");
        return true;
    }

    if (normalized.indexOf("音量最小") >= 0 || normalized.indexOf("最小音量") >= 0 || normalized.indexOf("声音最小") >= 0 || normalized.indexOf("调到最小") >= 0) {
        localMusicVolume_ = 4;
        g_music.setVolume(localMusicVolume_);
        showLocalControl("音量已调小");
        return true;
    }

    if (normalized.indexOf("音量大一点") >= 0 || normalized.indexOf("声音大一点") >= 0 || normalized.indexOf("调大音量") >= 0 || normalized.indexOf("音量调大") >= 0 || normalized.indexOf("大声一点") >= 0) {
        localMusicVolume_ = min<uint8_t>(30, localMusicVolume_ + 3);
        g_music.setVolume(localMusicVolume_);
        showLocalControl("音量已调大");
        return true;
    }

    if (normalized.indexOf("音量小一点") >= 0 || normalized.indexOf("声音小一点") >= 0 || normalized.indexOf("调小音量") >= 0 || normalized.indexOf("音量调小") >= 0 || normalized.indexOf("小声一点") >= 0) {
        localMusicVolume_ = (localMusicVolume_ > 3) ? (localMusicVolume_ - 3) : 0;
        g_music.setVolume(localMusicVolume_);
        showLocalControl("音量已调小");
        return true;
    }

    auto playMoodNow = [&](const String& moodLabel, const String& subtitle) {
        clearPendingMoodMusic();
        const int track = pickMoodTrack(moodLabel);
        if (track > 0) {
            currentMoodLabel_ = moodLabel;
            g_music.playSong(track);
            showLocalControl(subtitle);
            return true;
        }
        return false;
    };

    if (
        normalized.indexOf("下一首") >= 0 ||
        normalized.indexOf("下首") >= 0 ||
        normalized.indexOf("下一首歌") >= 0 ||
        normalized.indexOf("下一曲") >= 0 ||
        normalized.indexOf("换一首") >= 0 ||
        normalized.indexOf("换首歌") >= 0 ||
        normalized.indexOf("往后一首") >= 0 ||
        normalized.indexOf("后一首") >= 0
    ) {
        clearPendingMoodMusic();
        const String activeMood = !currentMoodLabel_.isEmpty() ? currentMoodLabel_ : defaultMoodForCurrentScene();
        const int track = stepMoodTrack(activeMood, g_music.currentTrack(), 1);
        if (track > 0) {
            currentMoodLabel_ = activeMood;
            g_music.playSong(track);
            showLocalControl("已切到下一首");
            return true;
        }
    }

    if (
        normalized.indexOf("上一首") >= 0 ||
        normalized.indexOf("上首") >= 0 ||
        normalized.indexOf("上一首歌") >= 0 ||
        normalized.indexOf("上一曲") >= 0 ||
        normalized.indexOf("前一首") >= 0 ||
        normalized.indexOf("往前一首") >= 0
    ) {
        clearPendingMoodMusic();
        const String activeMood = !currentMoodLabel_.isEmpty() ? currentMoodLabel_ : defaultMoodForCurrentScene();
        const int track = stepMoodTrack(activeMood, g_music.currentTrack(), -1);
        if (track > 0) {
            currentMoodLabel_ = activeMood;
            g_music.playSong(track);
            showLocalControl("已切到上一首");
            return true;
        }
    }

    if (normalized.indexOf("随机音乐") >= 0 || normalized.indexOf("随便放") >= 0 || normalized.indexOf("随便来一首") >= 0 || normalized.indexOf("随机来一首") >= 0) {
        return playMoodNow(!currentMoodLabel_.isEmpty() ? currentMoodLabel_ : defaultMoodForCurrentScene(), "已随机播放");
    }

    if (normalized.indexOf("专注音乐") >= 0 || normalized.indexOf("切到专注音乐") >= 0 || normalized.indexOf("来点专注音乐") >= 0) {
        return playMoodNow("focus", "已切到专注音乐");
    }

    if (normalized.indexOf("助眠音乐") >= 0 || normalized.indexOf("睡觉音乐") >= 0 || normalized.indexOf("切到助眠音乐") >= 0 || normalized.indexOf("来点助眠音乐") >= 0) {
        return playMoodNow("calm", "已切到助眠音乐");
    }

    if (normalized.indexOf("治愈音乐") >= 0 || normalized.indexOf("放松音乐") >= 0 || normalized.indexOf("切到治愈音乐") >= 0 || normalized.indexOf("来点放松音乐") >= 0) {
        return playMoodNow("happy", "已切到治愈音乐");
    }

    if (normalized.indexOf("播放") >= 0 || normalized.indexOf("来") >= 0 || normalized.indexOf("放") >= 0) {
        const int trackId = extractLocalTrackId(normalized);
        if (trackId > 0) {
            clearPendingMoodMusic();
            g_music.playSong(trackId);
            showLocalControl("已播放第" + String(trackId) + "首");
            return true;
        }
    }

    return false;
}

// 按键输入处理：按下录音键后触发一次录音流程。
void AppController::handleButtonInput() {
    if (digitalRead(RECORD_BUTTON_PIN) != LOW) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastButtonTriggerMs_ < 1200) {
        return;
    }

    lastButtonTriggerMs_ = now;
    startRecording();
}

// NFC 输入处理：识别场景卡并触发在线/离线场景流程。
void AppController::handleNfcInput() {
    const String uid = g_nfc.readCard();
    if (uid.isEmpty()) {
        return;
    }

    Serial.printf("[NFC] Card detected: %s\n", uid.c_str());

    const unsigned long now = millis();
    if (uid == lastCardUid_ && now - lastCardSeenMs_ < 1500) {
        return;
    }

    lastCardUid_ = uid;
    lastCardSeenMs_ = now;

    const SceneProfile* scene = findSceneByUid(uid);
    if (scene == nullptr) {
        setUiState(UiState::Error, "未知卡片", uid, "请重新配置 UID");
        Serial.println("[NFC] Unknown UID, ignore.");
        return;
    }

    currentScene_ = scene;
    currentMoodLabel_ = defaultMoodForCurrentScene();
    clearPendingMoodMusic();
    lastDisplayTitle_ = scene->sceneName;
    lastReplyText_.clear();
    Serial.printf(
        "[NFC] Matched scene=%s, offline_music_id=%u\n",
        scene->sceneId,
        static_cast<unsigned>(scene->offlineMusicId));
    setUiState(UiState::Processing, scene->sceneName, "场景处理中", uid);

    if (network.isConnected()) {
        Serial.println("[NFC] Online mode: send scene_event to Python server.");
        sendSceneEvent(*scene, "nfc");
    } else {
        Serial.println("[NFC] Offline mode: fallback to local DFPlayer.");
        enterOfflineMode("离线刷卡，直接本地回退");
    }
}

// 进入离线模式：更新状态并按场景播放本地回退音乐。
void AppController::enterOfflineMode(const String& reason) {
    offlineMode_ = true;
    ttsPlaying_ = false;
    clearPendingMoodMusic();
    currentMoodLabel_ = "sad";
    setUiState(UiState::Offline, "Offline Mode", currentScene_ ? currentScene_->sceneName : "等待联网恢复", reason);
    Serial.printf("[Offline] Enter offline mode: %s\n", reason.c_str());

    if (currentScene_ != nullptr) {
        const String offlineMood = defaultMoodForCurrentScene();
        const int track = pickMoodTrack(offlineMood);
        if (track > 0) {
            Serial.printf(
                "[Offline] Play local fallback mood track: mood=%s track=%d\n",
                offlineMood.c_str(),
                track);
            g_music.playSong(track);
        }
    }
}

// 退出离线模式：恢复在线状态显示。
void AppController::exitOfflineMode() {
    if (!offlineMode_) {
        setIdleState();
        return;
    }

    offlineMode_ = false;
    currentMoodLabel_ = defaultMoodForCurrentScene();
    setUiState(UiState::Idle, "在线模式", currentScene_ ? currentScene_->sceneName : "等待刷卡或语音", "WebSocket 已恢复");
}

// 发起录音：在线时投递录音任务，离线则直接降级提示。
void AppController::startRecording() {
    if (!network.isConnected()) {
        enterOfflineMode("当前离线，无法上传语音");
        return;
    }

    const uint8_t token = 1;
    xQueueSend(recordQueue_, &token, 0);
}

// 发送场景事件到云端，包含场景与设备上下文信息。
void AppController::sendSceneEvent(const SceneProfile& scene, const String& source) {
    JsonDocument doc;
    doc["type"] = "scene_event";
    doc["device_id"] = DEVICE_ID;
    doc["uid"] = scene.uid;
    doc["scene_id"] = scene.sceneId;
    doc["scene_name"] = scene.sceneName;
    doc["scene_prompt"] = scene.scenePrompt;
    doc["offline_music_id"] = scene.offlineMusicId;
    doc["source"] = source;

    String payload;
    serializeJson(doc, payload);
    network.sendText(payload);
}

// 发送纯文本提问到云端，并切换到处理中状态。
void AppController::sendTextQuery(const String& text) {
    if (!network.isConnected()) {
        enterOfflineMode("当前离线，文本消息未发送");
        return;
    }

    setUiState(UiState::Processing, "文本已发送", text, currentScene_ ? currentScene_->sceneName : "等待云端回复");
    network.sendText(text);
}

// 解析并执行云端下发控制 JSON（灯光、音乐、界面等）。
void AppController::applyControlJson(const String& jsonText) {
    Serial.printf("[Control] JSON = %s\n", jsonText.c_str());
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, jsonText);
    if (error) {
        Serial.printf("[App] Failed to parse control JSON: %s\n", error.c_str());
        return;
    }

    const String type = doc["type"] | "control";
    const String sceneId = doc["scene_id"] | "";
    const String spokenText = doc["spoken_text"] | "";
    const bool ttsEnabled = doc["tts_enabled"].isNull() ? true : doc["tts_enabled"].as<bool>();
    const String lightMode = doc["light_mode"] | "processing";
    const uint16_t lightSpeed = doc["light_speed"] | 120;
    const uint8_t offlineMusicId = doc["offline_music_id"] | 0;
    const String musicAction = doc["music_action"] | "";
    const String moodLabel = doc["mood_label"] | "";
    const bool moodMusicEnabled = doc["mood_music_enabled"].isNull() ? false : doc["mood_music_enabled"].as<bool>();
    const uint8_t volumeLevel = doc["volume_level"].isNull() ? localMusicVolume_ : doc["volume_level"].as<uint8_t>();
    const String motorMode = doc["motor_mode"] | "stop";
    const String displayTitle = doc["display_title"] | "AI 场景";
    Serial.printf(
        "[Control] type=%s scene_id=%s offline_music_id=%u music_action=%s volume=%u mood=%s mood_music=%s tts_enabled=%s\n",
        type.c_str(),
        sceneId.c_str(),
        static_cast<unsigned>(offlineMusicId),
        musicAction.c_str(),
        static_cast<unsigned>(volumeLevel),
        moodLabel.c_str(),
        moodMusicEnabled ? "true" : "false",
        ttsEnabled ? "true" : "false");

    pendingLightMode_ = lightMode;
    pendingLightSpeed_ = lightSpeed;
    if (doc["light_color"].is<JsonArray>()) {
        JsonArray color = doc["light_color"].as<JsonArray>();
        pendingLightR_ = color[0] | pendingLightR_;
        pendingLightG_ = color[1] | pendingLightG_;
        pendingLightB_ = color[2] | pendingLightB_;
    }
    g_led.setMode(pendingLightMode_, pendingLightR_, pendingLightG_, pendingLightB_, pendingLightSpeed_);

    if (!sceneId.isEmpty()) {
        for (const auto& scene : kScenes) {
            if (sceneId == scene.sceneId) {
                currentScene_ = &scene;
                break;
            }
        }
    }

    if (!moodLabel.isEmpty()) {
        currentMoodLabel_ = moodLabel;
    } else if (type == "scene_control") {
        currentMoodLabel_ = defaultMoodForCurrentScene();
    }

    const bool shouldPlayMusicControl = (type == "music_control" && musicAction == "play");
    const bool shouldPlaySceneControl = (type == "scene_control" && !moodMusicEnabled);
    const bool shouldPlayOfflineControl = (type == "offline_control");

    if (musicAction == "pause") {
        clearPendingMoodMusic();
        Serial.println("[Control] Pause local music.");
        g_music.pause();
    } else if (musicAction == "resume") {
        clearPendingMoodMusic();
        Serial.println("[Control] Resume local music.");
        g_music.resume();
    } else if (musicAction == "stop") {
        clearPendingMoodMusic();
        Serial.println("[Control] Stop local music.");
        g_music.stop();
    } else if (musicAction == "volume") {
        localMusicVolume_ = volumeLevel;
        Serial.printf("[Control] Set local music volume: %u\n", static_cast<unsigned>(localMusicVolume_));
        g_music.setVolume(localMusicVolume_);
    } else if (musicAction == "next") {
        clearPendingMoodMusic();
        const String activeMood = !moodLabel.isEmpty() ? moodLabel : (!currentMoodLabel_.isEmpty() ? currentMoodLabel_ : defaultMoodForCurrentScene());
        const int track = stepMoodTrack(activeMood, g_music.currentTrack(), 1);
        if (track > 0) {
            currentMoodLabel_ = activeMood;
            Serial.printf("[Control] Next mood track: mood=%s track=%d\n", activeMood.c_str(), track);
            g_music.playSong(track);
        }
    } else if (musicAction == "previous") {
        clearPendingMoodMusic();
        const String activeMood = !moodLabel.isEmpty() ? moodLabel : (!currentMoodLabel_.isEmpty() ? currentMoodLabel_ : defaultMoodForCurrentScene());
        const int track = stepMoodTrack(activeMood, g_music.currentTrack(), -1);
        if (track > 0) {
            currentMoodLabel_ = activeMood;
            Serial.printf("[Control] Previous mood track: mood=%s track=%d\n", activeMood.c_str(), track);
            g_music.playSong(track);
        }
    } else if (musicAction == "random") {
        clearPendingMoodMusic();
        const String activeMood = !moodLabel.isEmpty() ? moodLabel : (!currentMoodLabel_.isEmpty() ? currentMoodLabel_ : defaultMoodForCurrentScene());
        const int track = pickMoodTrack(activeMood);
        if (track > 0) {
            currentMoodLabel_ = activeMood;
            Serial.printf("[Control] Random mood track: mood=%s track=%d\n", activeMood.c_str(), track);
            g_music.playSong(track);
        }
    } else if (musicAction == "play_mood") {
        clearPendingMoodMusic();
        const String activeMood = !moodLabel.isEmpty() ? moodLabel : defaultMoodForCurrentScene();
        const int track = pickMoodTrack(activeMood);
        if (track > 0) {
            currentMoodLabel_ = activeMood;
            Serial.printf("[Control] Play mood track now: mood=%s track=%d\n", activeMood.c_str(), track);
            g_music.playSong(track);
        }
    }

    if (offlineMusicId > 0 && (shouldPlayMusicControl || shouldPlaySceneControl || shouldPlayOfflineControl)) {
        clearPendingMoodMusic();
        Serial.printf("[Control] Play track from control: %u\n", static_cast<unsigned>(offlineMusicId));
        g_music.playSong(offlineMusicId);
    } else if ((shouldPlaySceneControl || shouldPlayOfflineControl) && currentScene_ != nullptr) {
        clearPendingMoodMusic();
        Serial.printf(
            "[Control] Play scene fallback track: %u\n",
            static_cast<unsigned>(currentScene_->offlineMusicId));
        g_music.playSong(currentScene_->offlineMusicId);
    }

    if ((type == "reply_control" || type == "scene_control") && moodMusicEnabled && !moodLabel.isEmpty()) {
        pendingMoodLabel_ = moodLabel;
        pendingMoodMusicSwitch_ = true;
        if (!ttsEnabled || spokenText.isEmpty()) {
            playMoodTrackAfterReply();
        }
    } else if (type != "reply_control" && type != "scene_control") {
        clearPendingMoodMusic();
    }

    lastDisplayTitle_ = displayTitle;
    lastReplyText_ = spokenText;
    Serial.printf("[Motor] Placeholder mode = %s\n", motorMode.c_str());

    if (type == "offline_control") {
        enterOfflineMode(spokenText.isEmpty() ? "云端降级" : spokenText);
        return;
    }

    if (ttsEnabled && !spokenText.isEmpty()) {
        setUiState(UiState::Processing, displayTitle, spokenText, currentScene_ ? currentScene_->sceneName : type);
    } else if (!spokenText.isEmpty()) {
        setUiState(UiState::Reply, displayTitle, spokenText, currentScene_ ? currentScene_->sceneName : type);
    }
}

// 设置统一 UI 状态，并同步 OLED 与灯效表现。
void AppController::setUiState(UiState state, const String& title, const String& subtitle, const String& footer) {
    uiState_ = state;
    currentTitle_ = title;
    currentSubtitle_ = subtitle;
    currentFooter_ = footer;

    g_oled.showStatus(currentTitle_, currentSubtitle_, currentFooter_, resolveDisplayMood());

    switch (uiState_) {
        case UiState::Booting:
            g_led.setMode("processing", 32, 128, 255, 90);
            break;
        case UiState::Idle:
            g_led.setMode("idle", 0, 180, 48, 90);
            break;
        case UiState::Processing:
            g_led.setMode(pendingLightMode_ == "idle" ? "processing" : pendingLightMode_, pendingLightR_, pendingLightG_, pendingLightB_, pendingLightSpeed_);
            break;
        case UiState::Recording:
            g_led.setMode("processing", 255, 96, 0, 80);
            break;
        case UiState::Reply:
            g_led.setMode("mood_" + resolveDisplayMood(), pendingLightR_, pendingLightG_, pendingLightB_, pendingLightSpeed_);
            break;
        case UiState::Speaking:
            g_led.setMode("voice_" + resolveDisplayMood(), pendingLightR_, pendingLightG_, pendingLightB_, 50);
            break;
        case UiState::Offline:
            g_led.setMode("offline", 255, 80, 0, 120);
            break;
        case UiState::Error:
            g_led.setMode("error", 255, 0, 0, 200);
            break;
    }
}

// 设置空闲态文案与页脚联网状态。
void AppController::setIdleState() {
    setUiState(
        UiState::Idle,
        currentScene_ ? currentScene_->sceneName : "等待交互",
        currentScene_ ? "可继续语音提问" : "请刷 NFC 卡或输入文本",
        network.isConnected() ? "Cloud Online" : "Cloud Offline");
}

// 刷新显示与灯带动画。
void AppController::refreshIndicators() {
    g_oled.showStatus(currentTitle_, currentSubtitle_, currentFooter_, resolveDisplayMood());
    g_led.update();
}

// 根据当前场景给出默认情绪标签。
String AppController::defaultMoodForCurrentScene() const {
    if (currentScene_ == nullptr) {
        return "calm";
    }
    if (String(currentScene_->sceneId) == "focus") {
        return "focus";
    }
    if (String(currentScene_->sceneId) == "sleep") {
        return "calm";
    }
    if (String(currentScene_->sceneId) == "healing") {
        return "happy";
    }
    return "calm";
}

// 解析当前应显示的情绪图标标签。
String AppController::resolveDisplayMood() const {
    if (uiState_ == UiState::Booting) {
        return "boot";
    }
    if (uiState_ == UiState::Error || uiState_ == UiState::Offline) {
        return "sad";
    }
    if (uiState_ == UiState::Idle && currentScene_ == nullptr && networkOnline_) {
        return "online";
    }
    if (!currentMoodLabel_.isEmpty()) {
        return currentMoodLabel_;
    }
    if (uiState_ == UiState::Recording || uiState_ == UiState::Processing) {
        return "focus";
    }
    return defaultMoodForCurrentScene();
}

// 为指定情绪挑选曲目，并尽量避免短时间重复播放。
int AppController::pickMoodTrack(const String& moodLabel) {
    const MoodTrackProfile* profile = findMoodProfile(moodLabel);
    if (profile == nullptr) {
        return 0;
    }

    MoodTrackMemory* memory = memoryForMood(moodLabel);
    if (memory == nullptr) {
        return random(profile->startTrack, profile->endTrack + 1);
    }

    const int rangeCount = profile->endTrack - profile->startTrack + 1;
    int track = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        track = random(profile->startTrack, profile->endTrack + 1);
        if (rangeCount <= 3 || !wasRecentlyPlayed(*memory, static_cast<uint8_t>(track))) {
            break;
        }
    }

    if (rangeCount > 3 && wasRecentlyPlayed(*memory, static_cast<uint8_t>(track))) {
        for (int candidate = profile->startTrack; candidate <= profile->endTrack; ++candidate) {
            if (!wasRecentlyPlayed(*memory, static_cast<uint8_t>(candidate))) {
                track = candidate;
                break;
            }
        }
    }

    rememberMoodTrack(*memory, static_cast<uint8_t>(track));
    return track;
}

// 返回对应情绪的最近播放记忆槽。
int AppController::stepMoodTrack(const String& moodLabel, int currentTrack, int direction) const {
    const MoodTrackProfile* profile = findMoodProfile(moodLabel);
    if (profile == nullptr) {
        return 0;
    }

    if (currentTrack < profile->startTrack || currentTrack > profile->endTrack) {
        return direction >= 0 ? profile->startTrack : profile->endTrack;
    }

    const int rangeCount = profile->endTrack - profile->startTrack + 1;
    const int offset = currentTrack - profile->startTrack;
    const int nextOffset = (offset + direction + rangeCount) % rangeCount;
    return profile->startTrack + nextOffset;
}

AppController::MoodTrackMemory* AppController::memoryForMood(const String& moodLabel) {
    if (moodLabel == "happy") {
        return &happyTrackMemory_;
    }
    if (moodLabel == "calm") {
        return &calmTrackMemory_;
    }
    if (moodLabel == "focus") {
        return &focusTrackMemory_;
    }
    if (moodLabel == "tired") {
        return &tiredTrackMemory_;
    }
    if (moodLabel == "sad") {
        return &sadTrackMemory_;
    }
    return nullptr;
}

// 判断候选曲目是否在最近播放记录中。
bool AppController::wasRecentlyPlayed(const MoodTrackMemory& memory, uint8_t track) const {
    for (uint8_t recent : memory.tracks) {
        if (recent == track && recent != 0) {
            return true;
        }
    }
    return false;
}

// 将本次播放曲目写入循环记忆，用于去重。
void AppController::rememberMoodTrack(MoodTrackMemory& memory, uint8_t track) {
    memory.tracks[memory.nextIndex % 3] = track;
    memory.nextIndex = (memory.nextIndex + 1) % 3;
}

// 在回复结束后按待切换情绪播放对应本地曲目。
void AppController::playMoodTrackAfterReply() {
    if (!pendingMoodMusicSwitch_ || pendingMoodLabel_.isEmpty()) {
        return;
    }

    const int track = pickMoodTrack(pendingMoodLabel_);
    if (track > 0) {
        Serial.printf("[Mood] Play mood track: label=%s track=%d\n", pendingMoodLabel_.c_str(), track);
        g_music.playSong(track);
    } else {
        Serial.printf("[Mood] No track mapping for mood=%s\n", pendingMoodLabel_.c_str());
    }
    currentMoodLabel_ = pendingMoodLabel_;
    clearPendingMoodMusic();
}

// 清空待切换情绪音乐的临时状态。
void AppController::clearPendingMoodMusic() {
    pendingMoodMusicSwitch_ = false;
    pendingMoodLabel_.clear();
}

// 根据 UID 在场景表中查找匹配项。
const SceneProfile* AppController::findSceneByUid(const String& uid) const {
    for (const auto& scene : kScenes) {
        if (uid == scene.uid) {
            return &scene;
        }
    }
    return nullptr;
}
