#include <SimpleDHT.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include<SPIMemory.h>
#include <LiquidCrystal.h>
#include <Time.h>
#define RX 4  // Used to communicate with ESP-01 via serial
#define TX 3
#define pinDHT11 2
#define pinLDR A0
#define LOG_DATA_SIZE 18 // Unix timestamp (10 digits) + : (1 digit) humidity (2 digits) + temp (2 digits) + light (2 digits)
#define CS_PIN 5
#define backlight 6
// These are defined for the 74hc595 shift register
#define dataPin 8 // SER orange wire DS Data Pin 14 
#define latchPin 10 // SRCLK blue wire STCP Latch Pin 11 (Output Clock / Storage register clock pin)
#define clockPin 9 // RCLK yellow wire SHCP Clock Pin 12 (Input Clock / Shift register clock pin)
#define SETTINGS_MEMORY_ADDRESS 0x1F3000
#define SETTINGS_MEMORY_SIZE 256  // bytes
#define PASSWORD_SIZE 25
#define SSID_SIZE 25

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

const byte relay1 = 0x80; //0b10000000    HUMIDIFIER      green
const byte relay2 = 0x40; //0b01000000    DE-HUMIDIFIER   red
const byte relay3 = 0x20; //0b00100000    HEATER          yellow
const byte relay4 = 0x10; //0b00010000    AC              green
const byte relay5 = 0x08; //0b00001000    FAN 1
const byte relay6 = 0x04; //0b00000100    FAN 2
const byte relay7 = 0x02; //0b00000010    FAN 3
const byte relay8 = 0x01; //0b00000001    ALARM
const byte allOff = 0x00;
const byte allOn = 0xFF;
uint32_t lastKnownAddr;
char logData[LOG_DATA_SIZE];
char password[PASSWORD_SIZE];
char ssid[SSID_SIZE];
char timezone[4];
char loginterval[8];
char tempTarget[3];
char humTarget[3];
char almsettings[5];
char timer1hourON[3];
char timer1minuteON[3];
char timer2hourON[3];
char timer2minuteON[3];
char timer3hourON[3];
char timer3minuteON[3];
char timer1hourOFF[3];
char timer1minuteOFF[3];
char timer2hourOFF[3];
char timer2minuteOFF[3];
char timer3hourOFF[3];
char timer3minuteOFF[3];
unsigned long loggingInterval = 300000;  // log every 5 minutes
unsigned long sensorInterval = 15000;  // log every 5 seconds
unsigned long loggingTimer = 0;
unsigned long sensorTimer = 0;
byte state;
boolean humidifierActive = false, dehumidifierActive = false, heaterActive = false, acActive = false, fan1Active = false, fan2Active = false, fan3Active = false;
float Tf = 0;
float ldrPercentage = 0;
byte temperature = 0;
byte humidity = 0;
const byte numChars = 32;
char receivedESPChars[numChars]; // an array to store the received data
char receivedChars[numChars]; // an array to store the received ESP01 data
boolean newESPData = false, newData = false, recvInProgress = false, booting = true, waitForTimestamp = false, pauseLogging = false, alarmOn = false, wificonnected = false;
boolean firsttime = true;
boolean humidityHighAlarm = false, humidityLowAlarm = false, tempHighAlarm = false, tempLowAlarm = false;
boolean humidityHighTriggered = false, humidityLowTriggered = false, tempHighTriggered = false, tempLowTriggered = false;
unsigned long humidityHighTimer = 0;
unsigned long humidityLowTimer = 0;
unsigned long tempHighTimer = 0;
unsigned long tempLowTimer = 0;
const long alarmWaitPeriod = 1200000; // wait 20 minutes before triggering alarm
short int targetTemp = 75;
const int degreesOfForgiveness = 2;
short int degreesOfAlarm = 10;
short int targetHum = 55;
const int percentageOfForgiveness = 1;
short int percentageOfAlarm = 10;

// Function prototypes
bool checkLDR();
void activateDevice();
void writeReg(byte);
void formatData();
void activateDeviceTimer();
void requestHumTemp();
void logDataToMemory();
boolean logTimer();
boolean sensorRequestTimer();
void printLogDataBuffer();
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
String makeInfoString (char);


