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
bool humidifierActive = false;
bool dehumidifierActive = false;
bool heaterActive = false;
bool acActive = false;
float Tf = 0;
byte temperature = 0;
byte humidity = 0;
const byte numChars = 32;
char receivedESPChars[numChars]; // an array to store the received data
bool newESPData = false;
char SDChars[numChars]; // an array to store the received data
bool SDData = false;
bool waitForTime = false;
bool pollSlave = false;
bool getIP = false;
bool waitForIP = true;
char currentTime[11];
bool pollFirstTime = true;    // Don't wait 5 minutes to check the first time.
unsigned long slaveTimer = 0;
unsigned long logTimer = 0;
bool pausePolling = false;

// User defined variables
const long slaveInterval = 15000;  // Check every 15 seconds
const long loggingInterval = 300000;  // log every 5 minutes
const int targetTemp = 75;
const int degreesOfForgiveness = 2;
const int targetHumidity = 55;
const int percentageOfForgiveness = 1;

// Pin assignment for the LCD
//const int rs = 19, en = 18, d4 = 17, d5 = 15, d6 = 14, d7 = 6;
//LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
LiquidCrystal lcd(19, 18, 17, 15, 14, 6);

SimpleDHT11 dht11;
File myFile;
SoftwareSerial ESPserial(RX, TX);

// Changes the binary value for the shift register
void activateDevice () {
  state = allOff; 
  if (humidifierActive) { state = relay1; }
  if (dehumidifierActive) { state = state xor relay2; }
  if (heaterActive) { state = state xor relay3; }
  if (acActive) { state = state xor relay4; }
  state = allOn - state; // The shift register needs the opposite: On = 0, Off = 1
  writeReg(state);
}


void writeReg(byte value) {
  digitalWrite(latchPin, LOW);    // Pulls the shift register latch low
  for(int i = 0; i < 8; i++){  // Repeat 8 times (once for each bit)
    int bit = value & B10000000; // Use a "bitmask" to select only the eighth bit
    if(bit == 128) { 
      digitalWrite(dataPin, HIGH);
    } //if bit 8 is a 1, set our data pin high
    else{ 
      digitalWrite(dataPin, LOW);
    } //if bit 8 is a 0, set the data pin low
    digitalWrite(clockPin, HIGH);                // The next three lines pulse the clock
    delay(1);
    digitalWrite(clockPin, LOW);
    value = value << 1;          // Move the number up one bit value
  }
  digitalWrite(latchPin, HIGH);  // Pulls the latch high, to display the data
}


void requestHumTemp() {  // The DHT11 is polled to find the temp and humidity
    digitalWrite(ledPin, HIGH); 
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
    
    
    String cT = "";
    for (int i=0; i<11; i++) {
      cT += currentTime[i];
    }
    long l = cT.toInt();
    setTime(l);
    char hourminute[6];
    sprintf(hourminute,"%02d:%02d", hour(), minute());
    char logdevice[20];
    if (Tf > targetTemp) {  //  If the temperature is greater than 75
      if (heaterActive) {
          heaterActive = false;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":HE:0");
          lcd.setCursor(0, 1);
          lcd.print(F("Heat deactivated"));
          printDevActivityToLog(logdevice);
      }
      if ((Tf > targetTemp + degreesOfForgiveness) && (!acActive)){  //  If the temperature is greater than 77
          acActive = true;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":AC:1");
          lcd.setCursor(0, 1);
          lcd.print(F("AC activated    "));
          printDevActivityToLog(logdevice);
      }
    }
    else if (Tf < targetTemp) { //  If the temperature is less than 75
      if (acActive) {
          acActive = false;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":AC:0");
          lcd.setCursor(0, 1);
          lcd.print(F("AC deactivated  "));          
          printDevActivityToLog(logdevice);    
      } 
      if ((Tf < targetTemp - degreesOfForgiveness) && (!heaterActive)){ //  If the temperature is less than 73
          heaterActive = true;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":HE:1");
          lcd.setCursor(0, 1);
          lcd.print(F("Heat activated  "));
          printDevActivityToLog(logdevice);
      }
    }
    if ((int)humidity > targetHumidity) { //  If the humidity is greater than 55
      if (humidifierActive) {
          humidifierActive = false;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":HU:0");
          lcd.setCursor(0, 1);
          lcd.print(F("Humid. deactiva."));
          printDevActivityToLog(logdevice);
      }
      if (((int)humidity > targetHumidity + percentageOfForgiveness) && (!dehumidifierActive)){ //  If the humidity is greater than 56
          dehumidifierActive = true;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":DH:1");
          lcd.setCursor(0, 1);
          lcd.print(F("Dehumid. activa."));
          printDevActivityToLog(logdevice);
      }
    }
    if ((int)humidity < targetHumidity) { //  If the humidity is less than 55
      if (dehumidifierActive) {
          dehumidifierActive = false;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":DH:0");
          lcd.setCursor(0, 1);
          lcd.print(F("Dehumid. deacti."));
          printDevActivityToLog(logdevice);
      }
      if (((int)humidity < targetHumidity - percentageOfForgiveness) && (!humidifierActive)){ //  If the humidity is less than 54
          humidifierActive = true;
          strcpy(logdevice, currentTime);
          strcat(logdevice, ":HU:1");
          lcd.setCursor(0, 1);
          lcd.print(F("Humid. activated"));
          printDevActivityToLog(logdevice);
      }
    }
    activateDevice();

    lcd.setCursor(0, 0);
    lcd.print(hourminute);
    lcd.print(F("   "));
    lcd.print((int)Tf);
    lcd.print(F("F  "));
    lcd.print((int)humidity);
    lcd.print(F("%"));
    printDataToLog(currentTime, (int)Tf, (int)humidity);
    delay(500);
    digitalWrite(ledPin, LOW);
}

