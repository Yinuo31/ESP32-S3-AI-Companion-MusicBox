#ifndef NFC_H
#define NFC_H

#include <Arduino.h>

class NFC {
public:
    void begin();
    String readCard();
};

#endif
