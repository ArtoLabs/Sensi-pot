#include <SPI.h>
#include <SoftwareSerial.h>
#include <SPIMemory.h>
#include <Time.h>

#define LOG_DATA_SIZE 20 //device id (1 digit) + : + timestamp (10 digits) + : + data (6 digits) + terminating null character
#define PASSWORD_SIZE 25
#define SSID_SIZE 25
#define NUM_OF_DEVICES 8
#define RX 2  // Used to communicate with ESP-01 via serial
#define TX 3
#define MEMORY_SS_PIN 4  // Used to communicate to the Winbond SPI flash memory chip
#define pinA A2 // A B C pins used to control 74hc151 8 to 1 input multiplexer
#define pinB A3
#define pinC A4
#define SETTINGS_MEMORY_ADDRESS 0x1F3000
#define SETTINGS_MEMORY_SIZE 32  // bytes

//   USED TO DEBUG MEMORY PROBLEMS
//   REPORTS AVAILABLE MEMORY LEFT IN THE HEAP
#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else  // __ARM__
extern char *__brkval;
#endif  // __arm__
int freeMemory() {
  char top;
#ifdef __arm__
  return &top - reinterpret_cast<char*>(sbrk(0));
#elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
  return &top - __brkval;
#else  // __arm__
  return __brkval ? &top - __brkval : &top - __malloc_heap_start;
#endif  // __arm__
}

short int devSSPins[8] = {8, 7, 6, 5, 15, 14, 10, 9}; // List of pins used to slave select for the 8 "USB" ports
// Pin 8 -> SS1A -> SS3 -> J4 -> MISO3 -> D3 -> H H L
// Pin 9 -> SS8A -> SS6 -> J9 -> MISO6 -> D7 -> H H H
// Pin 10 -> SS7A -> SS5 -> J8 -> MISO5 -> D6 -> L H H
// Pin 14 -> SS6A -> SS8 -> J11 -> MISO8 -> D5 -> H L H
// Pin 15 -> SS5A -> SS7 -> J10 -> MISO7 -> D4 -> L L H
// Pin 5 -> SS4A -> SS2 -> J3 -> MISO2 -> D0 -> L L L
// Pin 6 -> SS3A -> SS1 -> J1 -> MISO1 -> D1 -> H L L
// Pin 7 -> SS2A -> SS4 -> J6 -> MISO4 -> D2 -> L H L

uint32_t lastKnownAddr;
unsigned long loggingInterval = 300000;  // log every 5 minutes
const byte numChars = 32; 
char receivedESPChars[numChars]; // an array to store the received ESP01 data
char receivedChars[numChars]; // an array to store the received ESP01 data
char logData[LOG_DATA_SIZE]; // an array to store the received SPI data
char password[PASSWORD_SIZE];
char ssid[SSID_SIZE];
char timezone[4];
char loginterval[6];
unsigned long loggingTimer = 0;
boolean pauseLogging = true, newESPData = false, newData = false, waitForTimestamp = false, recvInProgress = false, dataErr = false, disconnected = false, booting = true;
byte spiData;
char spiBuffer[16];

// Function prototypes
void set74HC251(int);
void logDataToMemory();
boolean logTimer();
void printLogDataBuffer();
boolean sendCommand(short int, uint8_t);
void fetchSPIdata(short int);
void recvESPData();
void printChipStats(boolean);
void formatValue(char*, char*, size_t);
void getSettings(boolean);
void saveSettings();
void printDataToLog();
void printLog(boolean);
boolean endOfData(uint32_t);
void recvData();
void eraseLog();
void processESPData();
void processNewData();

SoftwareSerial ESPserial(RX, TX);
SPIFlash flash(MEMORY_SS_PIN);


void setup() {
  for (int i=0; i<NUM_OF_DEVICES; i++) {
    pinMode(devSSPins[i], OUTPUT);
    digitalWrite(devSSPins[i], HIGH);
  }
  Serial.begin(115200);
  Serial.println(F("Initializing ESP serial communication"));
  delay(10);
  ESPserial.begin(9600);
  delay(10);
  pinMode(pinA, OUTPUT);
  pinMode(pinB, OUTPUT);
  pinMode(pinC, OUTPUT);
  set74HC251(8);
  delay(10);
  Serial.println(F("Starting SPI clock at 1Khz"));
  SPI.begin();
  flash.begin();
  flash.setClock(500); // A slow 1Khz to prevent reflections and distortions in the voltage dividers
  delay(10);
  //printChipStats(false);
  Serial.print(F("Free bytes: "));
  Serial.println(freeMemory());
  delay(100);
}


