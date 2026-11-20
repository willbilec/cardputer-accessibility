#pragma once
#include <Arduino.h>
#include "keyboard_map.h"

bool startUnifiedUSB();
bool unifiedUSBConnected();
bool keyboardRadioRunning();
void setHidBatteryLevel(int percent);
String keyboardDeviceName(int slot);
enum class KeyboardStartup { Ready, InsufficientMemory, Controller, Services, Advertising };
KeyboardStartup startKeyboardSlot(int slot, const String &peer, bool pair, const String (&otherPeers)[2]);
void stopKeyboardSlot();
bool pollKeyboardSlot(String &pairedPeer);
bool sendUnifiedHid(bool usb, bool mouse, const keyboardMap::Map &map, bool alternate,
                    bool release = false, bool optionTap = false);