// Pin assignment for the LCD
//const int rs = 19, en = 18, d4 = 17, d5 = 15, d6 = 14, d7 = 6;
//LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
LiquidCrystal lcd(19, 18, 17, 16, 15, 7);
SimpleDHT11 dht11;
SoftwareSerial ESPserial(RX, TX);
SPIFlash flash(CS_PIN);


void setup() {
  pinMode(dataPin, OUTPUT);
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(backlight, OUTPUT);
  writeReg(allOn); // Turn all devices off: shift regisiter is inverting
  activateDevice();
  if (checkLDR()) { digitalWrite(backlight, LOW); }
  else { digitalWrite(backlight, HIGH); }
  ESPserial.begin(9600);
  Serial.begin(115200); // Used for debugging
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print(F("Starting up!"));
  Serial.println("Starting up!");
  delay(1000);
  flash.begin();
  flash.setClock(1000); // A slow 1Khz to prevent reflections and distortions in the voltage dividers 
  Serial.print(F("Free bytes: "));
  Serial.println(freeMemory());
}


// Main loop
void loop() {
  if (checkLDR()) { digitalWrite(backlight, LOW); }
  else { digitalWrite(backlight, HIGH); }
  if (sensorRequestTimer()){
    ESPserial.print(F("<1>"));
    waitForTimestamp = true;
    activateDeviceTimer();
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


bool checkLDR() {
  int sensorValue = analogRead(pinLDR);
  ldrPercentage = ((float)sensorValue / 1023) * 100;
  if (sensorValue < 200) { return true; }
  else { return false; }
}


// Changes the binary value for the shift register
void activateDevice() {
  state = allOff; 
  if (humidifierActive) { state = relay1; }
  if (dehumidifierActive) { state = state xor relay2; }
  if (heaterActive) { state = state xor relay3; }
  if (acActive) { state = state xor relay4; }
  if (fan1Active) { state = state xor relay5; }
  if (fan2Active) { state = state xor relay6; }
  if (fan3Active) { state = state xor relay7; }
  if (!(alarmOn)) { state = state xor relay8; }
  state = allOn - state; // The shift register needs the opposite: On = 0, Off = 1
  writeReg(state);
}


void writeReg(byte value) {
  digitalWrite(latchPin, LOW);    // Pulls the shift register latch low
  for(int i = 0; i < 8; i++){  // Repeat 8 times (once for each bit)
    int bit = value & B10000000; // Use a "bitmask" to select only the eighth bit
    if(bit == 128) {  digitalWrite(dataPin, HIGH); } //if bit 8 is a 1, set our data pin high
    else{ digitalWrite(dataPin, LOW); } //if bit 8 is a 0, set the data pin low
    digitalWrite(clockPin, HIGH);                // The next three lines pulse the clock
    delay(1);
    digitalWrite(clockPin, LOW);
    value = value << 1;          // Move the number up one bit value
  }
  digitalWrite(latchPin, HIGH);  // Pulls the latch high, to display the data
}


void formatData() {
  memset(logData, 0, sizeof(logData));
  logData[0] = '\0';
  strcat(logData, receivedESPChars); // this is the timestamp
  strcat(logData, ":");
  // Parse humidity into char array
  char humc[3];
  sprintf(humc, "%02d", (int)humidity);
  strcat(logData, humc);
  // Parse temperature into char array
  int tem = int(Tf);
  char temc[3];
  sprintf(temc, "%02d", (int)Tf);
  strcat(logData, temc);
  // Parse light sensor into char array
  int ldrPer = ldrPercentage;
  char ldrc[3];
  sprintf(ldrc, "%02d", ldrPer);
  strcat(logData, ldrc);
}


void activateDeviceTimer() {
  lcd.setCursor(0, 1);
  if ((hour() == atoi(timer1hourON)) && (minute() == atoi(timer1minuteON))) { fan1Active = true; }
  if ((hour() == atoi(timer2hourON)) && (minute() == atoi(timer2minuteON))) { fan2Active = true; }
  if ((hour() == atoi(timer3hourON)) && (minute() == atoi(timer3minuteON))) { fan3Active = true; }
  if ((hour() == atoi(timer1hourOFF)) && (minute() == atoi(timer1minuteOFF))) { fan1Active = false; }
  if ((hour() == atoi(timer2hourOFF)) && (minute() == atoi(timer2minuteOFF))) { fan2Active = false; }
  if ((hour() == atoi(timer3hourOFF)) && (minute() == atoi(timer3minuteOFF))) { fan3Active = false; }
  activateDevice();
}


void requestHumTemp() {  // The DHT11 is polled to find the temp and humidity
  int err = SimpleDHTErrSuccess;
  if ((err = dht11.read(pinDHT11, &temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    // If the DHT11 encounters an error print it to the LCD
    lcd.clear();
    lcd.print(F("DHT11 error:    "));
    lcd.setCursor(0, 1);
    lcd.print(err);   
    return;
  }
  Tf = ((int)temperature * 9.0)/ 5.0 + 32.0;  // Convert temperature to fahrenheit
  if ((int)Tf > targetTemp + degreesOfForgiveness) {
    tempLowTriggered = false;
    heaterActive = false;
    acActive = true;
    if (((int)Tf >= targetTemp + degreesOfAlarm) && !(tempHighTriggered)) {
      tempHighTimer = millis();
      tempHighTriggered = true;
      Serial.println(F("High temp alarm timer triggered"));
    }
  }
  if ((int)Tf < targetTemp - degreesOfForgiveness){
    tempHighTriggered = false;
    acActive = false; 
    heaterActive = true;
    if (((int)Tf <= targetTemp - degreesOfAlarm) && !(tempLowTriggered)) { 
      tempLowTimer = millis();
      tempLowTriggered = true;
      Serial.println(F("Low temp alarm timer triggered"));
    }
  }
  if (((int)Tf <= targetTemp + degreesOfForgiveness) && ((int)Tf >= targetTemp - degreesOfForgiveness)) {
    tempHighTriggered = false;
    tempLowTriggered = false;
    if (((int)Tf == targetTemp) {
      acActive = false;
      heaterActive = false;
    }
  }
  if ((int)humidity > targetHum + percentageOfForgiveness) {
    humidityLowTriggered = false;
    humidifierActive = false;
    dehumidifierActive = true;
    if (((int)humidity >= targetHum + percentageOfAlarm) && !(humidityHighTriggered)) {
      humidityHighTimer = millis();
      humidityHighTriggered = true;
      Serial.println(F("High humidity alarm timer triggered"));
    }
  }
  if ((int)humidity < targetHum - percentageOfForgiveness) {
    humidityHighTriggered = false;
    dehumidifierActive = false;
    humidifierActive = true;
    if (((int)humidity <= targetHum - percentageOfAlarm) && !(humidityLowTriggered)) {
      humidityLowTimer = millis();
      humidityLowTriggered = true;
      Serial.println(F("Low humidity alarm timer triggered"));
    }
  }
  if (((int)humidity <= targetHum + percentageOfForgiveness) && ((int)humidity >= targetHum - percentageOfForgiveness))  {
    humidityHighTriggered = false;
    humidityLowTriggered = false;
    if (((int)humidity == targetHum) {
      humidifierActive = false;
      dehumidifierActive = false;
    }
  }
  activateDevice();
  String cT = "";
  for (int i=0; i<11; i++) { cT += receivedESPChars[i]; }
  unsigned long ulCurrentTime = cT.toInt();
  // Set the local system time to the time received by the ESP01
  setTime(ulCurrentTime);
  char hourminute[6];
  sprintf(hourminute,"%02d:%02d", hour(), minute());
  lcd.setCursor(0, 0);
  lcd.print(hourminute);
  lcd.print(F("   "));
  lcd.print((int)Tf);
  lcd.print(F("F  "));
  lcd.print((int)humidity);
  lcd.print(F("%"));
  unsigned long currentMillis2 = millis();
  if ((currentMillis2 - tempHighTimer >= alarmWaitPeriod) && (acActive) && (tempHighAlarm) && (tempHighTriggered)) {
      alarmOn = true;
      lcd.setCursor(0, 1);
      lcd.print(F("Temp is too high "));
  }
  else if ((currentMillis2 - tempLowTimer >= alarmWaitPeriod) && (heaterActive) && (tempLowAlarm) && (tempLowTriggered)) {
      alarmOn = true;
      lcd.setCursor(0, 1);
      lcd.print(F("Temp is too low "));
  }
  else if ((currentMillis2 - humidityHighTimer >= alarmWaitPeriod) && (dehumidifierActive) && (humidityHighAlarm) && (humidityHighTriggered)) {
      alarmOn = true;
      lcd.setCursor(0, 1);
      lcd.print(F("Humid. too high "));
  }
  else if ((currentMillis2 - humidityLowTimer >= alarmWaitPeriod) && (humidifierActive) && (humidityLowAlarm) && (humidityLowTriggered)) {
      alarmOn = true;
      lcd.setCursor(0, 1);
      lcd.print(F("Humid. too low "));
  }
  else {
      alarmOn = false;
  }
  if (logTimer()) {  // If the logging timer is up we log
      formatData();
      printLogDataBuffer();
      logDataToMemory();
      Serial.print(F("Logged: "));
      Serial.print(ulCurrentTime);
      Serial.print(F(" "));
      Serial.print((int)Tf);
      Serial.print(F("F "));
      Serial.print((int)humidity);
      Serial.println(F("%"));
  }
  delay(500);
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
  for(short int i=1;i<size_rc;i++) { 
    b[i-1] = rc[i];
    if (rc[i] == '\0') {
      b[i-1] = '\0';
      break;
    }
  }
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
    else if (s == 4) {
      if (buf[i] == ':') { loginterval[t] = '\0'; s++; t=0; } 
      else { loginterval[t] = buf[i]; t++; }
    }
    else if (s == 5) {   
      if (buf[i] == ':') { tempTarget[t] = '\0'; s++; t=0; } 
      else { tempTarget[t] = buf[i]; t++; }
    }
    else if (s == 6) {   
      if (buf[i] == ':') { humTarget[t] = '\0'; s++; t=0; } 
      else { humTarget[t] = buf[i]; t++; }
    }
    else if (s == 7) {   
      if (buf[i] == ':') { almsettings[t] = '\0'; s++; t=0; } 
      else { almsettings[t] = buf[i]; t++; }
    }
    else if (s == 8) {   
      if (buf[i] == ':') { timer1hourON[t] = '\0'; s++; t=0; } 
      else { timer1hourON[t] = buf[i]; t++; }
    }
    else if (s == 9) {   
      if (buf[i] == ':') { timer1minuteON[t] = '\0'; s++; t=0; } 
      else { timer1minuteON[t] = buf[i]; t++; }
    }
    else if (s == 10) {   
      if (buf[i] == ':') { timer2hourON[t] = '\0'; s++; t=0; } 
      else { timer2hourON[t] = buf[i]; t++; }
    }
    else if (s == 11) {   
      if (buf[i] == ':') { timer2minuteON[t] = '\0'; s++; t=0; } 
      else { timer2minuteON[t] = buf[i]; t++; }
    }
    else if (s == 12) {   
      if (buf[i] == ':') { timer3hourON[t] = '\0'; s++; t=0; } 
      else { timer3hourON[t] = buf[i]; t++; }
    }
    else if (s == 13) {   
      if (buf[i] == ':') { timer3minuteON[t] = '\0'; s++; t=0; } 
      else { timer3minuteON[t] = buf[i]; t++; }
    }
    else if (s == 14) {   
      if (buf[i] == ':') { timer1hourOFF[t] = '\0'; s++; t=0; } 
      else { timer1hourOFF[t] = buf[i]; t++; }
    }
    else if (s == 15) {   
      if (buf[i] == ':') { timer1minuteOFF[t] = '\0'; s++; t=0; } 
      else { timer1minuteOFF[t] = buf[i]; t++; }
    }
    else if (s == 16) {   
      if (buf[i] == ':') { timer2hourOFF[t] = '\0'; s++; t=0; } 
      else { timer2hourOFF[t] = buf[i]; t++; }
    }
    else if (s == 17) {   
      if (buf[i] == ':') { timer2minuteOFF[t] = '\0'; s++; t=0; } 
      else { timer2minuteOFF[t] = buf[i]; t++; }
    }
    else if (s == 18) {   
      if (buf[i] == ':') { timer3hourOFF[t] = '\0'; s++; t=0; } 
      else { timer3hourOFF[t] = buf[i]; t++; }
    }
    else if (s == 19) {   // The ESP is all colons, the ATMEGA \0
      if (buf[i] == '\0') { timer3minuteOFF[t] = '\0'; s++; t=0; } 
      else { timer3minuteOFF[t] = buf[i]; t++; }
    }
    else { break; }
  }
  if (toesp) { 
    ESPserial.print(F("<S"));
    ESPserial.print(buf);
    ESPserial.print(F(">"));
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
    Serial.print(F("Target temp: "));
    Serial.println(tempTarget); 
    Serial.print(F("Target humidity: "));
    Serial.println(humTarget); 
    Serial.print(F("Alarm settings: "));
    Serial.println(almsettings);
    Serial.print(F("Device 5 Timer On: "));
    Serial.print(timer1hourON);
    Serial.print(F(":"));
    Serial.print(timer1minuteON);
    Serial.print(F(" Off: "));
    Serial.print(timer1hourOFF);
    Serial.print(F(":"));
    Serial.println(timer1minuteOFF);
    Serial.print(F("Device 6 Timer On: "));
    Serial.print(timer2hourON);
    Serial.print(F(":"));
    Serial.print(timer2minuteON);
    Serial.print(F(" Off: "));
    Serial.print(timer2hourOFF);
    Serial.print(F(":"));
    Serial.println(timer2minuteOFF);
    Serial.print(F("Device 7 Timer On: "));
    Serial.print(timer3hourON);
    Serial.print(F(":"));
    Serial.print(timer3minuteON);
    Serial.print(F(" Off: "));
    Serial.print(timer3hourOFF);
    Serial.print(F(":"));
    Serial.print(timer3minuteOFF);
  }
  targetTemp = atoi(tempTarget);
  targetHum = atoi(humTarget);
  if (almsettings[0] == '1') { tempHighAlarm = true; }
  else { tempHighAlarm = false; }
  if (almsettings[1] == '1') { tempLowAlarm = true; }
  else { tempLowAlarm = false; }
  if (almsettings[2] == '1') { humidityHighAlarm = true; }
  else { humidityHighAlarm = false; }
  if (almsettings[3] == '1') { humidityLowAlarm = true; }
  else { humidityLowAlarm = false; }      
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
  strcat(buf, ":");
  strcat(buf, tempTarget);
  strcat(buf, ":");
  strcat(buf, humTarget);
  strcat(buf, ":");
  strcat(buf, almsettings);
  strcat(buf, ":");
  strcat(buf, timer1hourON);
  strcat(buf, ":");
  strcat(buf, timer1minuteON);
  strcat(buf, ":");
  strcat(buf, timer2hourON);
  strcat(buf, ":");
  strcat(buf, timer2minuteON);
  strcat(buf, ":");
  strcat(buf, timer3hourON);
  strcat(buf, ":");
  strcat(buf, timer3minuteON);
  strcat(buf, ":");
  strcat(buf, timer1hourOFF);
  strcat(buf, ":");
  strcat(buf, timer1minuteOFF);
  strcat(buf, ":");
  strcat(buf, timer2hourOFF);
  strcat(buf, ":");
  strcat(buf, timer2minuteOFF);
  strcat(buf, ":");
  strcat(buf, timer3hourOFF);
  strcat(buf, ":");
  strcat(buf, timer3minuteOFF);
  Serial.print(F("Saving: "));
  Serial.println(buf);
  flash.eraseSector(SETTINGS_MEMORY_ADDRESS);
  flash.writeCharArray(SETTINGS_MEMORY_ADDRESS, buf, SETTINGS_MEMORY_SIZE);
}


boolean logTimer() {
  if ((pauseLogging) || (booting) || !(wificonnected)) { return false; }
  unsigned long currentMillis = millis();
  if ((currentMillis - loggingTimer >= loggingInterval) || (firsttime)) {
    loggingTimer = currentMillis;
    firsttime = false;
    return true;
  }
  else { return false; }
}


boolean sensorRequestTimer() {
  if (booting) { return false; }
  unsigned long currentMillis = millis();
  if (currentMillis - sensorTimer >= sensorInterval){
    sensorTimer = currentMillis;
    return true;
  }
  else { return false; }
}


void logDataToMemory() {
  uint32_t Myaddr = 0;
  Myaddr = flash.getAddress(sizeof(char));
  lastKnownAddr = Myaddr;
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


void printLogDataBuffer() {
  for(short int i=0; i<LOG_DATA_SIZE; i++) { Serial.print(logData[i]); }
  Serial.println();
}


boolean endOfData(uint32_t thisaddr) {
    int s = 0;
    uint8_t thisByte = 0x00;
    uint32_t newaddr = 0x00;
    for (uint32_t i=0; i<8; i++) {
      newaddr = thisaddr + i;
      thisByte = flash.readByte(newaddr);
      if ((thisByte == 0xFF) || (thisByte == 255)){
        s++; // If 6 bytes of 8 bytes are all 0xFF (255) then there is no more data
      }
    }
    if (s > 6) {
      return true;
    }
    else {
      return false;
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


// Prepares a string to be sent to the ESP01 out of a binary (8 characters)
String makeInfoString(char commandLetter[]) { // Command letter is a code that separates between 
                                                // polling a manual device state change
  byte truestate = ~state;
  String sendit = "<";
  sendit += commandLetter;
  for(int i = 0; i < 8; i++){  //Will repeat 8 times (once for each bit)
    int bit = truestate & B10000000; //We use a "bitmask" to select only the eighth bit in our number 
    if(bit == 128) { sendit += '1'; } 
    else{ sendit += '0'; } 
    truestate = truestate << 1;          //we move our number up one bit value
  }
  sendit += ">";
  return sendit;
}


void formatTimer(char *timerHour, char *timerMinute, char *buff) {
  short int p = 0;
  for(short int i=0; i<3; i++) {
    if (buff[i+2] == ':') { timerHour[i] = '\0'; break; }
    else { timerHour[i] = buff[i+2]; p++; }
  }
  for(short int i=0; i<3; i++) {
    if (buff[i+3+p] == '\0') { timerMinute[i] = '\0'; break; }
    else { timerMinute[i] = buff[i+3+p]; }
  }
}


void processESPData() {
  if (waitForTimestamp) {
    unsigned long r = atol(receivedESPChars);
    if (r > 1576074461) {
      requestHumTemp();  // Fetch data from DHT11 and log it
      String info = makeInfoString("s"); // Update ESP01 
      ESPserial.print(info);
      wificonnected = true;
    }
    else {
      wificonnected = false;
      lcd.setCursor(0, 0);
      lcd.print(F("Wifi not connect"));
      lcd.setCursor(0, 1);
      lcd.print(F("Use Sensipot AP "));
    }
    waitForTimestamp = false;
  }
  else if (receivedESPChars[0] == '4') {  // Set the various alarms. 
    if (receivedESPChars[2] == '1') { tempHighAlarm = true; }
    else { tempHighAlarm = false; }
    almsettings[0] = receivedESPChars[2];
    if (receivedESPChars[3] == '1') { tempLowAlarm = true; }
    else { tempLowAlarm = false; }
    almsettings[1] = receivedESPChars[3];
    if (receivedESPChars[4] == '1') { humidityHighAlarm = true; }
    else { humidityHighAlarm = false; }
    almsettings[2] = receivedESPChars[4];
    if (receivedESPChars[5] == '1') { humidityLowAlarm = true; }
    else { humidityLowAlarm = false; }
    almsettings[3] = receivedESPChars[5];
    almsettings[4] = '\0';
    ESPserial.print("<a>");
    saveSettings();
    getSettings(true);
  }
  else if (receivedESPChars[0] == 'T') {
    tempTarget[0] = receivedESPChars[1];
    tempTarget[1] = receivedESPChars[2];
    tempTarget[2] = '\0';
    targetTemp = atoi(tempTarget);
    saveSettings();
    getSettings(true);
  }
  else if (receivedESPChars[0] == 'H') {
    humTarget[0] = receivedESPChars[1];
    humTarget[1] = receivedESPChars[2];
    humTarget[2] = '\0';
    targetHum = atoi(humTarget);
    saveSettings();
    getSettings(true);
  }
  else if (receivedESPChars[0] == 'R') {
    if (receivedESPChars[1] == '1') {
      formatTimer(timer1hourON, timer1minuteON, receivedESPChars);
    }
    if (receivedESPChars[1] == '2') {
      formatTimer(timer2hourON, timer2minuteON, receivedESPChars);
    }
    if (receivedESPChars[1] == '3') {
      formatTimer(timer3hourON, timer3minuteON, receivedESPChars);
    }
    if (receivedESPChars[1] == '4') {
      formatTimer(timer1hourOFF, timer1minuteOFF, receivedESPChars);
    }
    if (receivedESPChars[1] == '5') {
      formatTimer(timer2hourOFF, timer2minuteOFF, receivedESPChars);
    }
    if (receivedESPChars[1] == '6') {
      formatTimer(timer3hourOFF, timer3minuteOFF, receivedESPChars);
    }
    saveSettings();
    ESPserial.println(F("<a>"));
    getSettings(true);
  }
  else if (receivedESPChars[0] == 'D') {  // The "D" code sent from the ESP01 is given as
                                          // a command to change the on/off state of a device
    if (receivedESPChars[1] == '1') {
        humidifierActive = !humidifierActive;
    }
    else if (receivedESPChars[1] == '2') {
        dehumidifierActive = !dehumidifierActive;
    }
    else if (receivedESPChars[1] == '3') {
        heaterActive = !heaterActive;
    }
    else if (receivedESPChars[1] == '4') {
        acActive = !acActive;
    }
    else if (receivedESPChars[1] == '5') {
        fan1Active = !fan1Active;
    }
    else if (receivedESPChars[1] == '6') {
        fan2Active = !fan2Active;
    }
    else if (receivedESPChars[1] == '7') {
        fan3Active = !fan3Active;
    }
    else if (receivedESPChars[1] == '8') {
        alarmOn = !alarmOn;
    }
    activateDevice();
    String info = makeInfoString("f");
    ESPserial.println(info);
  }
  else if (receivedESPChars[0] == 'a') { printLog(true); }
  else if (receivedESPChars[0] == 'w') { printChipStats(true); }
  else if (receivedESPChars[0] == 'e') { eraseLog(); }
  else if (receivedESPChars[0] == 'r') {
    if (pauseLogging == true) { 
      pauseLogging = false;
      Serial.println(F("Logging will resume"));
      lcd.setCursor(0, 1);
      lcd.print(F("Logging resumed "));
      ESPserial.println(F("<u>"));
    }
    else { 
      pauseLogging = true;
      Serial.println(F("Logging has been paused"));
      lcd.setCursor(0, 1);
      lcd.print(F("Logging paused  "));
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
  else if (receivedESPChars[0] == '*') {
    Serial.println(receivedESPChars);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(receivedESPChars);  // If we don't know what the command is print it out
  }

  else {
    Serial.println(receivedESPChars);
  }
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
  else if (receivedChars[0] == '5') {  // Set the various alarms. 
    if (receivedChars[1] == '1') { tempHighAlarm = true; }
    else { tempHighAlarm = false; }
    almsettings[0] = receivedChars[1];
    if (receivedChars[2] == '1') { tempLowAlarm = true; }
    else { tempLowAlarm = false; }
    almsettings[1] = receivedChars[2];
    if (receivedChars[3] == '1') { humidityHighAlarm = true; }
    else { humidityHighAlarm = false; }
    almsettings[2] = receivedChars[4];
    if (receivedChars[4] == '1') { humidityLowAlarm = true; }
    else { humidityLowAlarm = false; }
    almsettings[3] = receivedChars[4];
    almsettings[4] = '\0';
    saveSettings();
  }
  else if (receivedChars[0] == 'T') {
    tempTarget[0] = receivedChars[1];
    tempTarget[1] = receivedChars[2];
    tempTarget[2] = '\0';
    targetTemp = atoi(tempTarget);
    Serial.println(humTarget);
    saveSettings();
  }
  else if (receivedChars[0] == 'H') {
    humTarget[0] = receivedChars[1];
    humTarget[1] = receivedChars[2];
    humTarget[2] = '\0';
    targetHum = atoi(humTarget);
    Serial.println(humTarget);
    saveSettings();
  }
  else if (receivedChars[0] == 'R') {
    if (receivedChars[1] == '1') {
      formatTimer(timer1hourON, timer1minuteON, receivedChars);
    }
    if (receivedChars[1] == '2') {
      formatTimer(timer2hourON, timer2minuteON, receivedChars);
    }
    if (receivedChars[1] == '3') {
      formatTimer(timer3hourON, timer3minuteON, receivedChars);
    }
    if (receivedChars[1] == '4') {
      formatTimer(timer1hourOFF, timer1minuteOFF, receivedChars);
    }
    if (receivedChars[1] == '5') {
      formatTimer(timer2hourOFF, timer2minuteOFF, receivedChars);
    }
    if (receivedChars[1] == '6') {
      formatTimer(timer3hourOFF, timer3minuteOFF, receivedChars);
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
  newData = false;
}
