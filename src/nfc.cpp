#include "nfc.h"

#include "config.h"

#include <MFRC522.h>
#include <SPI.h>

namespace {
MFRC522 g_reader(RC522_CS, RC522_RST);
}

// 初始化 RC522 读卡器并输出版本信息。
void NFC::begin() {
    Serial.println("[NFC] Init RC522...");
    SPI.begin(RC522_SCK, RC522_MISO, RC522_MOSI, RC522_CS);
    g_reader.PCD_Init();

    const byte version = g_reader.PCD_ReadRegister(MFRC522::VersionReg);
    if (version == 0x00 || version == 0xFF) {
        Serial.println("[NFC] Reader not detected. Check SPI wiring and power.");
        return;
    }

    Serial.print("[NFC] Version register = 0x");
    Serial.println(version, HEX);
    Serial.println("[NFC] Ready.");
}

// 读取 NFC 卡 UID，未检测到卡时返回空字符串。
String NFC::readCard() {
    if (!g_reader.PICC_IsNewCardPresent()) {
        return "";
    }
    if (!g_reader.PICC_ReadCardSerial()) {
        return "";
    }

    char uidBuffer[21] = {0};
    for (byte index = 0; index < g_reader.uid.size && index < 10; ++index) {
        snprintf(&uidBuffer[index * 2], 3, "%02X", g_reader.uid.uidByte[index]);
    }

    g_reader.PICC_HaltA();
    g_reader.PCD_StopCrypto1();
    return String(uidBuffer);
}
