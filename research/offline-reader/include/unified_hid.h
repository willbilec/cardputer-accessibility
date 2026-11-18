#pragma once
#include <Arduino.h>

bool startUnifiedUSB();
bool unifiedUSBConnected();
bool keyboardRadioRunning();
void setHidBatteryLevel(int percent);
String keyboardDeviceName(int slot);
enum class KeyboardStartup { Ready, InsufficientMemory, Controller, Services, Advertising };
KeyboardStartup startKeyboardSlot(int slot, const String &peer, bool pair, const String (&otherPeers)[2]);
void stopKeyboardSlot();
bool pollKeyboardSlot(String &pairedPeer);
void sendUnifiedHid(bool usb, bool mouse, bool release = false);
