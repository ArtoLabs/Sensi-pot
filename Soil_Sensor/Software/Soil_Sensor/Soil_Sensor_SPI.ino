#include <Wire.h>
#include <EEPROM.h>
#include "SoundEffects.h"
#include <Time.h>

#define ldrPin A5
#define ThermistorPin A4
#define soilPin A3
#define soilDevOn 2
#define redPin 9
#define greenPin 5
#define bluePin 6
#define maxReadingEepromAddr 0
#define speakerOut 3



const long alarmInterval = 3000; // 3 seconds
unsigned long alarmIntervalTimer = 0;
const long alarmLength = 60000; // 60 seconds
unsigned long alarmLengthTimer = 0;
const long checkSensorInterval = 1000; // 1 second
unsigned long checkSensorTimer = 0;
const long reportSensorInterval = 10000; // 10 seconds
unsigned long reportSensorTimer = 0;

int Vo;
float R1 = 10000;
float logR2, R2, T, Tc, Tf;
float c1 = 1.009249522e-03, c2 = 2.378405444e-04, c3 = 2.019202697e-07;

int maxReading = 100;
int soilSensorValue = 0;
int ldrValue = 0;
float humidity = 0;
float oldHumidity;
float ldrPercentage;

char data[7];
byte pos;
boolean process_it;
boolean sendMyData = false, sendConfirmReset = false, soundAlarm = false, sendAlarmConfirm = false, recalibrated = false;

SoundEffects sound;

/* Settings */

int threshold = 75;
int minimum = 50;

