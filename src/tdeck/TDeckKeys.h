// The T-Deck's trackball (rolling; its click is the UI's button: user_btn) and keyboard, as
// the key codes the companion UI already takes (KEY_NEXT, KEY_ENTER, ...), one at a time.
#pragma once
#include <stdint.h>

// Backspace gives KEY_CANCEL: UITask goes back a screen

void tdeckKeysBegin();
char tdeckTakeKey();  // 0: none
bool tdeckKeysPending();  // the trackball has rolled (not taken yet)
void tdeckKeysActive(bool on);  // off: the keyboard isn't asked (screen off)
