#include <Arduino.h>
#include <Wire.h>
#include "headless_board.h"
// Same board drivers, individually invoked so an unsupported stage is visible.
void setup(){
 Serial.begin(115200);delay(200);Serial.println("BOARD_PROBE start");
 pinMode(38,OUTPUT);digitalWrite(38,LOW);pinMode(37,OUTPUT);digitalWrite(37,HIGH);pinMode(0,INPUT_PULLUP);
 Serial.println("BOARD_PROBE before_wire");
 bool wire=Wire.begin(8,9);Serial.printf("BOARD_PROBE wire=%d\n",wire);
 bool keyboard=M5Cardputer.Keyboard.begin();Serial.printf("BOARD_PROBE keyboard=%d\n",keyboard);
 bool audio=M5Cardputer.Speaker.begin();Serial.printf("BOARD_PROBE audio=%d\n",audio);
 Serial.println("BOARD_PROBE_COMPLETE");
}
void loop(){delay(10);}
