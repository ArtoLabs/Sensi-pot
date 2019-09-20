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
#define LOG_BUFFER_SIZE 16 // Unix timestamp (10 digits) + : (1 digit) + temp (2 digits) + : (1 digit) + humidity (2 digits) 
#define CS_PIN 5
#define CURRENT_UNIX_TIMESTAMP 1550447210 // This is only used for demonstration purposes and should be replaced (Approx Feb 17 16:50)

#define backlight 6

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
const byte relay8 = 0x01; //0b00000001    ALARM
const byte allOff = 0x00;
const byte allOn = 0xFF;

char logDataBuffer[LOG_BUFFER_SIZE];

byte state;
bool humidifierActive = false;
bool dehumidifierActive = false;
bool heaterActive = false;
bool acActive = false;

float Tf = 0;
byte temperature = 0;
byte humidity = 0;
char tempChar[3];
char humChar[3];

const byte numChars = 32;
char receivedESPChars[numChars]; // an array to store the received data
bool newESPData = false;

bool waitForTime = false;
bool pollSlave = false;
bool getIP = false;
bool waitForIP = true;

char currentTime[11];
bool pollFirstTime = true;    // Don't wait 5 minutes to check the first time.
unsigned long slaveTimer = 0;
unsigned long logTimer = 0;
bool pausePolling = false;
bool alarmOn = false;
bool humidityHighAlarm = false; // These values hold whether an alarm is used
bool humidityLowAlarm = false;
bool tempHighAlarm = false;
bool tempLowAlarm = false;
unsigned long humidityHighTimer = 0;
unsigned long humidityLowTimer = 0;
unsigned long tempHighTimer = 0;
unsigned long tempLowTimer = 0;



// User defined variables
const long alarmWaitPeriod = 240000; //600000; // wait 10 minutes before triggering alarm
const long slaveInterval = 15000;  // Check every 15 seconds
const long loggingInterval = 60000; //300000;  // log every 5 minutes
const int targetTemp = 75;
const int degreesOfForgiveness = 2;
const int targetHumidity = 55;
const int percentageOfForgiveness = 1;

// Pin assignment for the LCD
//const int rs = 19, en = 18, d4 = 17, d5 = 15, d6 = 14, d7 = 6;
//LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
LiquidCrystal lcd(19, 18, 17, 16, 15, 7);

SimpleDHT11 dht11;

SoftwareSerial ESPserial(RX, TX);

SPIFlash flash(CS_PIN);


bool checkLDR () {
  int sensorValue = analogRead(pinLDR);
  if (sensorValue < 200) {
    return true;
  }
  else {
    return false;
  }
}