void setup() {
  int readEeprom = 0;
  pinMode(soilPin, INPUT);
  pinMode(soilDevOn, OUTPUT);
  digitalWrite(soilDevOn, LOW);
  pinMode(speakerOut, OUTPUT);
  pinMode(ldrPin, INPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  pinMode(MISO, OUTPUT); // have to send on master in, *slave out*
  SPCR |= _BV(SPE); // turn on SPI in slave mode
  SPCR |= _BV(SPIE); // turn on interrupts
  Serial.begin(115200);
  Serial.println(F("Starting up."));
  EEPROM.get(maxReadingEepromAddr, readEeprom);
  if (readEeprom > maxReading) { maxReading = readEeprom; }
  else { Serial.println(F("Please calibrate the soil sensor.")); }
  sound.oneUp(speakerOut);
}


void loop() {
  unsigned long currentMillis = millis();
  if (soundAlarm) {
    if (currentMillis - alarmIntervalTimer >= alarmInterval){
      alarmIntervalTimer = currentMillis;
      alarm();
    }
  }
  if (currentMillis - checkSensorTimer >= checkSensorInterval){
    checkSensorTimer = currentMillis;
    checkSensors();
    makeSounds();
    if (currentMillis - reportSensorTimer >= reportSensorInterval){
      reportSensorTimer = currentMillis;
      formatData();
      printData();
      signalLED();
    }
    
  }
}


void makeSounds() {
  if ((oldHumidity >= 30) && (humidity < 30)) {sound.whawha(speakerOut);}
}


void printData() {
  Serial.print(F("Humidity: "));
  Serial.print(int(humidity));
  Serial.println(F("%"));
  Serial.print(F("Temperature: ")); 
  Serial.print(Tf);
  Serial.print(F(" F; "));
  Serial.print(Tc);
  Serial.println(F(" C"));
  Serial.print(F("LDR: "));
  Serial.println(ldrPercentage);
  Serial.print(F("data: "));
  Serial.println(data);
}


// SPI interrupt routine
ISR (SPI_STC_vect) {
  byte c = SPDR;
  if (c == 3) {
    Serial.println(F("Request to sound alarm"));
    alarmLengthTimer = millis();
    sendAlarmConfirm = true;
    soundAlarm = true;
  }
  else if (c == 2) {
    Serial.println(F("Request to reset calibration"));
    resetCalibration();
    sendConfirmReset = true;
  }
  else if (c == 1) {
    Serial.println(F("Request for data"));
    sendMyData = true;
    pos = 0;
  }
  else if ((c == 0) && ((sendConfirmReset) || (sendAlarmConfirm))) {
    char s[2] = "1";
    SPDR = s[0];
    sendConfirmReset = false;
    sendAlarmConfirm = false;
  }
  else if ((c == 0) && (sendMyData) && (pos < 6)) {
    SPDR = data[pos];
    pos++;
  }
  else if ((pos < 10) && (pos >= 6) && (sendMyData)){
    SPDR = 0;
    pos++;
  }
  else if (pos >= 10) {
    sendMyData = false;
    pos = 0;
  }
}


void alarm() {
  unsigned long currentMillis = millis();
  if (currentMillis - alarmLengthTimer >= alarmLength){
    soundAlarm = false;
  }
  else {
    setColor(0, 255, 0);
    sound.score(speakerOut);
    setColor(0,0,0);
  }
}


void formatData() {
  memset(data, 0, sizeof(data));
  // Parse humidity into char array
  int hum = int(humidity);
  char humc[3];
  sprintf(humc, "%02d", hum);
  strcat(data, humc);
  // Parse temperature into char array
  int tem = int(Tf);
  char temc[3];
  sprintf(temc, "%02d", tem);
  strcat(data, temc);
  // Parse light sensor into char array
  int ldrPer = ldrPercentage;
  char ldrc[3];
  sprintf(ldrc, "%02d", ldrPer);
  strcat(data, ldrc);
}


void checkSensors() {
  oldHumidity = humidity;
  readSoilSensor();
  humidity =  soilSensorValue / (maxReading / 100.0); /* Using 10K resistor for pull down */
  readThermistor();
  /* per = 100; */
  if (humidity <= 10) {
    if (isLight()) { setColor(255,0,0); }
    if (!(recalibrated)) { resetCalibration(); }
  }
}

void signalLED() {
  if (humidity > threshold) {
    recalibrated = false;
    float normal = (humidity - threshold) / threshold;
    int red = 255 - (255 * normal);
    if (red > 255) { red = 255; }
    if (red < 0) { red = 0; }
    if (isLight()) { 
      setColor(red,255,0);
      delay(500);
      setColor(0,0,0);
    }   
  }
  else if (humidity > 2) {
    /* (val - min) / (max - min) */
    float normal = (humidity - minimum) / (threshold - minimum);
    int green = 255 * normal;
    if (green > 255) { green = 255; }
    if (green < 0) { green = 0; }
    if (isLight()) { setColor(255,green,0); }   
  }
}

boolean isLight() {
    ldrValue = analogRead(ldrPin);
    ldrPercentage = ((float)ldrValue / 1023) * 100;
    if (ldrValue > 250) {
      return true;
    }
    else {
      setColor(0,0,0);
      return false;
    }
}

void readThermistor() {
    Vo = analogRead(ThermistorPin);
    R2 = R1 * (1023.0 / (float)Vo - 1.0);
    logR2 = log(R2);
    T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2));
    Tc = T - 273.15;
    Tf = (Tc * 9.0)/ 5.0 + 32.0;
}

void readSoilSensor() {
    digitalWrite(soilDevOn, HIGH);
    delay(10);
    soilSensorValue = analogRead(soilPin);
    delay(10);
    digitalWrite(soilDevOn, LOW);
    if (soilSensorValue > maxReading) {
      maxReading = soilSensorValue;
      EEPROM.put(maxReadingEepromAddr, maxReading);
      setColor(0,0,255);
      delay(100);
      setColor(0,0,0);
      delay(100);
      setColor(0,0,255);
      delay(100);
      setColor(0,0,0);
      delay(100);
      setColor(0,0,255);
      delay(100);
      setColor(0,0,0);
      delay(100);
    }
}

void setColor(int red, int green, int blue) {
    analogWrite(redPin, red);
    analogWrite(greenPin, green);
    analogWrite(bluePin, blue);
}

void resetCalibration() {
    EEPROM.put(maxReadingEepromAddr, 100);
    maxReading = 100;
    Serial.println(F("Soil sensor recalibrated"));
    recalibrated = true;
    
}
