#include "../Arduino/MakerMiniSumo_AutoRC/MakerMiniSumo_AutoRC.ino"
#include <cassert>
#include <iostream>
void resetTest() {
 defaults(); configSession=false; selectedMode=0; buttonStart=true; inputArmed=false; attacking=false; rcChannelsSwapped=false;
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
 resetTest();enterState(IDENTIFY);initialHigh=true;inputAt=identifyStartedAt=testNow;
 testNow+=999;runRobot();assert(state==IDENTIFY);testNow+=1;runRobot();assert(buttonStart && state==WAIT_START);
 // A start is ignored until the selected input has first been observed idle.
 digitalPins[START]=LOW;testNow+=25;runRobot();assert(state==WAIT_START);
 digitalPins[START]=HIGH;runRobot();digitalPins[START]=LOW;testNow+=25;runRobot();assert(state==COUNTDOWN);
 testNow+=5000;runRobot();assert(state==OPENING);
 // An IR output that settles LOW during startup is classified as active-high IR.
 resetTest();initialHigh=true;inputAt=identifyStartedAt=testNow;enterState(IDENTIFY);
 testNow+=200;digitalPins[START]=LOW;runRobot();testNow+=800;runRobot();assert(!buttonStart && state==WAIT_START);
 runRobot();assert(inputArmed);digitalPins[START]=HIGH;runRobot();assert(state==OPENING);
 // Edge thresholds use the first run reading and POT sensitivity trim.
 resetTest();analogPins[EDGE_L]=800;analogPins[EDGE_R]=600;analogPins[POT]=0;startOpening();
 assert(edgeLeftThreshold==200 && edgeRightThreshold==150);
 analogPins[POT]=1023;startOpening();assert(edgeLeftThreshold==600 && edgeRightThreshold==450);
 // RC neutral, full throttle, loss and invalid pulse.
 resetTest();selectedMode=7;rcSpeedPulseWidth=2000;rcSteeringPulseWidth=1500;rcSpeedLastPulseAt=rcSteeringLastPulseAt=micros();runRobot();assert(MakerSumo.motors[0]==255);testNow+=31;runRobot();assert(MakerSumo.motors[0]==0);
 rcSpeedLastPulseAt=rcSteeringLastPulseAt=micros();rcSpeedPulseWidth=500;runRobot();assert(MakerSumo.motors[0]==0);
 // Swapped mapping reads GPIO2 as throttle and GPIO1 as steering.
 resetTest();selectedMode=7;rcChannelsSwapped=true;rcSpeedPulseWidth=1500;rcSteeringPulseWidth=2000;
 rcSpeedLastPulseAt=rcSteeringLastPulseAt=micros();runRobot();assert(MakerSumo.motors[0]==255 && MakerSumo.motors[1]==255);
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
 // Power-on sequence finishes LOW without blocking clock/control.
 resetTest();setup();assert(buzzerLength==3);uint32_t before=testNow;
 updateBuzzer();assert(testNow==before);
 for(int duration: {80,40,110}) {testNow+=duration;updateBuzzer();}
 assert(buzzerLength==0 && digitalPins[BUZZER]==LOW);
 command("CONFIG ON");command("SAVE F 5");assert(buzzerLength==5);
 for(int duration: {60,30,60,30,120}) {testNow+=duration;updateBuzzer();}
 assert(buzzerLength==0 && digitalPins[BUZZER]==LOW);
 command("SAVE F 26");assert(buzzerLength==0);
 command("GET CONFIG");assert(buzzerLength==0);
 command("SAVE DEF 2 1500 40 45 60");assert(buzzerLength==5);
 command("SAVE RC 1");assert(Serial.output=="OK SAVED RC=1\n" && rcChannelsSwapped);
 rcChannelsSwapped=false;loadSettings();assert(rcChannelsSwapped);
 command("GET RC");assert(Serial.output=="RC MAP=1\n");
 command("SAVE RC 2");assert(Serial.output=="ERROR VALUE\n" && rcChannelsSwapped);
 // RC failsafe still runs while a sound is playing.
 configSession=false;selectedMode=7;rcSpeedLastPulseAt=rcSteeringLastPulseAt=0;
 MakerSumo.motors[0]=255;loop();assert(MakerSumo.motors[0]==0);
 std::cout<<"AutoRC state, protocol, EEPROM and failsafe tests passed\n";
}
