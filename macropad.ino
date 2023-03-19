// These define's must be placed at the beginning before #include "TimerInterrupt_Generic.h"
// _TIMERINTERRUPT_LOGLEVEL_ from 0 to 4
// Don't define _TIMERINTERRUPT_LOGLEVEL_ > 0. Only for special ISR debugging only. Can hang the system.
#define TIMER_INTERRUPT_DEBUG         0
#define _TIMERINTERRUPT_LOGLEVEL_     0
//

#include <RPi_Pico_TimerInterrupt.h>
#include <Adafruit_MCP23X17.h>
#include <RotaryEncoder.h>
#include <Mouse.h>
#include <Wire.h>
#include <Keyboard.h>
#include <Adafruit_NeoPixel.h>

#define TIMER0_INTERVAL_MS            20  //Polling rate for macropad and GPIO board keys; time in milliseconds

//---NeoPixel---//
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_NEOPIXEL, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);   // Create NeoPixel object and define pins

int pixelNum = 0;

//---Encoder---//
RotaryEncoder encoder(PIN_ROTA, PIN_ROTB, RotaryEncoder::LatchMode::FOUR3);   //Create Rotary Encoder, define pins, create ints
void checkPosition() {  encoder.tick(); }     // just call tick() to check the state.

int encoder_pos = 0;    // our encoder position state

volatile int encoder0Pos = 0;
volatile int encoder0Prev = 0;
volatile int encoder0Change = 0;


//---Timer---//
RPI_PICO_Timer ITimer0(0);    // Init RPI_PICO_Timer, can use any from 0-15 pseudo-hardware timers

unsigned long timer0Stop = 0; //Time timer0 was stopped from millis()
unsigned long timer0Time = 0; //Current time check
unsigned long timer0Diff = 0; //Time timer0 has been paused in mS


//---Radio State Variables---//
bool txFlag = false;  //Radio is transmitting
bool txFlagOld = false;
bool txFlagChange = false;

bool slice = 0;          //Which Slice is TX enabled? 0=A, 1=B
bool sliceOld = 0;
bool sliceChangeFlag = 0;

bool macroFlag = false;


//---MCP23017 GPIO Expansion objects and variables---//
Adafruit_MCP23X17 mcp;    //Create MCP23017 GPIO Expansion board object
bool mcpFlag = false; //New IO event on MCP
int mcpBtn = 99;       //Pin# MCP event happened on 

int mcpState = 0;     //Stuff for determining MCP board key press (pins 0-3)
int mcpStatus = 0;
int mcpStatusOld = 0;
int mcpWorking = 0;

bool testBtn4 = 0;    //txFlag SIM      REMOVE WHEN PC INPUT AVAILABLE
bool testBtn5 = 0;    //Active Slice SIM
bool testbtn6 = 0;    //AVAILABLE
bool testBtn7 = 0;    //AVAILABLE


//---Adafruit RP2040 Macropad variables---//
bool keyFlag = false; //New IO event on Macropad
int keyNum = 99;      //Key Macropad event occurred on. 0 = encoder button, 1-12 = F keys

int keyState = 0;     //Stuff for determining macropad key pressed
int keyStatus = 0;
int keyStatusOld = 0;
int keyWorking = 0;


//---Misc Flags and Variables---//
bool escFlag = false; //Escape condition exists
bool stopFlag = false; //Timer is paused
int loopCount = 0;      //# of loops completed since the timer event was paused //USE TO TEST KEY ACTION DURATION



//-----------FUNCTIONS-------------//

bool TimerHandler0(struct repeating_timer *t) {     //TIMER0 INTERRUPT ROUTINE that scans all IO pins for changes every 20mS. Timer is paused for 70mS after change //
  (void) t;

  //TEST INPUTS//
  txFlagOld = txFlag;
  txFlag = mcp.digitalRead(4);  //SIMULATED TX FLAG INPUT, REPLACE WITH INPUT FROM PC//
  if (txFlagOld != txFlag) {
    txFlagChange = true;
  }
  else {
    txFlagChange = false;
  }
  sliceOld = slice;
  slice = mcp.digitalRead(5);   //SIMULATED SLICE INPUT, REPLACE WITH INPUT FROM PC//
  if (sliceOld != slice) {
    sliceChangeFlag = true;
  }
  else {
    sliceChangeFlag = false;
  }

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
    ITimer0.stopTimer();
    timer0Stop = millis();
    stopFlag = true;
  }

  return true;
}