void printDevActivityToLog (char *devstate) {
    myFile = SD.open("data.txt", FILE_WRITE);
    if (myFile) {
        myFile.println(devstate);
    }
    myFile.close();
}

void printDataToLog (char* curtime, int temp, int humid) {
    unsigned long currentMillis2 = millis();  
    if (currentMillis2 - logTimer >= loggingInterval){  // If the logging timer is up we log 
        logTimer = currentMillis2;
        myFile = SD.open("data.txt", FILE_WRITE);
        if (myFile) {
            myFile.print(curtime);
            myFile.print(":");
            myFile.print(temp);
            myFile.print(":");
            myFile.println(humid);
            lcd.setCursor(0, 1);
            lcd.print(F("Log data to SD  ")); 
        }
        else {
            lcd.setCursor(0, 1);
            lcd.print(F("Err write to SD ")); 
        }
        myFile.close();
    }
}

void getSDdata () { // Fetches the logging data from the SD card and prints it
                    // to the ESP01 via serial
    myFile = SD.open("/data.txt", FILE_READ);
    if(myFile) {
        ESPserial.print("<");
        while (myFile.available()) {
          ESPserial.write(myFile.read());
        }
        ESPserial.println(">");
        myFile.close();
    }
    else {
      lcd.setCursor(0, 1);
      lcd.print(F("Error reading SD")); 
    }
}

// simply deletes all data on the SD card
void deleteSDdata () {
    SD.remove("/data.txt");
}

// Receives data from the ESP01 via serial connection
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

// Prepares a string to be sent to the ESP01 out of a binary (8 characters)
String makeInfoString (char commandLetter[]) { // Command letter is a code that separates between 
                                                // polling a manual device state change
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
    //Serial.begin(9600); // Used for debugging
    pinMode(pinCS, OUTPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(dataPin, OUTPUT);
    pinMode(latchPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    writeReg(allOn); // Turn all devices off: shift regisiter is inverting
    lcd.begin(16, 2);
    lcd.setCursor(0, 0);
    lcd.print(F("Starting up!"));
    delay(1000);
    if (SD.begin()) {
       lcd.setCursor(0, 0);
       lcd.print(F("SD card ready  "));
       delay(1000);
    }
    else {
       lcd.setCursor(0, 0);
       lcd.print(F("Error:          "));
       lcd.setCursor(0, 1);
       lcd.print(F("No SD card found"));
       delay(1000);
    }
    ESPserial.begin(9600);
    delay(30);
    getIP = true;
    lcd.setCursor(0, 0);
    lcd.print(F("Getting IP      "));

}

// Main loop
void loop() {
    if (getIP) {
      unsigned long currentMillis = millis();
      ESPserial.print("<3>");  // Request the IP address from the ESP01
      delay(2000);
      while(!(ESPserial.available())) { // Wait for the response
        unsigned long currentMillisPast = millis();
        if ((currentMillisPast - currentMillis) > 10000) { // Timeout after 10 seconds
          break;                                            // and print error
          lcd.setCursor(0, 0);
          lcd.print(F("Could not get IP"));
        }
      }
      getIP = false;
      waitForIP = true; // No timeout, response is ready
    }
    if (ESPserial.available()) {  // Handles responses from ESP01
      recvESPData(); 
    }
    if (newESPData) {  // Reacts to parsed ESP01 data
        if (pollSlave) {
            for (int i=0;i<10;i++) {
              currentTime[i] = receivedESPChars[i];
            }
            currentTime[11] = 0;
            requestHumTemp();  // Fetch data from DHT11 and log it
            pollSlave = false;
            String info = makeInfoString("s"); // Update ESP01 
            ESPserial.print(info);
        }
        else if (waitForIP) {
            lcd.clear();
            lcd.setCursor(0, 0);
            for (int i=0;i<sizeof(receivedESPChars);i++) {
              lcd.print(receivedESPChars[i]);  // Print the IP Address to the LCD
            }
            waitForIP = false;
            delay(5000);
        }
        else if (receivedESPChars[0] == '1') {getSDdata();}  // Gets all data and returns it to ESP01
        else if (receivedESPChars[0] == '2') {        
            lcd.setCursor(0, 1);
            lcd.print(F("Deleted all data"));
            deleteSDdata();
            ESPserial.print("<deleted>");
        }
        else if (receivedESPChars[0] == '5') {
            if (pausePolling == true) { 
              pausePolling = false;
              lcd.setCursor(0, 1);
              lcd.print(F("Logging resumed "));
              ESPserial.println("<u>");
            }
            else { 
              pausePolling = true;
              lcd.setCursor(0, 1);
              lcd.print(F("Logging paused  "));
              ESPserial.println("<p>");
            }
        }
        else if (receivedESPChars[0] == 'D') {  // The "D" code sent from the ESP01 is given as
                                                // a command to change the on/off state of a device
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
          ESPserial.println(info);
        }
        else {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print(receivedESPChars);  // If we don't know what the command is print it out
        }
        newESPData = false;
    }
    unsigned long currentMillis = millis();  // If the polling timer is up we poll the DHT11 for temp and humidity
    if (((currentMillis - slaveTimer >= slaveInterval) && (!pausePolling)) || (pollFirstTime)){
      pollFirstTime = false;
      digitalWrite(ledPin, HIGH);
        slaveTimer = currentMillis;
        ESPserial.print("<1>");
        pollSlave = true;
      delay(10);
      digitalWrite(ledPin, LOW); 
    }
}
