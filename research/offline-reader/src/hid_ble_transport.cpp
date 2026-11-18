// Bluetooth-only adaptation keeps the USB Serial/JTAG recovery path available.
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include "bluetooth_transport_generated.h"
void releaseHidReports(bool usb) { if(!usb && getBluetoothStatus()) sendEmptyReports(); }
