#include <SimpleDHT.h>
#include <SoftwareSerial.h>
#include <SD.h>
#include <SPI.h>
#include <LiquidCrystal.h>
#include <Time.h>
#define RX 4  // Used to communicate with ESP-01 via serial
#define TX 3
#define ledPin 7
#define pinDHT11 2
#define pinCS 5 // CS Pin for SD card. This is the only pin that can be changed. The miso must go to miso, mosi to mosi, sck to sck.
#define dataPin 8 // SER orange wire DS Data Pin 14 
#define latchPin 10 // SRCLK blue wire STCP Latch Pin 11 (Output Clock / Storage register clock pin)
#define clockPin 9 // RCLK yellow wire SHCP Clock Pin 12 (Input Clock / Shift register clock pin)
const byte relay1 = 0x80; //0b10000000    HUMIDIFIER      green
const byte relay2 = 0x40; //0b01000000    DE-HUMIDIFIER   red
const byte relay3 = 0x20; //0b00100000    HEATER          yellow
const byte relay4 = 0x10; //0b00010000    AC              green
const byte relay5 = 0x08; //0b00001000    FAN 1
const byte relay6 = 0x04; //0b00000100    FAN 2
const byte relay7 = 0x02; //0b00000010    FAN 3
const byte relay8 = 0x01; //0b00000001    FAN 4
const byte allOff = 0x00;
const byte allOn = 0xFF;
byte state;
boolean humidifierActive = false;
boolean dehumidifierActive = false;
boolean heaterActive = false;
boolean acActive = false;
float Tf = 0;
byte temperature = 0;
byte humidity = 0;
const byte numChars = 32;
//char receivedChars[numChars]; // an array to store the received data
//boolean newData = false;
char receivedESPChars[numChars]; // an array to store the received data
boolean newESPData = false;
char SDChars[numChars]; // an array to store the received data
boolean SDData = false;
boolean waitForTime = false;
boolean pollSlave = false;
boolean getIP = false;
boolean waitForIP = true;
char currentTime[11];
const long interval = 2000;
const long slaveInterval = 300000;
unsigned long slaveTimer = 0;
boolean pausePolling = false;
const int rs = 19, en = 18, d4 = 17, d5 = 15, d6 = 14, d7 = 6;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
SimpleDHT11 dht11;
File myFile;
SoftwareSerial ESPserial(RX, TX);


void activateDevice () {
  state = allOff;
  if (humidifierActive) { state = relay1; }
  if (dehumidifierActive) { state = state xor relay2; }
  if (heaterActive) { state = state xor relay3; }
  if (acActive) { state = state xor relay4; }
  state = allOn - state;
  writeReg(state);
}


void writeReg(byte value) {
  digitalWrite(latchPin, LOW);    //Pulls the chips latch low
  for(int i = 0; i < 8; i++){  //Will repeat 8 times (once for each bit)
    int bit = value & B10000000; //We use a "bitmask" to select only the eighth bit in our number 
    if(bit == 128) { 
      digitalWrite(dataPin, HIGH);
    } //if bit 8 is a 1, set our data pin high
    else{ 
      digitalWrite(dataPin, LOW);
    } //if bit 8 is a 0, set the data pin low
    digitalWrite(clockPin, HIGH);                //the next three lines pulse the clock
    delay(1);
    digitalWrite(clockPin, LOW);
    value = value << 1;          //we move our number up one bit value
  }
  digitalWrite(latchPin, HIGH);  //pulls the latch high, to display our data
}