void loop() {
  if (logTimer()){
    ESPserial.print(F("<1>"));
    waitForTimestamp = true;
  }
  if (newESPData) {
    processESPData();
  }
  else if (ESPserial.available()) {
    recvESPData(); 
  }
  if (newData) {
    processNewData();
  }
  else if ((Serial.available()) && (recvInProgress == false)){
    recvData();
  }
}


void set74HC251(int slavePin) {
  if (slavePin == 8) {
    digitalWrite(pinA, HIGH);
    digitalWrite(pinB, HIGH);
    digitalWrite(pinC, LOW);
  }
  if (slavePin == 9) {
    digitalWrite(pinA, HIGH);
    digitalWrite(pinB, HIGH);
    digitalWrite(pinC, HIGH);
  }
  if (slavePin == 10) {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, HIGH);
    digitalWrite(pinC, HIGH);
  }
  if (slavePin == 14) {
    digitalWrite(pinA, HIGH);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, HIGH);
  }
  if (slavePin == 15) {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, HIGH);
  }
  if (slavePin == 5) {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
  }
  if (slavePin == 6) {
    digitalWrite(pinA, HIGH);
    digitalWrite(pinB, LOW);
    digitalWrite(pinC, LOW);
  }
  if (slavePin == 7) {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, HIGH);
    digitalWrite(pinC, LOW);
  }
}


void logDataToMemory() {
  uint32_t Myaddr = 0;
  Myaddr = flash.getAddress(sizeof(char));
  lastKnownAddr = Myaddr;
  flash.writeCharArray(Myaddr, logData, sizeof(logData));
}


boolean logTimer() {
  if ((pauseLogging) || (booting)) { return false; }
  unsigned long currentMillis = millis();
  if (currentMillis - loggingTimer >= loggingInterval){
    loggingTimer = currentMillis;
    return true;
  }
  else { return false; }
}


void printLogDataBuffer() {
  for(short int i=0; i<LOG_DATA_SIZE; i++) { Serial.print(logData[i]); }
  Serial.println();
}


boolean sendCommand(short int slavePin, uint8_t com) {
  boolean worked = false;
  memset(spiBuffer, 0, sizeof(spiBuffer));
  SPI.beginTransaction(SPISettings(1000, MSBFIRST, SPI_MODE0));
  digitalWrite(slavePin, LOW);
  set74HC251(slavePin);
  delay(5);
  SPI.transfer(&com, sizeof(com)); // Send the command 2
  delay(50);
  short int s;
  short int endcounter = 0;
  short int pos = 0;
  while (1) {
    if (pos > 25) { break; } else { pos++; }
    spiData = SPI.transfer(0);
    short int s = char(spiData) - '0';
    if (s == 0) { endcounter++; }
    else { endcounter = 0; }
    if (endcounter > 3) { break; } 
    if (s == 1) { worked = true; break; }
  }
  return worked;
}


void fetchSPIdata(short int slavePin) {
  dataErr = false;
  disconnected = false;
  short int logPos = 0;
  short int s;
  short int endcounter = 0;
  short int discounter = 0;
  Serial.print(F("Polling "));
  Serial.println(slavePin);
  memset(spiBuffer, 0, sizeof(spiBuffer));
  spiBuffer[0] = '\0';
  SPI.beginTransaction(SPISettings(1000, MSBFIRST, SPI_MODE0));
  // enable Slave Select
  digitalWrite(slavePin, LOW);
  delay(100);
  set74HC251(slavePin);
  delay(100);
  SPI.transfer(1); // Send the command 1
  delay(1);
  while (1) {
    spiData = SPI.transfer(0);
    s = (int)spiData;
    if (s == 255) { discounter++; }
    else { discounter = 0; }
    if (discounter > 32) { disconnected = true; break; }
    if (s == 0) { endcounter++; }
    else { endcounter = 0; }
    if (endcounter > 2) { break; } 
    if ((s - 48) >= 0) {
      spiBuffer[logPos] = char(s);
      logPos++;
    }
  }
  if (logPos != 6) { dataErr = true; }
  spiBuffer[7] = '\0';
  // disable Slave Select
  digitalWrite(slavePin, HIGH);
  SPI.endTransaction();
  delay(100);
}