// Changes the binary value for the shift register
void activateDevice () {
  state = allOff; 
  if (humidifierActive) { state = relay1; }
  if (dehumidifierActive) { state = state xor relay2; }
  if (heaterActive) { state = state xor relay3; }
  if (acActive) { state = state xor relay4; }
  if (!(alarmOn)) { state = state xor relay8; }
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
    unsigned long ulCurrentTime = cT.toInt();

    // Set the local system time to the time received by the ESP01
    setTime(ulCurrentTime);

    char hourminute[6];
    sprintf(hourminute,"%02d:%02d", hour(), minute());
    bool msg = false;
    char logdevice[20];
    if (Tf > targetTemp) {  //  If the temperature is greater than 75
      if (heaterActive) {
          heaterActive = false;
          lcd.setCursor(0, 1);
          lcd.print(F("Heat deactivated"));
      }
      if ((Tf > targetTemp + degreesOfForgiveness) && (!acActive)){  //  If the temperature is greater than 77
          acActive = true;
          lcd.setCursor(0, 1);
          lcd.print(F("AC activated    "));
          tempHighTimer = millis();
      }
      msg = true;
    }
    else if (Tf < targetTemp) { //  If the temperature is less than 75
      if (acActive) {
          acActive = false;
          lcd.setCursor(0, 1);
          lcd.print(F("AC deactivated  "));          
      } 
      if ((Tf < targetTemp - degreesOfForgiveness) && (!heaterActive)){ //  If the temperature is less than 73
          heaterActive = true;
          lcd.setCursor(0, 1);
          lcd.print(F("Heat activated  "));
          tempLowTimer = millis();
      }
      msg = true;
    }
    if ((int)humidity > targetHumidity) { //  If the humidity is greater than 55
      if (humidifierActive) {
          humidifierActive = false;
          lcd.setCursor(0, 1);
          lcd.print(F("Humid. deactiva."));
      }
      if (((int)humidity > targetHumidity + percentageOfForgiveness) && (!dehumidifierActive)){ //  If the humidity is greater than 56
          dehumidifierActive = true;
          lcd.setCursor(0, 1);
          lcd.print(F("Dehumid. activa."));
          humidityHighTimer = millis();
      }
      msg = true;
    }
    if ((int)humidity < targetHumidity) { //  If the humidity is less than 55
      if (dehumidifierActive) {
          dehumidifierActive = false;
          lcd.setCursor(0, 1);
          lcd.print(F("Dehumid. deacti."));
      }
      if (((int)humidity < targetHumidity - percentageOfForgiveness) && (!humidifierActive)){ //  If the humidity is less than 54
          humidifierActive = true;
          lcd.setCursor(0, 1);
          lcd.print(F("Humid. activated"));
          humidityLowTimer = millis();
      }
      msg = true;
    }
    activateDevice();

    lcd.setCursor(0, 0);
    lcd.print(hourminute);
    lcd.print(F("   "));
    lcd.print((int)Tf);
    lcd.print(F("F  "));
    lcd.print((int)humidity);
    lcd.print(F("%"));


    unsigned long currentMillis2 = millis();
    if ((currentMillis2 - tempHighTimer >= alarmWaitPeriod) && (acActive) && (tempHighAlarm)) {
        alarmOn = true;
        lcd.setCursor(0, 1);
        lcd.print(F("Temp is too high "));
    }
    else if ((currentMillis2 - tempLowTimer >= alarmWaitPeriod) && (heaterActive) && (tempLowAlarm)) {
        alarmOn = true;
        lcd.setCursor(0, 1);
        lcd.print(F("Temp is too low "));
    }
    else if ((currentMillis2 - humidityHighTimer >= alarmWaitPeriod) && (dehumidifierActive) && (humidityHighAlarm)) {
        alarmOn = true;
        lcd.setCursor(0, 1);
        lcd.print(F("Humid. too high "));
    }
    else if ((currentMillis2 - humidityLowTimer >= alarmWaitPeriod) && (humidifierActive) && (humidityLowAlarm)) {
        alarmOn = true;
        lcd.setCursor(0, 1);
        lcd.print(F("Humid. too low "));
    }
    else {
        alarmOn = false;
    }
    if (currentMillis2 - logTimer >= loggingInterval) {  // If the logging timer is up we log 
        logTimer = currentMillis2;
        printDataToLog(ulCurrentTime, (int)Tf, (int)humidity);
        if(msg) { delay(3000); }
        lcd.setCursor(0, 1);
        lcd.print(F("Updated log     "));
        Serial.print("Logged: ");
        Serial.print(ulCurrentTime);
        Serial.print(" ");
        Serial.print((int)Tf);
        Serial.print("F ");
        Serial.print((int)humidity);
        Serial.println("%");
    }
    else if (!(msg)) {
        lcd.setCursor(0, 1);
        lcd.print("                ");
    }
    
    delay(500);

}

void printChipStats() {

    Serial.println("Winbond chip stats");
    
    ESPserial.print("<Memory: ");
    delay(3);
    uint32_t JEDEC = flash.getJEDECID();
    lcd.setCursor(0, 0);
    lcd.print(F("JEDEC ID:"));
    String jedecid = String(JEDEC);
    lcd.print(jedecid);

    Serial.print("JEDEC ID: ");
    Serial.println(jedecid);

    ESPserial.print(jedecid);
    delay(3);
    ESPserial.print(" ");
    delay(3);

    uint32_t Cap = flash.getCapacity() / 1048576;
    lcd.setCursor(0, 1);
    lcd.print(F("Capacity: "));
    String capy = String(Cap);
    lcd.print(capy);
    lcd.print(F(" MB  "));

    Serial.print("Capacity: ");
    Serial.print(capy);
    Serial.println(" MB");

    ESPserial.print(capy);
    delay(3);
    ESPserial.print(" MB>");

    delay(5000);

}