void requestHumTemp() {
    digitalWrite(ledPin, HIGH);
    int err = SimpleDHTErrSuccess;
    if ((err = dht11.read(pinDHT11, &temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
      /* Serial.print("Read DHT11 failed, err="); 
      Serial.println(err); */

      lcd.setCursor(0, 0);
      lcd.print("DHT11 error:    ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      lcd.setCursor(0, 1);
      lcd.print(err);   
      return;
    }

    Tf = ((int)temperature * 9.0)/ 5.0 + 32.0; 
    myFile = SD.open("data.txt", FILE_WRITE);
    String value;
    if (Tf > 73) {
      if (heaterActive) {
          heaterActive = false;
          //value =  ":Heater deactivated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("Heat deactivated");
          if (myFile) {
            myFile.println(value);
          }
      }
      if ((Tf > 76) && (!acActive)){ 
          acActive = true;
          //value =  ":AC activated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("AC activated    ");
          if (myFile) {
            myFile.println(value);
          }
      }
    }
    else if (Tf < 74) {
      if (acActive) {
          acActive = false;
          //value =  ":AC deactivated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("AC deactivated  ");          
          if (myFile) {
            myFile.println(value);
          }        
      } 
      if ((Tf < 70) && (!heaterActive)){
          heaterActive = true;
          //value =  ":Heater activated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("Heat activated  ");
          if (myFile) {
            myFile.println(value);
          }
      }
    }
    if ((int)humidity > 55) {
      if (humidifierActive) {
          humidifierActive = false;
          //value =  ":Humidifier deactivated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("Humid. deactiva.");
          if (myFile) {
            myFile.println(value);
          }
      }
      if (((int)humidity > 58) && (!dehumidifierActive)){
          dehumidifierActive = true;
          //value =  ":Dehumidifier activated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("Dehumid. activa.");
          if (myFile) {
            myFile.println(value);
          }
      }
    }
    if ((int)humidity < 56) {
      if (dehumidifierActive) {
          dehumidifierActive = false;
          //value =  ":Dehumidifier deactivated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("Dehumid. deacti.");
          if (myFile) {
            myFile.println(value);
          }
      }
      if (((int)humidity < 52) && (!humidifierActive)){
          humidifierActive = true;
          //value =  ":Humidifier activated";
          //Serial.println(value);
          lcd.setCursor(0, 1);
          lcd.print("Humid. activated");
          if (myFile) {
            myFile.println(value);
          }
      }
    }
    activateDevice();
    String cT = "";
    for (int i=0; i<11; i++) {
      cT += currentTime[i];
    }
    long l = cT.toInt();
    setTime(l);
    char buf[6];
    sprintf(buf,"%02d:%02d", hour(), minute());
    String t = "";
    value = t + buf + "   " + (int)Tf + "F  " + (int)humidity + "%";

    lcd.setCursor(0, 0);
    lcd.print(value);
    if (myFile) {
        //Serial.println("Logging: " + value);
        myFile.println(value);
        myFile.close();
    }
    else {
        //Serial.println("Error reading SD card.");
    }
    delay(500);
    digitalWrite(ledPin, LOW);
}

void getSDdata () {
    myFile = SD.open("/data.txt", FILE_READ);
    if(myFile) {
        ESPserial.print("<");
        while (myFile.available()) {
          ESPserial.write(myFile.read());
          //Serial.write(myFile.read());
        }
        ESPserial.println(">");
        myFile.close();
    }
    else {
      lcd.setCursor(0, 1);
      lcd.print("Error reading SD"); 
    }
}
void deleteSDdata () {
    SD.remove("/data.txt");
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


String makeInfoString (char commandLetter[]) {
    int t = int(Tf);
    int h = int(humidity);
    byte truestate = ~state;
    String sendit = "<";
    sendit += commandLetter;
    sendit += t;
    sendit += h;
    for(int i = 0; i < 8; i++){  //Will repeat 8 times (once for each bit)
      int bit = truestate & B10000000; //We use a "bitmask" to select only the eighth bit in our number 
      if(bit == 128) { sendit += '1'; } 
      else{ sendit += '0'; } 
      truestate = truestate << 1;          //we move our number up one bit value
    }
    sendit += ">";
    return sendit;
}


void setup() {
    Serial.begin(9600);
    pinMode(pinCS, OUTPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(dataPin, OUTPUT);
    pinMode(latchPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    
    writeReg(allOn);
    lcd.begin(16, 2);
    lcd.setCursor(0, 0);
    lcd.print("Starting up!");
    if (SD.begin()) {
       lcd.setCursor(0, 0);
       lcd.print("SD card ready  ");
       delay(1000);
    }
    else {
       lcd.setCursor(0, 0);
       lcd.print("Error:          ");
       lcd.setCursor(0, 1);
       lcd.print("No SD card found");
       delay(1000);
    }
    ESPserial.begin(9600);
    delay(30);
    getIP = true;
    lcd.setCursor(0, 0);
    lcd.print("Getting IP      ");

}

void loop() {
    if (getIP) {
      unsigned long currentMillis = millis();
      ESPserial.print("<3>");
      delay(2000);
      while(!(ESPserial.available())) { 
        unsigned long currentMillisPast = millis();
        if ((currentMillisPast - currentMillis) > 10000) {
          break;
          lcd.setCursor(0, 0);
          lcd.print("Could not get IP");
        }
      }
      getIP = false;
      waitForIP = true;
    }
    if (ESPserial.available()) {
      recvESPData(); 
    }
    if (newESPData) {
        if (pollSlave) {
            for (int i=0;i<10;i++) {
              currentTime[i] = receivedESPChars[i];
            }
            currentTime[11] = 0;
            requestHumTemp();
            pollSlave = false;
            String info = makeInfoString("s");
            Serial.println(info);
            ESPserial.print(info);
        }
        else if (waitForIP) {
            lcd.clear();
            lcd.setCursor(0, 0);
            for (int i=0;i<sizeof(receivedESPChars);i++) {
              lcd.print(receivedESPChars[i]);
            }
            waitForIP = false;
        }
        else if (receivedESPChars[0] == '1') {getSDdata();}
        else if (receivedESPChars[0] == '2') {        
            lcd.setCursor(0, 1);
            lcd.print("Deleted all data");
            deleteSDdata();
            ESPserial.print("<deleted>");
        }
        else if (receivedESPChars[0] == '5') {
            if (pausePolling == true) { 
              pausePolling = false;
              lcd.setCursor(0, 1);
              lcd.print("Logging resumed ");
              ESPserial.println("<u>");
            }
            else { 
              pausePolling = true;
              lcd.setCursor(0, 1);
              lcd.print("Logging paused  ");
              ESPserial.println("<p>");
            }
        }
        else if (receivedESPChars[0] == 'D') {        

          if (receivedESPChars[1] == '1') {
              if (humidifierActive) { humidifierActive = false; }
              else { humidifierActive = true; }
          }
          else if (receivedESPChars[1] == '2') {
              if (dehumidifierActive) { dehumidifierActive = false; }
              else { dehumidifierActive = true; }
          }
          else if (receivedESPChars[1] == '3') {
              if (heaterActive) { heaterActive = false; }
              else { heaterActive = true; }
          }
          else if (receivedESPChars[1] == '4') {
              if (acActive) { acActive = false; }
              else { acActive = true; }
          }
          activateDevice();
          String info = makeInfoString("d");
          Serial.println(info);
          ESPserial.println(info);
        }
        else {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(receivedESPChars);
        }
        newESPData = false;
    }
    unsigned long currentMillis = millis();
    if ((currentMillis - slaveTimer >= slaveInterval) && (!pausePolling)){
      digitalWrite(ledPin, HIGH);
        slaveTimer = currentMillis;
        ESPserial.print("<1>");
        pollSlave = true;
      delay(10);
      digitalWrite(ledPin, LOW); 
    }
    /*
    if (Serial.available()){recvData();}
    if (newData) {
        if (receivedChars[0] == '1') {getSDdata();}
        else if (receivedChars[0] == '2') {
            ESPserial.print("<1>");
            waitForTime = true; 
        }
        else if (receivedChars[0] == '3') {
            Serial.println(currentTime);
        }
        else if (receivedChars[0] == '4') {        
            /* Serial.println("Deleting SD data"); 
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Deleting all data");
            deleteSDdata();
        }
        else if (receivedChars[0] == '5') {
            if (pausePolling == true) { 
              pausePolling = false;
              /* Serial.println("Logging will resume"); 
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print("Logging will resume");
              ESPserial.println("<u>");
            }
            else { 
              pausePolling = true;
              /* Serial.println("Logging has been paused"); 
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print("Logging has been paused");
              ESPserial.println("<p>");
            }
        }
        
        else { Serial.println(receivedChars);}
        newData = false;
    }
    */

}


/*
void recvData() {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    char startMarker = '<';
    char endMarker = '>';
    char rc;
    while (Serial.available() && newData == false) {
        rc = Serial.read();
        if (recvInProgress == true) {
            if (rc != endMarker) {
                receivedChars[ndx] = rc;
                ndx++;
                if (ndx >= numChars) {ndx = numChars - 1;}
            }
            else {
                receivedChars[ndx] = '\0'; // terminate the string
                recvInProgress = false;
                ndx = 0;
                newData = true;
            }
        }
        else if (rc == startMarker) {recvInProgress = true; }
    }
} */
