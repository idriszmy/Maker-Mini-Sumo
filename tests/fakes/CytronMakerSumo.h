#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <algorithm>
#define F(x) x
#define GPIO1 16
#define GPIO2 17
#define START 2
#define LED 18
#define EDGE_L 14
#define EDGE_R 15
#define OPP_L 12
#define OPP_FL 5
#define OPP_FC 6
#define OPP_FR 7
#define OPP_R 13
#define MOTOR_L 0
#define MOTOR_R 1
#define INPUT_PULLUP 2
#define LOW 0
#define HIGH 1
#define PC2 2
#define PC3 3
#define PCINT10 2
#define PCINT11 3
#define PCIF1 1
#define PCIE1 1
#define _BV(x) (1<<(x))
uint8_t PINC=0, PCIFR=0, PCMSK1=0, PCICR=0;
uint32_t testNow=100;
int digitalPins[32], analogPins[32];
uint32_t millis(){return testNow;}
uint32_t micros(){return testNow*1000;}
void noInterrupts(){} void interrupts(){}
void pinMode(int,int){}
int digitalRead(int p){return digitalPins[p];}
int analogRead(int p){return analogPins[p];}
void digitalWrite(int p,int v){digitalPins[p]=v;}
template<class T> T constrain(T v,T lo,T hi){return std::max(lo,std::min(v,hi));}
struct FakeSerial {
 std::string output, input;
 void begin(int){}
 int available(){return input.size();}
 char read(){char c=input[0];input.erase(0,1);return c;}
 template<class T> void print(T v){std::ostringstream s;s<<v;output+=s.str();}
 void print(uint8_t v){print(int(v));} void print(int8_t v){print(int(v));}
 void println(){output+='\n';}
 template<class T> void println(T v){print(v);println();}
 void println(float v,int){print(v);println();}
} Serial;
struct FakeSumo {
 int motors[2]={0,0}; int dip=0;
 void begin(){} void stop(){motors[0]=motors[1]=0;}
 void setMotorSpeed(int side,int value){motors[side]=value;}
 int readDipSwitch(){return dip;} float readBatteryVoltage(){return 7.4;}
} MakerSumo;