void recvESPData() {
  static boolean recvInProgress = false;
  static byte ndx = 0;
  char startMarker = '<';
  char endMarker = '>';
  char rc;
  while (ESPserial.available() > 0 && newESPData == false) {
    rc = ESPserial.read();
    if (recvInProgress == true) {
      if (rc != endMarker) {
        receivedESPChars[ndx] = rc;
        ndx++;
        if (ndx >= numChars) {ndx = numChars - 1;}
      }
      else {
        receivedESPChars[ndx] = '\0'; // terminate the string
        recvInProgress = false;
        ndx = 0;
        newESPData = true;
      }
    }
    else if (rc == startMarker) {recvInProgress = true;}
  }
}


void printChipStats(boolean toesp) {
  uint32_t JEDEC = flash.getJEDECID();
  uint32_t Cap = flash.getCapacity() / 1048576;
  uint32_t result;
  char sign[6];
  if (lastKnownAddr > 1000) { result = lastKnownAddr / 1000; strcpy(sign, "KB"); }
  else if (lastKnownAddr > 1000000) { result = lastKnownAddr / 1000000; strcpy(sign, "MB"); }
  else { result = lastKnownAddr; strcpy(sign, "Bytes"); }
  if (toesp) {
    ESPserial.print(F("<Mem: "));
    ESPserial.print(result);
    ESPserial.print(F(" "));
    ESPserial.print(sign);
    ESPserial.print(F(" used of "));
    ESPserial.print(Cap);
    ESPserial.print(F(" MB>"));
  }
  else {
    Serial.println(F("Winbond chip stats"));
    Serial.print(F("JEDEC ID: "));
    Serial.println(JEDEC);
    Serial.print(F("Capacity: "));
    Serial.print(Cap);
    Serial.println(F(" MB"));
    Serial.print(F("Used: "));
    Serial.print(result);
    Serial.print(F(" "));
    Serial.print(sign);
  }
}


void formatValue(char *myvalue, char *rc, size_t size_rc) {
  myvalue[0] = '\0';
  char b[size_rc];
  for(short int i=1;i<size_rc;i++) { b[i-1] = rc[i]; }
  b[size_rc] = '\0';
  strcat(myvalue, b);
}


void getSettings(boolean toesp) {
  char buf[SETTINGS_MEMORY_SIZE];
  char pausestate[2];
  buf[0] = '\0';
  flash.readCharArray(SETTINGS_MEMORY_ADDRESS, buf, SETTINGS_MEMORY_SIZE);
  short int s = 0;
  short int t = 0;
  for(short int i=0;i<SETTINGS_MEMORY_SIZE;i++) {
    if (s == 0) { 
      if (buf[i] == ':') { ssid[t] = '\0'; s++; t=0; }
      else { ssid[t] = buf[i]; t++; }
    }
    else if (s == 1) {
      if (buf[i] == ':') { password[t] = '\0'; s++; t=0; }
      else { password[t] = buf[i]; t++; }
    }
    else if (s == 2) {
      if (buf[i] == ':') { timezone[t] = '\0'; s++; t=0; } 
      else { timezone[t] = buf[i]; t++; }
    }
    else if (s == 3) {
      if (buf[i] == ':') { pausestate[t] = '\0'; s++; t=0; } 
      else { pausestate[t] = buf[i]; t++; }
    }
    else if (s == 4) {   // The ESP is all colons, the ATMEGA ends with \0
      if (buf[i] == '\0') { loginterval[t] = '\0'; s++; t=0; } 
      else { loginterval[t] = buf[i]; t++; }
    }
    else { break; }
  }
  if (toesp) { 
    ESPserial.print(F("<S"));
    ESPserial.print(buf);
    ESPserial.print(F(":>"));
  }
  else {
    Serial.print(F("SSID: "));
    Serial.println(ssid);
    Serial.print(F("Password: "));
    Serial.println(password);
    Serial.print(F("Timezone: "));
    Serial.println(timezone);
    Serial.print(F("Pause State: "));
    Serial.println(pausestate);
    Serial.print(F("Log interval: "));
    Serial.println(loginterval); 
  }
  if (pausestate[0] == '1') { pauseLogging = true; }
  else { pauseLogging = false; }
  unsigned long o;
  o = atoi(loginterval);
  loggingInterval = o * 1000UL;
}


