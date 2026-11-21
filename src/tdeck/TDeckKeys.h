// The T-Deck's trackball (rolling; its click is the UI's button: user_btn) and keyboard, as
// the key codes the companion UI already takes (KEY_NEXT, KEY_ENTER, ...), one at a time.
#pragma once
#include <stdint.h>

// Backspace gives KEY_CANCEL: UITask goes back a screen. Any other key: TDECK_KEY_ACTIVITY (it
// wakes the screen and keeps it on, nothing else).
#define TDECK_KEY_ACTIVITY 0x7F

void tdeckKeysBegin();
char tdeckTakeKey();  // 0: none
bool tdeckKeysPending();  // the trackball has rolled (not taken yet)
void tdeckKeysActive(bool on);  // off: the keyboard isn't asked (screen off)
