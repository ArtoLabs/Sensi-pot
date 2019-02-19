#include <Wire.h>
#include <EEPROM.h>

const int soilPin = A0;
const int soilDevOn = 7;
const int ldrPin = A1;
const int redPin = 3;
const int greenPin = 5;
const int bluePin = 6;
int maxReading = 670;
int soilSensorValue = 0;
int ldrValue = 0;
char sendData[2];
float per = 0;
int maxReadingEepromAddr = 0;
int i2cAddressEepromAddr = 9;
int newId = 0;

/* Settings */

int threshold = 75;
int minimum = 50;

void setup() {
  int readEeprom = 0;
  EEPROM.get(i2cAddressEepromAddr, readEeprom);
  if (readEeprom > 1) {
    Wire.begin(readEeprom);
    Wire.onRequest(requestEvent);
    EEPROM.put(i2cAddressEepromAddr, 1);
  }
  else {
    Wire.begin(1);
    Wire.onReceive(waitAssignment);
  }
  digitalWrite(soilDevOn, LOW);
  Serial.begin(9600);
  pinMode(soilPin, INPUT);
  pinMode(soilDevOn, OUTPUT);
  pinMode(ldrPin, INPUT);
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  EEPROM.get(maxReadingEepromAddr, readEeprom);
  if (readEeprom > maxReading) {
    maxReading = readEeprom;
  }
  else {
    Serial.println("Please calibrate the soil sensor.");
  }
}

void(* resetFunc) (void) = 0;

void loop() {
  soilHumidity();
  delay(10000);
}

void resetCalibration() {
  EEPROM.put(maxReadingEepromAddr, 670);
}

boolean isLight() {
  ldrValue = analogRead(ldrPin);
  Serial.println(ldrValue);
  if (ldrValue > 40) {
    return true;
  }
  else {
    setColor(0,0,0);
    return false;
  }
}

void soilHumidity() {
  getSensorData();
  per =  soilSensorValue / (maxReading / 100.0); /* Using 10K resistor for pull down */
  /* per = 100; */
  if (per > threshold) {
    float normal = (per - threshold) / threshold;
    int red = 255 - (255 * normal);
    if (red > 255) { red = 255; }
    if (red < 0) { red = 0; }
    if (isLight()) { 
      setColor(red,255,0);
      delay(500);
      setColor(0,0,0);
    }   
  }
  else if (per > 2) {
    /* (val - min) / (max - min) */
    float normal = (per - minimum) / (threshold - minimum);
    int green = 255 * normal;
    if (green > 255) { green = 255; }
    if (green < 0) { green = 0; }
    if (isLight()) {
      setColor(255,green,0);
    }   
  }
  else {
    setColor(255,0,0);
    resetCalibration();
  }
  Serial.print("Humidity: ");
  Serial.print(per);
  Serial.println("%");
}

void getSensorData() {
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

void requestEvent() {
  if (per > 99) {
    per = 99;
  }
  int humidity = per;
  itoa(humidity, sendData, 10);
  Wire.write(sendData[0]);
  Wire.write(sendData[1]);
}

void waitAssignment() {
  if (Wire.available()) {
     while (Wire.available()) {
       newId = Wire.read();
     }
     EEPROM.put(i2cAddressEepromAddr, newId);
     resetFunc();
  }
}
