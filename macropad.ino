// These define's must be placed at the beginning before #include "TimerInterrupt_Generic.h"
// _TIMERINTERRUPT_LOGLEVEL_ from 0 to 4
// Don't define _TIMERINTERRUPT_LOGLEVEL_ > 0. Only for special ISR debugging only. Can hang the system.
#define TIMER_INTERRUPT_DEBUG         0
#define _TIMERINTERRUPT_LOGLEVEL_     0
//

#include <RPi_Pico_TimerInterrupt.h>
#include <Adafruit_MCP23X17.h>
#include <Wire.h>
#include <Keyboard.h>
#include <Adafruit_NeoPixel.h>

#define TIMER0_INTERVAL_MS            20

Adafruit_MCP23X17 mcp;

// Init RPI_PICO_Timer, can use any from 0-15 pseudo-hardware timers
RPI_PICO_Timer ITimer0(0);

unsigned long timer0Stop = 0; //Time timer0 was stopped from millis()
unsigned long timer0Time = 0; 
unsigned long timer0Diff = 0; //Time timer0 has been paused in mS

bool txFlag = false;  //Radio is transmitting
bool escFlag = false; //Escape condition exists


bool mcpFlag = false; //New IO event on MCP
int mcpBtn = 99;       //Pin MCP event happened on 
int mcpBtnOld = 99;

bool keyFlag = false; //New IO event on Macropad
bool keyChangeFlag = false; //Is the same key being held down?
int keyNum = 99;      //Key Macropad event occurred on. 0 = encoder button, 1-12 = F keys

int keyState = 0;     //TESTING KEY STATE COMPARE
int keyStatus = 0;
int keyStatusOld = 0;
int keyWorking = 0;

int mcpState = 0;
int mcpStatus = 0;
int mcpStatusOld = 0;
int mcpWorking = 0;

bool stopFlag = false; //Timer is paused
int loopCount = 0;      //# of loops completed since the timer event was paused //USE TO TEST KEY ACTION DURATION

bool slice = 0;          //Which Slice is TX enabled? 0=A, 1=B


//TEST INPUTS ON MCP//
bool testBtn4 = 0;    //txFlag SIM
bool testBtn5 = 0;    //Active Slice SIM
bool testbtn6 = 0;    //AVAILABLE
bool testBtn7 = 0;    //AVAILABLE


//---------------//

bool TimerHandler0(struct repeating_timer *t) {     //TIMER0 INTERRUPT ROUTINE that scans all IO pins for changes every 20mS. Timer is paused for 70mS after change //
  (void) t;

  //TEST INPUTS//
  txFlag = !mcp.digitalRead(4);  //SIMULATED TX FLAG INPUT, REPLACE WITH INPUT FROM PC//
  slice = !mcp.digitalRead(5);   //SIMULATED SLICE INPUT, REPLACE WITH INPUT FROM PC//

  for (int i=0; i<=3; i++) {        //Read IO Expansion pins into int mcpStatus
    mcpState = !mcp.digitalRead(i);
    if (mcpState == HIGH) {
      mcpWorking |= 1 << i;
    }
    if (i==3) {
      mcpStatus = mcpWorking;
    }
  }

  mcpWorking = 0;

  if (mcpStatus != mcpStatusOld) {  //Checks mcpStatus for change and prevents repeated key calls with long press
    if (mcpStatus > 0) {
      mcpFlag = true;
    }
    mcpStatusOld = mcpStatus;
  }


  for (int i=0; i<=12; i++) {     //Reads macropad keys into int keyStatus
    keyState = !digitalRead(i);
    if (keyState == HIGH) {
      keyWorking |= 1 << i;
    }
    if (i==12) {
      keyStatus = keyWorking;
    }
  }

  keyWorking = 0;

  if (keyStatus != keyStatusOld) {  //Checks keyStatus for changes and prevents repeated key calls with long press

    if (keyStatus > 0) {
      keyFlag = true;
    }

    keyStatusOld = keyStatus;
  }

  if (keyFlag == true || mcpFlag == true || escFlag == true) {    //Stops timer for 50mS after key state change
    timer0Stop = millis();
    ITimer0.stopTimer();
    stopFlag = true;
  }

  return true;
}

//--------------//