void saveSettings() {
  char buf[SETTINGS_MEMORY_SIZE];
  buf[0] = '\0';
  strcat(buf, ssid);
  strcat(buf, ":");
  strcat(buf, password);
  strcat(buf, ":");
  strcat(buf, timezone);
  strcat(buf, ":");
  if (pauseLogging) { strcat(buf, "1"); }
  else { strcat(buf, "0"); }
  strcat(buf, ":");
  strcat(buf, loginterval);
  Serial.print(F("Saving: "));
  Serial.println(buf);
  flash.eraseSector(SETTINGS_MEMORY_ADDRESS);
  flash.writeCharArray(SETTINGS_MEMORY_ADDRESS, buf, SETTINGS_MEMORY_SIZE);
}


void printDataToLog() {
  uint32_t Myaddr = flash.getAddress(sizeof(char));
  Serial.print(F("Saving data to address "));
  Serial.println(Myaddr);
  flash.writeCharArray(Myaddr, logData, sizeof(logData));
}


void printLog(boolean toesp) {
  uint32_t paddr = 0x00; // Start at the very beginning of the chip
  if (!(toesp)) { Serial.println(F("____Log_Data____")); }
  else { ESPserial.print(F("<")); }
  while(1) {
    if (endOfData(paddr)) { break; }
    flash.readCharArray(paddr, logData, sizeof(logData));
    paddr += LOG_DATA_SIZE;
    if (toesp) {
      for(uint32_t i=0; i<sizeof(logData); i++) { ESPserial.print(logData[i]); }
      if (endOfData(paddr)) { ESPserial.print("z"); }
      else { ESPserial.print("n"); }
    }
    else { Serial.println(logData); }
    delay(3);
  }
  if (!(toesp)) { Serial.println(F("____End____")); }
  else { ESPserial.print(">"); }
}


// Find the end of our data log. All the bytes of the chip are set to High 0xFF (255)
// When the chip is erased. Since no data entered by the data logger is likely to
// have for bytes in a row set high, it is considered the end of the data.
boolean endOfData(uint32_t thisaddr) {
  short int s = 0;
  uint8_t thisByte = 0x00;
  uint32_t newaddr = 0x00;
  for (uint32_t i=0; i<8; i++) {
    newaddr = thisaddr + i;
    thisByte = flash.readByte(newaddr);
    // If 6 bytes of 8 bytes are all 0xFF (255) then there is no more data
    if ((thisByte == 0xFF) || (thisByte == 255)){ s++; }
  }
  if (s > 6) { 
    lastKnownAddr = newaddr;
    return true; 
  }
  else { return false; }
}


// This function parses Serial data
void recvData() {
  static byte ndx = 0;
  char startMarker = '<';  // The start and end markers are used to delineate the data
  char endMarker = '>';
  char rc;
  while ((Serial.available() > 0 && newData == false) || (recvInProgress)) {
    if (Serial.available()) {
      rc = Serial.read();
      if (recvInProgress == true) {
        if (rc != endMarker) {  // Wait for the end marker
          receivedChars[ndx] = rc;  // We do this one character at a time
          ndx++;
          if (ndx >= numChars) { ndx = numChars - 1; }
        }
        else {
          receivedChars[ndx] = '\0'; // terminate the string
          recvInProgress = false;
          ndx = 0;
          newData = true;
        }
      }
      else if (rc == startMarker) { recvInProgress = true; } // Keep going
    }
  }
}


void eraseLog() {
  getSettings(false);
  Serial.print(F("Erasing flash chip..."));
  flash.eraseChip();
  Serial.println(F("Erased."));
  delay(100);
  flash.begin();
  delay(100);
  saveSettings();
  ESPserial.println(F("<d>"));
}


