#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <ArduinoWebsockets.h>

class NetworkHandler {
public:
    void begin();
    void loop();
    bool isConnected();
    bool sendText(const String& message);
    bool sendBinary(const char* data, size_t length);

private:
    void ensureWiFi();
    void ensureWebSocket();
    void configureCallbacks();

    websockets::WebsocketsClient client_;
    bool wifiReady_ = false;
    bool wsConnected_ = false;
    unsigned long lastWiFiAttemptMs_ = 0;
    unsigned long lastWsAttemptMs_ = 0;
    unsigned long lastHeartbeatMs_ = 0;
};

extern NetworkHandler network;

#endif