void printDataToLog (unsigned long currentTimestamp, int temperature, int humidity) {

    ultoa(currentTimestamp, logDataBuffer, 10);

    itoa(temperature, tempChar, 10);
    itoa(humidity, humChar, 10);

    strcat(logDataBuffer, ":");

    strcat(logDataBuffer, tempChar);
    strcat(logDataBuffer, ":");
    strcat(logDataBuffer, humChar);


    uint32_t Myaddr = flash.getAddress(sizeof(char));

    //Serial.print(F("Next available address: "));
    //Serial.println(Myaddr);

    flash.writeCharArray(Myaddr, logDataBuffer, sizeof(logDataBuffer));


}

void getSPIdata () { // Fetches the logging data from Winbond memory chip
    uint32_t paddr = 0x00; // Start at the very beginning of the chip
    int dude = 0;
    ESPserial.print("<");
    while(1) {
      
      if (endOfData(paddr)) { break; }
      flash.readCharArray(paddr, logDataBuffer, sizeof(logDataBuffer));

      for(uint32_t i=0; i<sizeof(logDataBuffer); i++) {
        Serial.print(logDataBuffer[i]);
        ESPserial.print(logDataBuffer[i]);
      }
      ESPserial.print("n");
      Serial.println("");
      lcd.setCursor(0, dude);
      lcd.print(logDataBuffer);
      if (dude > 0){ dude = 0; }
      else { dude = 1; }
      
      delay(3);
      paddr += LOG_BUFFER_SIZE;
      
    }
    ESPserial.print(">");

}

