#pragma once
inline bool fakeUsbInitOk=true,fakeUsbConnected=true;
struct {bool begin(){return fakeUsbInitOk;}} inline USB;
