bool slice;
bool sliceOld;
bool txStatus;
bool txStatusOld;
bool pollSuccessFlag = false;

char incomingByte = 0;
char mode = 0;
char modeOld = 0;

const int bufferSize = 32;
char buf[bufferSize];

bool serialHalt = false;
bool serialWaiting = false;
bool serialErrorFlag = false;

unsigned long wdStart = 0;
unsigned long wdCurrent = 0;
unsigned long wdDiff = 0;

int pollCounter = 0;
int serialErrorCount = 0;

void radioPoll() {
  txStatusOld = txStatus;   //Save states to detect changes
  sliceOld = slice;
  modeOld = mode;
  
  if (pollCounter == 0) {     //Request Transmit status
    Serial.println("ZZTX;");
  }
  else if (pollCounter == 1) {  //If above succeeded, request frequency to determine slice
    Serial.println("ZZFA;");
    pollCounter++;
  } 
  else if (pollCounter == 2) {  //If above succeeded, request current mode of slice A;
    Serial.println("ZZMD;");
    pollCounter++;
  }
  
  wdStart = millis();
  
  while (!Serial.available() && serialHalt == false) {  //Serial timeout. If no response in 5 seconds
    serialWaiting = true;                               //set error flag.
    wdCurrent = millis();
    wdDiff = wdCurrent - wdStart;    
    if (wdDiff >= 1000) {
      serialErrorCount++;
    }
    if (serialErrorCount >= 5) {
      serialHalt = true;              //MAKE IT SO ANY KEY STROKE CLEARS THIS ERROR//      
    }                                 //AND SIGNALS AN ERROR ON PAD
  }
  
  if (Serial.available() && serialWaiting == true) {  //Clear watchdog data
    serialWaiting = false;
    serialErrorFlag = false;
    serialHalt = false;
  }
  
  while (Serial.available()) {          
    incomingByte = Serial.read();
    if (pollCounter == 0) {
      switch (incomingByte) {
        case 'X':                       //Response will be ZZTX#; 0 = RX, 1 = TX
          txStatus = Serial.read();
          Serial.readBytesUntil(';', buf, bufferSize); //Empty incoming serial buffer
          pollCounter++;
          break;
      }
    }
    else if (pollCounter == 1) {
      switch (incomingByte) {
        case 'F':                   //Response will be ZZFA###########; if slice A. # = frequency.
          slice = 0;
          Serial.readBytesUntil(';', buf, bufferSize);
          pollCounter++;
          break;
        case '?':                   //Response will be ?; if slice B due to slice gnostic behavior
          slice = 1;
          Serial.readBytesUntil(';', buf, bufferSize);
          pollCounter++;
          break;
      }
    }
    else if (pollCounter == 2) {  //Response will be ZZMD##; 00 = LSB, 01 = USB, 04 = CW
      switch (incomingByte) {
        case 'D':
          incomingByte = Serial.read();
          mode = Serial.read();
          Serial.readBytesUntil(';', buf, bufferSize);
          pollCounter++;
          break;
      }
    }
    else if (pollCounter == 3) {  //All polls succeeded
      pollSuccessFlag = true;
      pollCounter = 0;
    }
  }  
}
