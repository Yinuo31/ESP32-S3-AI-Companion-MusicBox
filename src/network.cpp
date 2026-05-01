#include "network.h"

#include "app_controller.h"
#include "audio.h"
#include "config.h"

#include <WiFi.h>

namespace {
constexpr unsigned long WIFI_RETRY_MS = 5000;
constexpr unsigned long WS_RETRY_MS = 3000;
}

NetworkHandler network;

// 网络模块初始化：配置回调并尝试建立 WiFi 与 WebSocket 连接。
void NetworkHandler::begin() {
    Serial.println("\n==============================");
    Serial.printf("[WiFi] Target SSID: %s\n", WIFI_SSID);
    configureCallbacks();
    ensureWiFi();
    ensureWebSocket();
    Serial.println("==============================\n");
}

// 配置 WebSocket 文本/二进制消息与连接状态回调。
void NetworkHandler::configureCallbacks() {
    client_.setFragmentsPolicy(websockets::FragmentsPolicy_Aggregate);

    client_.onMessage([](websockets::WebsocketsMessage message) {
        if (message.isBinary()) {
            const bool queued = myaudio.enqueueTtsAudio(
                reinterpret_cast<const uint8_t*>(message.c_str()),
                message.length());
            if (!queued) {
                Serial.printf("[WS] Failed to queue binary audio chunk: %u bytes\n",
                    static_cast<unsigned>(message.length()));
            }
            return;
        }

        const String text = message.data();
        if (text.startsWith("CMD:TTS_BEGIN")) {
            size_t totalBytes = 0;
            size_t chunkBytes = 0;
            const int a = text.indexOf('|');
            const int b = text.indexOf('|', a + 1);
            const int c = text.indexOf('|', b + 1);
            const int d = text.indexOf('|', c + 1);
            const int e = text.indexOf('|', d + 1);
            if (a > 0 && b > a && c > b && d > c && e > d) {
                totalBytes = static_cast<size_t>(text.substring(d + 1, e).toInt());
                chunkBytes = static_cast<size_t>(text.substring(e + 1).toInt());
                myaudio.onTtsBegin(totalBytes, chunkBytes);
                app.onTtsBegin();
            }
            return;
        }

        if (text == "CMD:TTS_END") {
            myaudio.onTtsEnd();
            app.onTtsEnd();
            return;
        }

        if (text.startsWith("{")) {
            app.onIncomingControl(text);
        } else {
            app.onIncomingText(text);
        }
    });

    client_.onEvent([this](websockets::WebsocketsEvent event, String data) {
        if (event == websockets::WebsocketsEvent::ConnectionOpened) {
            wsConnected_ = true;
            lastHeartbeatMs_ = millis();
            Serial.println("[WS] Connection opened.");
            app.onNetworkConnected();
        } else if (event == websockets::WebsocketsEvent::ConnectionClosed) {
            if (wsConnected_) {
                Serial.printf("[WS] Connection closed: %s\n", data.c_str());
                wsConnected_ = false;
                app.onNetworkDisconnected();
            }
        } else if (event == websockets::WebsocketsEvent::GotPing) {
            Serial.println("[WS] Ping received.");
        }
    });
}

// 确保 WiFi 已连接，未连接时按间隔重试。
void NetworkHandler::ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiReady_) {
            wifiReady_ = true;
            Serial.print("[WiFi] Connected. IP = ");
            Serial.println(WiFi.localIP());
        }
        return;
    }

    const unsigned long now = millis();
    if (now - lastWiFiAttemptMs_ < WIFI_RETRY_MS) {
        return;
    }

    lastWiFiAttemptMs_ = now;
    wifiReady_ = false;
    Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// 确保 WebSocket 已连接，未连接时按间隔重连。
void NetworkHandler::ensureWebSocket() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (wsConnected_ && client_.available()) {
        return;
    }

    const unsigned long now = millis();
    if (now - lastWsAttemptMs_ < WS_RETRY_MS) {
        return;
    }

    lastWsAttemptMs_ = now;
    Serial.printf("[WS] Connecting to ws://%s:%d\n", WEBSOCKET_SERVER, WEBSOCKET_PORT);
    const bool connected = client_.connect(WEBSOCKET_SERVER, WEBSOCKET_PORT, "/");
    if (!connected) {
        Serial.println("[WS] Connection attempt failed.");
    }
}

// 网络主循环：维持连接、轮询消息并发送心跳。
void NetworkHandler::loop() {
    ensureWiFi();
    ensureWebSocket();

    if (client_.available()) {
        client_.poll();
        if (wsConnected_ && millis() - lastHeartbeatMs_ >= HEARTBEAT_INTERVAL) {
            lastHeartbeatMs_ = millis();
            sendText(String("{\"type\":\"heartbeat\",\"device_id\":\"") + DEVICE_ID + "\"}");
        }
    } else if (wsConnected_) {
        wsConnected_ = false;
        app.onNetworkDisconnected();
    }
}

// 返回当前云端链路是否可用。
bool NetworkHandler::isConnected() {
    return wifiReady_ && wsConnected_ && client_.available();
}

// 发送文本消息到 WebSocket。
bool NetworkHandler::sendText(const String& message) {
    if (!isConnected()) {
        return false;
    }
    return client_.send(message);
}

// 发送二进制消息到 WebSocket。
bool NetworkHandler::sendBinary(const char* data, size_t length) {
    if (!isConnected()) {
        return false;
    }
    return client_.sendBinary(data, length);
}
