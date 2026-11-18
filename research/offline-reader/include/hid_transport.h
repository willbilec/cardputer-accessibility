#pragma once
void initBluetooth();
bool getBluetoothStatus();
void handleBluetoothMode(bool mouseMode);
void handleUsbMode(bool mouseMode);
bool initUsbHid();
bool bluetoothTransportReady();
bool usbHostConnected();
void releaseHidReports(bool usb);