void keyDetect() {        //switches all IO pins to determine key state and calls appropriate key action function. MCP pins 4-7 are handled in timer interrupt
 
  if (keyStatus > 0) {      //Determine action if macropad key strike
    switch (keyStatus) {
     case 1:
        keyNum = 0;
        escape();
        break;
      case 2:
        keyNum = 1;
        keyAction();
        break;
      case 4:
        keyNum = 2;
        keyAction();
        break;
      case 8:
        keyNum = 3;
        keyAction();
        break;
      case 16:
        keyNum = 4;
        keyAction();
        break;
      case 32:
        keyNum = 5;
        keyAction();
        break;
      case 64:
        keyNum = 6;
        keyAction();
        break;
      case 128:
        keyNum = 7;
        keyAction();
        break;
      case 256:
        keyNum = 8;
        keyAction();
        break;
      case 512:
        keyNum = 9;
        break;
      case 1024:
        keyNum = 10;
        break;
      case 2048:
        keyNum = 11;
        break;
      case 4096:
        keyNum = 12;
        wipeKey();
        break;
    }
    keyStatus = 0;
  }


  if (mcpStatus > 0) {      //Determine action if MCP23017 expansion board key strike. Includes PTT pedals and Fn key currently
    switch (mcpStatus) {
      case 1:
        leftPtt();
        break;
      case 2:
        rightPtt();
        break;
      case 4:
        fnKeyPress();
        break;
      case 8:
        sliceCheck();
        break;
    }
    mcpStatus = 0;
  }
}

//---------------//

void escape() {       //Escape action, terminates current macro if txFlag = true when called
  escFlag = true;
  Keyboard.write(KEY_ESC);
  Serial.println("!");
  keyStatus = 0;
}

//--------------//

void keyAction() {            //Macropad key actions minus F12 (wipe) 
  Keyboard.write(193+keyNum);
  Serial.print(keyNum);
  Serial.println("Key");
  keyStatus = 0;
}

//----------------//

void wipeKey() {            //F12 (Wipe) key action
  Serial.println("wipeKey() Called");
  mcpStatus = 0;
}

//---------------//

void leftPtt() {          //Left pedal action. Focus and PTT handled by AHK. Escape handled by timer interrupt and escape()
  Serial.println("Left Pedal");
  mcpStatus = 0;
}

//---------------//

void rightPtt() {       //Right pedal action. Focus and PTT handled by AHK. Escape handled by timer interrupt and escape()
  Serial.println("Right Pedal");
  mcpStatus = 0;
}

//---------------//

void fnKeyPress() {     //Fn key allows rotary encoder to switch between VFO and CW speed adjust. Also allows F1 and F3 to adjust LED brightness
  Serial.println("Fn Key");
  mcpStatus = 0;
}

//---------------//

void sliceCheck() {     //Checks the current TX enabled slice
  if (slice == 0){
    Serial.println("SLICE A");
  }
  if (slice == 1) {
    Serial.println("SLICE B");
  }
}

//---------------//

void setup() {              //SETUP FUNCTION

  Serial.begin(115200);     //Start Serial, Wire, Keyboard, Mouse//
  while (!Serial);
  Wire.begin();
  Keyboard.begin();

  if (!mcp.begin_I2C()) {      //Start I2C
    Serial.println("I2C Error");
    while (1);
  }

  for (uint8_t i=0; i<=7; i++) {    //Configure I2C IO pin as active low inputs
    mcp.pinMode(i, INPUT_PULLUP);
  }

  for (uint8_t i=0; i<=12; i++) {   //Configure macropad pins as active low inputs
    pinMode(i, INPUT_PULLUP);
  }

  ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS * 1000, TimerHandler0);      //Begin Timer0

}

//---------------//

void loop() {

  if (keyFlag == true || mcpFlag == true) {   //If a key is struck
    keyDetect();
  }


  if (stopFlag == true) {         //KEEP AT BOTTOM OF MAIN LOOP. Tracks TIMER0 stop duration and restarts it. Clears any flags that trigger TIMER0.stopTimer()
      timer0Time = millis();
      timer0Diff = timer0Time - timer0Stop;

    if (timer0Diff >= 70) {
      ITimer0.restartTimer();
      keyFlag = false;
      mcpFlag = false;
      escFlag = false;
      stopFlag = false;

    }
  }
}
