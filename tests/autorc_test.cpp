#include "../Arduino/MakerMiniSumo_AutoRC/MakerMiniSumo_AutoRC.ino"
#include <cassert>
#include <iostream>
void resetTest() {
 defaults(); configSession=false; selectedMode=0; buttonStart=true; attacking=false;
 testNow=100; forwardTrim=backwardTrim=0;
 for(int i=0;i<32;i++){digitalPins[i]=HIGH;analogPins[i]=800;}
 edgeLeftThreshold=edgeRightThreshold=400; stopRobot(); enterState(FIGHT);
}
void command(const char *s){strcpy(serialLine,s);Serial.output.clear();handleCommand();}
int main(){
 resetTest();runRobot();assert(MakerSumo.motors[0]==MakerSumo.motors[1] && MakerSumo.motors[0]>0);
 // Defense exits for every sensor, also during the forward pulse.
 for(int p: {OPP_L,OPP_FL,OPP_FC,OPP_FR,OPP_R}) {
  resetTest();selectedMode=5;defenseCount=0;enterState(DEF_MOVE);digitalPins[p]=LOW;runRobot();assert(state==FIGHT && MakerSumo.motors[0]==0);
 }
 resetTest();selectedMode=5;defenseCount=0;config.defense[0]=2;enterState(DEF_WAIT);
 for(int i=0;i<2;i++){testNow+=2000;runRobot();assert(state==DEF_MOVE);testNow+=50;runRobot();assert(defenseCount==i+1);}
 runRobot();assert(state==FIGHT);
 resetTest();selectedMode=5;config.defense[0]=0;defenseCount=0;enterState(DEF_WAIT);runRobot();assert(state==FIGHT);
 // Disabled rows are skipped; no enabled steps goes straight to fight.
 resetTest();stepIndex=0;enterState(OPENING);runRobot();assert(state==FIGHT);
 resetTest();config.steps[0][4]=1;config.steps[0][5]=70;stepIndex=0;enterState(OPENING);runRobot();assert(stepIndex==1 && MakerSumo.motors[0]>0);testNow+=100;runRobot();runRobot();assert(state==FIGHT);
 // Edge preempts opening; IR STOP preempts all moving states.
 resetTest();enterState(OPENING);analogPins[EDGE_L]=100;runRobot();assert(state==BACK_REVERSE && MakerSumo.motors[0]<0);
 for(RunState s: {OPENING,DEF_WAIT,DEF_MOVE,FIGHT,BACK_REVERSE,BACK_TURN,BACK_PAUSE}) {
  resetTest();buttonStart=false;digitalPins[START]=LOW;enterState(s);runRobot();assert(state==STOPPED && MakerSumo.motors[0]==0);
 }
 resetTest();enterState(IDENTIFY);initialHigh=true;inputAt=testNow;testNow+=50;runRobot();assert(buttonStart && state==WAIT_START);
 digitalPins[START]=LOW;testNow+=25;runRobot();assert(state==COUNTDOWN);testNow+=5000;runRobot();assert(state==OPENING);
 resetTest();digitalPins[START]=LOW;initialHigh=false;inputAt=testNow;enterState(IDENTIFY);testNow+=50;runRobot();assert(!buttonStart && state==WAIT_START);digitalPins[START]=HIGH;runRobot();assert(state==OPENING);
 // RC neutral, full throttle, loss and invalid pulse.
 resetTest();selectedMode=7;rcSpeedPulseWidth=2000;rcSteeringPulseWidth=1500;rcSpeedLastPulseAt=rcSteeringLastPulseAt=micros();runRobot();assert(MakerSumo.motors[0]==255);testNow+=31;runRobot();assert(MakerSumo.motors[0]==0);
 rcSpeedLastPulseAt=rcSteeringLastPulseAt=micros();rcSpeedPulseWidth=500;runRobot();assert(MakerSumo.motors[0]==0);
 // Complete saves, range validation, checksum and lock.
 resetTest();command("SAVE F 10");assert(Serial.output=="ERROR CONFIG_REQUIRED\n");command("CONFIG ON");runRobot();assert(MakerSumo.motors[0]==0);
 command("SAVE DEF 2 1500 40 45 60");assert(Serial.output=="OK SAVED DEF\n" && config.defense[0]==2);
 command("SAVE DEF 2 1500 40 45 60 extra");assert(Serial.output=="ERROR VALUE\n");
 defaults();loadSettings();assert(config.defense[0]==2);EEPROM.bytes[CONFIG_ADDRESS+4]^=1;loadSettings();assert(config.defense[0]==3);
 command("SAVE STR 0 1 40 40 100 0 0 0 100 0 0 0 100 0 0 0 100 0 0 0 100");assert(Serial.output=="OK SAVED STR 0\n");
 command("SAVE STR 5 1");assert(Serial.output=="ERROR STRATEGY\n");
 Serial.input=std::string(210,'x')+"SAVE F 12\n";while(Serial.available())processSerial();assert(forwardTrim==0);
 // Time rollover still advances durations correctly.
 resetTest();testNow=UINT32_MAX-20;enterState(DEF_WAIT);defenseCount=0;config.defense[1]=50;testNow=40;runRobot();assert(state==DEF_MOVE);
 std::cout<<"AutoRC state, protocol, EEPROM and failsafe tests passed\n";
}
