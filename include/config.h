#ifndef CONFIG_H
#define CONFIG_H

// ==========================================
// 1. 网络与 WebSocket 服务端配置
// ==========================================
#define WIFI_SSID "Yinuo"
#define WIFI_PASSWORD "yinuo331"
#define WEBSOCKET_SERVER "172.20.10.2"
#define WEBSOCKET_PORT 8765
#define DEVICE_ID "esp32s3_nfc_box_01"
#define HEARTBEAT_INTERVAL 30000

// ==========================================
// 2. 语音链路引脚
// ==========================================
#define I2S_MIC_WS 4
#define I2S_MIC_SD 5
#define I2S_MIC_SCK 6
#define RECORD_BUTTON_PIN 47

// MAX98357A 扬声器 I2S 输出引脚
#define I2S_SPK_BCLK 15
#define I2S_SPK_LRC 16
#define I2S_SPK_DIN 9

// ==========================================
// 3. OLED 显示屏配置
// ==========================================
#define I2C_SDA 41
#define I2C_SCL 42
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

// ==========================================
// 4. RC522 NFC 模块配置
// ==========================================
#define RC522_CS 10
#define RC522_MOSI 11
#define RC522_SCK 12
#define RC522_MISO 13
#define RC522_RST 14

// ==========================================
// 5. DFPlayer Mini 音乐模块配置
// ==========================================
#define DFPLAYER_RX_PIN 17
#define DFPLAYER_TX_PIN 18

// ==========================================
// 6. WS2812 灯带配置
// ==========================================
#define LED_PIN 38
#define NUM_LEDS 8

#endif
