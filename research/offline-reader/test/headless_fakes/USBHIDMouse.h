#pragma once
constexpr unsigned MOUSE_BUTTON_LEFT=1,MOUSE_BUTTON_RIGHT=2;
struct USBHIDMouse {
 unsigned buttons=0,moves=0;int x=0,y=0;
 void begin(){}
 void press(unsigned b){buttons|=b;}
 void release(unsigned b){buttons&=~b;}
 void move(int a,int b){x=a;y=b;++moves;}
};
