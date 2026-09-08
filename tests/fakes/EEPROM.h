#pragma once
#include <cstdint>
#include <cstring>
struct FakeEEPROM {
 uint8_t bytes[1024]={0};
 uint8_t read(int a){return bytes[a];}
 void update(int a,uint8_t v){bytes[a]=v;}
 template<class T> void put(int a,const T &v){memcpy(bytes+a,&v,sizeof(v));}
 template<class T> void get(int a,T &v){memcpy(&v,bytes+a,sizeof(v));}
} EEPROM;