void processESPData() {
  if (waitForTimestamp) {
    unsigned long r = atol(receivedESPChars);
    if (r > 1576074461) {
      for(short int i=0;i<8;i++) {
        short int retries = 0;
        while (1) {
          fetchSPIdata(devSSPins[i]); // getting sensor data
          if ((disconnected) || (retries > 2)) {
            Serial.println(F("This device is disconnected."));
            break;
          }
          if (dataErr) { 
            Serial.println(F("Error in data detected! Trying again..."));
            retries++;
            delay(1000);
          }
          else {
            logData[0] = '\0';
            short int currentDev = i + 1;
            itoa(currentDev, logData, 10);  // adding the device id
            strcat(logData, ":");
            strcat(logData, receivedESPChars); // this is the timestamp
            strcat(logData, ":");
            strcat(logData, spiBuffer); // add that data to the string
            printLogDataBuffer();
            logDataToMemory();
            break;
          }
        }
      }
      delay(1000);
    }
    waitForTimestamp = false;
  }
  else if (receivedESPChars[0] == 'c') {
    Serial.println(F("ESP requested calibration reset"));
    short int d = receivedESPChars[1] - '0';
    if (sendCommand(devSSPins[d - 1], 2)) { ESPserial.print(F("<t>"));}
    else { ESPserial.print(F("<f>")); }
  }
  else if (receivedESPChars[0] == 'l') {
    short int d = receivedESPChars[1] - '0';
    if (sendCommand(devSSPins[d-1], 3)) { ESPserial.print(F("<y>")); }
    else { ESPserial.print(F("<z>")); }
  }
  else if (receivedESPChars[0] == 'a') { printLog(true); }
  else if (receivedESPChars[0] == 'w') { printChipStats(true); }
  else if (receivedESPChars[0] == 'e') {        
    eraseLog();
  }
  else if (receivedESPChars[0] == 'r') {
    if (pauseLogging == true) { 
      pauseLogging = false;
      Serial.println(F("Logging will resume"));
      ESPserial.println(F("<u>"));
    }
    else { 
      pauseLogging = true;
      Serial.println(F("Logging has been paused"));
      ESPserial.println(F("<p>"));
    }
    saveSettings();
  }
  else if (receivedESPChars[0] == 's') {
    formatValue(ssid, receivedESPChars, sizeof(receivedESPChars));
    saveSettings();
  }
  else if (receivedESPChars[0] == 'p') {
    formatValue(password, receivedESPChars, sizeof(receivedESPChars));
    saveSettings();
  }
  else if (receivedESPChars[0] == 'u') {
    formatValue(timezone, receivedESPChars, sizeof(receivedESPChars));
    saveSettings();
  }
  else if (receivedESPChars[0] == 'y') {
    formatValue(loginterval, receivedESPChars, sizeof(receivedESPChars));
    saveSettings();
  }
  else if (receivedESPChars[0] == 'z') {
    getSettings(true);
  }
  else if (receivedESPChars[0] == 'h') {
    booting = false;
  }
  else {Serial.println(receivedESPChars);}
  newESPData = false;
}


void processNewData() {
  if (receivedChars[0] == '1') { printLog(false); }
  else if (receivedChars[0] == '2') {
    eraseLog();
  }
  else if (receivedChars[0] == '3') {
    Serial.print(F("Fetching IP address..."));
    ESPserial.print(F("<3>"));
    delay(1);
  }
  else if (receivedChars[0] == '4') {
    if (pauseLogging == true) { 
      pauseLogging = false;
      Serial.print(F("Logging will resume"));
    }
    else { 
      pauseLogging = true;
      Serial.print(F("Logging has been paused"));
    }
    saveSettings();
  }
  else if (receivedChars[0] == 'p') {
    formatValue(password, receivedChars, sizeof(receivedChars));  
    saveSettings();
  }
  else if (receivedChars[0] == 's') {
    formatValue(ssid, receivedChars, sizeof(receivedChars));
    saveSettings();
  }
  else if (receivedChars[0] == 'u') {
    formatValue(timezone, receivedChars, sizeof(receivedChars));
    saveSettings();
  }
  else if (receivedChars[0] == 'y') {
    formatValue(loginterval, receivedChars, sizeof(receivedChars));
    saveSettings();
  }
  else if (receivedChars[0] == 'z') {
    getSettings(false);
  }
  else if (receivedChars[0] == 'b') {
    booting = false;
  }
  else if (receivedChars[0] == 'r') {
    short int d = receivedChars[1] - '0';
    if (sendCommand(devSSPins[d-1], 2)) { Serial.println(F("Calibration reset")); }
    else { Serial.println(F("Calibration could not be rest: device did not respond")); }
  }
  else if (receivedChars[0] == 'a') {
    short int d = receivedChars[1] - '0';
    if (sendCommand(devSSPins[d-1], 3)) { Serial.println(F("Alarm activated")); }
    else { Serial.println(F("Location alarm not activated: device did not respond")); }
  }
  newData = false;
}