// Find the end of our data log. All the bytes of the chip are set to High 0xFF (255)
// When the chip is erased. Since no data entered by the data logger is likely to
// have 6 bytes in a row set high, it is considered the end of the data.
bool endOfData(uint32_t thisaddr) {
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

//Prints hex/dec formatted data from page reads - for debugging
void _printPageBytes(uint8_t *data_buffer, uint8_t outputType) {
  char buffer[10];
  for (int a = 0; a < 16; ++a) {
    for (int b = 0; b < 16; ++b) {
      if (outputType == 1) {
        sprintf(buffer, "%02x", data_buffer[a * 16 + b]);
        Serial.print(buffer);
      }
      else if (outputType == 2) {
        uint8_t x = data_buffer[a * 16 + b];
        if (x < 10) Serial.print("0");
        if (x < 100) Serial.print("0");
        Serial.print(x);
        Serial.print(',');
      }
    }
    Serial.println();
  }
}

//Reads a page of data and prints it to Serial stream. Make sure the sizeOf(uint8_t data_buffer[]) == 256.
void printPage(uint32_t _addy, uint8_t outputType) {

  char buffer[24];
  sprintf(buffer, "Reading address 0x(%04x)", _addy);
  Serial.println(buffer);

  uint8_t data_buffer[SPI_PAGESIZE];
  flash.readByteArray(_addy, &data_buffer[0], SPI_PAGESIZE);
  _printPageBytes(data_buffer, outputType);
}

// simply deletes all data on the Winbond memory chip
void deleteSPIdata () {
    flash.eraseChip();
    flash.begin();
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

    pinMode(dataPin, OUTPUT);
    pinMode(latchPin, OUTPUT);
    pinMode(clockPin, OUTPUT);
    pinMode(backlight, OUTPUT);
    writeReg(allOn); // Turn all devices off: shift regisiter is inverting
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

    //printChipStats();

    //printDataToLog (currentTimestamp, temperature, humidity);

    /*
    Serial.print(F("The next available address: "));
    uint32_t gaddr = flash.getAddress(0);
    Serial.println(gaddr);
    */  
    getIP = true;
    lcd.setCursor(0, 0);
    lcd.print(F("Getting IP      "));
    Serial.println("Getting IP");
    
}

// Main loop
void loop() {
    unsigned long currentMillis = millis();  // If the polling timer is up we poll the DHT11 for temp and humidity (see bottom)
    if (checkLDR()) { digitalWrite(backlight, LOW); }
    else { digitalWrite(backlight, HIGH); }
    if (getIP) {
      unsigned long currentMillis = millis();
      ESPserial.print("<3>");  // Request the IP address from the ESP01
      while(!(ESPserial.available())) { // Wait for the response
        unsigned long currentMillisPast = millis();
        if ((currentMillisPast - currentMillis) > 10000) { // Timeout after 10 seconds
                                            // and print error
          lcd.setCursor(0, 0);
          lcd.print(F("Could not get IP"));
          break;
        }
      }
      delay(5000);
      getIP = false;
      waitForIP = true; // No timeout, response is ready
    }
    if (ESPserial.available() && !(newESPData) && !(getIP)) {  // Handles responses from ESP01
      recvESPData(); 
    }
    if (newESPData) {  // Reacts to parsed ESP01 data
        if (waitForIP) {
            lcd.clear();
            lcd.setCursor(0, 0);
            for (int i=0;i<sizeof(receivedESPChars);i++) {
              lcd.print(receivedESPChars[i]);  // Print the IP Address to the LCD
              Serial.print(receivedESPChars[i]);
              delay(5);
            }
            waitForIP = false;
            delay(5000);
        }
        else if (pollSlave) {
            for (int i=0;i<10;i++) {
              currentTime[i] = receivedESPChars[i];
            }
            currentTime[11] = 0;
            Serial.print("Polling at current timestamp: ");
            Serial.println(currentTime);
            requestHumTemp();  // Fetch data from DHT11 and log it
            pollSlave = false;
            String info = makeInfoString("s"); // Update ESP01 
            ESPserial.print(info);
        }
        else if (receivedESPChars[0] == '1') {getSPIdata();}  // Gets all data and returns it to ESP01
        else if (receivedESPChars[0] == '2') {        
            lcd.setCursor(0, 1);
            lcd.print(F("Deleted all data"));
            deleteSPIdata();
            ESPserial.print("<deleted>");
        }
        else if (receivedESPChars[0] == '3') {  // Print the Winbond chip stats to LCD
            printChipStats();
        }
        else if (receivedESPChars[0] == '4') {  // Set the various alarms. 

            if (receivedESPChars[2] == '1') { humidityHighAlarm = true; }
            else { humidityHighAlarm = false; }

            if (receivedESPChars[3] == '1') { humidityLowAlarm = true; }
            else { humidityLowAlarm = false; }

            if (receivedESPChars[4] == '1') { tempHighAlarm = true; }
            else { tempHighAlarm = false; }

            if (receivedESPChars[5] == '1') { tempLowAlarm = true; }
            else { tempLowAlarm = false; }

            ESPserial.print("<a>");
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
        else if (receivedESPChars[0] == '6') {  // Send back the alarm option values to the ESP01

            ESPserial.print("<4:");
            if (humidityHighAlarm) { ESPserial.print("1"); }
            else { ESPserial.print("0"); }
            if (humidityLowAlarm) { ESPserial.print("1"); }
            else { ESPserial.print("0"); }         
            if (tempHighAlarm) { ESPserial.print("1"); }
            else { ESPserial.print("0"); }
            if (tempLowAlarm) { ESPserial.print("1"); }
            else { ESPserial.print("0"); }

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
            else if (receivedESPChars[1] == '8') {
                if (alarmOn) { alarmOn = false; }
                else { alarmOn = true; }
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
    else if (((currentMillis - slaveTimer >= slaveInterval) && (!pausePolling)) || (pollFirstTime)) {
        pollFirstTime = false;
        slaveTimer = currentMillis;
        ESPserial.print("<1>");
        pollSlave = true;
        delay(10);
    }
}
