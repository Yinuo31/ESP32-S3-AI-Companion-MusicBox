#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

struct SceneProfile {
    const char* uid;
    const char* sceneId;
    const char* sceneName;
    const char* scenePrompt;
    uint8_t offlineMusicId;
};

class AppController {
public:
    void begin();

    void onNetworkConnected();
    void onNetworkDisconnected();
    void onIncomingText(const String& text);
    void onIncomingControl(const String& json);
    void onTtsBegin();
    void onTtsEnd();
    void onCloudError(const String& errorText);

private:
    struct MoodTrackMemory {
        uint8_t tracks[3] = {0, 0, 0};
        uint8_t nextIndex = 0;
    };

    enum class UiState : uint8_t {
        Booting,
        Idle,
        Processing,
        Recording,
        Reply,
        Speaking,
        Offline,
        Error,
    };

    enum class EventType : uint8_t {
        NetworkOnline,
        NetworkOffline,
        TextReply,
        ControlJson,
        TtsBegin,
        TtsEnd,
        Error,
        RecordStarted,
        RecordUploaded,
    };

    struct AppEvent {
        EventType type;
        char text[384];
    };

    void createTasks();
    void postEvent(EventType type, const String& text = String());
    void networkTaskLoop();
    void uiTaskLoop();
    void audioCaptureTaskLoop();
    static void networkTaskEntry(void* parameter);
    static void uiTaskEntry(void* parameter);
    static void audioCaptureTaskEntry(void* parameter);

    void handleEvent(const AppEvent& event);
    void handleSerialInput();
    bool tryHandleLocalCommand(const String& text);
    void handleButtonInput();
    void handleNfcInput();
    void enterOfflineMode(const String& reason);
    void exitOfflineMode();
    void startRecording();
    void sendSceneEvent(const SceneProfile& scene, const String& source);
    void sendTextQuery(const String& text);
    void applyControlJson(const String& jsonText);
    void setUiState(UiState state, const String& title, const String& subtitle = String(), const String& footer = String());
    void setIdleState();
    void refreshIndicators();
    const SceneProfile* findSceneByUid(const String& uid) const;
    String defaultMoodForCurrentScene() const;
    String resolveDisplayMood() const;
    int pickMoodTrack(const String& moodLabel);
    int stepMoodTrack(const String& moodLabel, int currentTrack, int direction) const;
    MoodTrackMemory* memoryForMood(const String& moodLabel);
    bool wasRecentlyPlayed(const MoodTrackMemory& memory, uint8_t track) const;
    void rememberMoodTrack(MoodTrackMemory& memory, uint8_t track);
    void playMoodTrackAfterReply();
    void clearPendingMoodMusic();

    QueueHandle_t eventQueue_ = nullptr; 
    QueueHandle_t recordQueue_ = nullptr;
    TaskHandle_t networkTaskHandle_ = nullptr;
    TaskHandle_t uiTaskHandle_ = nullptr;
    TaskHandle_t audioTaskHandle_ = nullptr;

    UiState uiState_ = UiState::Booting;
    const SceneProfile* currentScene_ = nullptr;
    String currentTitle_;
    String currentSubtitle_;
    String currentFooter_;
    String lastReplyText_;
    String lastDisplayTitle_;
    String pendingLightMode_ = "idle";
    uint8_t pendingLightR_ = 0;
    uint8_t pendingLightG_ = 160;
    uint8_t pendingLightB_ = 32;
    uint16_t pendingLightSpeed_ = 120;
    bool offlineMode_ = false;
    bool networkOnline_ = false;
    bool ttsPlaying_ = false;
    unsigned long lastButtonTriggerMs_ = 0;
    unsigned long lastCardSeenMs_ = 0;
    String lastCardUid_;
    String currentMoodLabel_ = "calm";
    String pendingMoodLabel_;
    bool pendingMoodMusicSwitch_ = false;
    uint8_t localMusicVolume_ = 10;
    MoodTrackMemory happyTrackMemory_;
    MoodTrackMemory calmTrackMemory_;
    MoodTrackMemory focusTrackMemory_;
    MoodTrackMemory tiredTrackMemory_;
    MoodTrackMemory sadTrackMemory_;
};

extern AppController app;

#endif