//--------------//

void neoPixelUpdate() {
  if (keyNum > 0) {
    pixelNum = keyNum - 1;
  }
  if (txFlag == true && macroFlag == false) {
      for(int i=0; i< pixels.numPixels(); i++) {  //All keys red if no macro called & PTT flag = true
        pixels.setPixelColor(i, 255, 0, 0);  
      }
  }
  if (slice == 0 && txFlag == false) {
    for(int i=0; i< pixels.numPixels(); i++) {  //All keys green for Slice A && txflag = false
      pixels.setPixelColor(i, 0, 255, 0);  
    }
  }
  if (slice == 1 && txFlag == false) {
    for(int i=0; i< pixels.numPixels(); i++) {  //All keys green for Slice B & txflag = false
      pixels.setPixelColor(i, 0, 0, 255);  
    }
  }
  if (macroFlag == true){
    pixels.setPixelColor(pixelNum, 255, 0, 0);
  }
  pixels.show();
  sliceOld = slice;
  sliceChangeFlag = false;
}

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

void transmit() {
  Serial.println("TRANSMIT");
}

//---------------//

void flashKey() {
  Serial.println("FLASH KEY");
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
  macroFlag = true;
  neoPixelUpdate();           //Comment out/remove when TX flag from PC is available. TESTING ONLY
/*Keyboard.write(KEY_F13);    //Uncomment when TX flag from PC is available
  delay(200);
  if (txFlag == false) {
    flashKey();
  }
  else {
    transmit();
  }
*/
  delay(500);                 //REMOVE THIS DELAY, TESTING ONLY
  macroFlag = false;
  neoPixelUpdate();
  keyStatus = 0;
}

//----------------//

void wipeKey() {            //F12 (Wipe) key action
  Serial.println("wipeKey() Called");
  Keyboard.write(KEY_F12);
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
  Mouse.begin();
  Keyboard.begin();

  if (!mcp.begin_I2C()) {      //Start I2C: COMMENT OUT IF NO MCP23017 BREAKOUT CONNECTED OR IT WILL HANG
    Serial.println("I2C Error");
    while (1);
  }

  for (uint8_t i=0; i<=7; i++) {    //Configure I2C IO pin as active low inputs: COMMENT OUT IF NO MCP23017
    mcp.pinMode(i, INPUT_PULLUP);
  }

  for (uint8_t i=0; i<=12; i++) {   //Configure macropad pins as active low inputs
    pinMode(i, INPUT_PULLUP);
  }

  pixels.begin();                             //Setup NeoPixel
  pixels.setBrightness(150);                  //Set intial key brightness 0-255
  for(int i=0; i< pixels.numPixels(); i++) {  //All keys green for Slice A, blue for Slice B
      pixels.setPixelColor(i, 0, 255, 0);  
  }
  pixels.show();


  pinMode(PIN_ROTA, INPUT_PULLUP);  //Setup Encoder Pins
  pinMode(PIN_ROTB, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(2), checkPosition, CHANGE); //Setup Encoder interrupts
  attachInterrupt(digitalPinToInterrupt(3), checkPosition, CHANGE);


  ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS * 1000, TimerHandler0);      //Begin Timer0

}

//---------------//

void loop() {

  if (keyFlag == true || mcpFlag == true) {   //If a key is struck
    keyDetect();
  }

  if (sliceChangeFlag == true) {
   neoPixelUpdate();
  }

  encoder.tick();                      // Check the encoder
  int newPos = encoder.getPosition(); //If it changed position
  if (encoder_pos != newPos) {
    switch (encoder.getDirection()){  //Switch on getDirection and increment scroll wheel 1 in correct direction
      case RotaryEncoder::Direction::NOROTATION:
        break;
      case RotaryEncoder::Direction::CLOCKWISE:
        Mouse.move(0,0,-1);
        break;
      case RotaryEncoder::Direction::COUNTERCLOCKWISE:
        Mouse.move(0,0,1);
        break;
    }
    encoder_pos = newPos;
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
