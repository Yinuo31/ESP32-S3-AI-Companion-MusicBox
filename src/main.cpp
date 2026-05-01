#include <Arduino.h>

#include "app_controller.h"
#include "config.h"

void setup() {
    Serial.begin(115200);
    delay(1500);
    pinMode(RECORD_BUTTON_PIN, INPUT_PULLUP);
    app.begin();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
