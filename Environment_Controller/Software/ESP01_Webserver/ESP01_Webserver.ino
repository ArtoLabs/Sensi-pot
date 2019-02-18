#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <time.h>
#include <TimeLib.h>

#if !defined(__time_t_defined) // avoid conflict with newlib or other posix libc
typedef int time_t;
#endif

ESP8266WebServer server ( 80 ); // Start up the web server

const char *ssid = "";  // These need to be provided to access the local wifi
const char *password = "";


const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
int iplen = 16; // Length of the IP address string
boolean newData = false;
boolean waitForData = false;
static boolean recvInProgress = false;
char ip[16]; // The IP address
boolean pollingPaused = false;
boolean dataErr = false;
String temp = ""; // for storing the temperature and humidity data
String humidity = "";
int devices[8]; // Stores the off/on state of all 8 devices

// The CSS and HTML for the webpage have been broken up into 
// smaller parts for variable page display.
const char* INDEX_HEADER = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
<title>Seni-Pot Room Sensor</title>
<style>
body, html {
  background-color: #dedede;
  margin: 0px;
  font-family: arial;
  width: 100%;
  height: 100%;
}
#main {
  margin: 20px;
  position: relative;
  width: calc(100% - 40px);
}
#side, #top, #content {
  position: relative;
  float: left;
  width: calc(20% - 40px);
}
#top, #content {
  width: calc(70% - 40px);
}
.btn {
  position: relative;
  clear: left;
  float: left;
  background-color: #999999;
  color: #ffffff;
  padding: 10px;
  padding-top: 6px;
  padding-bottom: 6px;
  border-radius: 10px 10px 10px 10px;
  -moz-border-radius: 10px 10px 10px 10px;
  -webkit-border-radius: 10px 10px 10px 10px;
  border: 0px solid #000000;
  margin-bottom: 5px;
  width: 150px;
}
.a {
  position: relative;
  float: left;
  padding: 10px;
  font-size: 30px;
  margin-bottom: 20px;
}
</style>
</head>
<body>
<div id="main">
)rawliteral";

const char* INDEX_FOOTER = R"rawliteral(
</div></body></html>
)rawliteral";

const char* FORM_HEADER = R"rawliteral(
<div id="side"><form id='theform' action='http://)rawliteral";

const char* FORM_MIDDLE = R"rawliteral(' method='POST'>
<a href='#' onClick="document.getElementById('ARG').value='2'; document.getElementById('theform').submit();">
<div class="btn">Get SD Data</div></a>
<a href='#' onClick="document.getElementById('ARG').value='1'; document.getElementById('theform').submit();">
<div class="btn">Delete SD Data</div></a>
)rawliteral";

const char* UNPAUSED = R"rawliteral(
<a href='#' onClick="document.getElementById('ARG').value='3'; document.getElementById('theform').submit();">
<div class="btn">Pause Logging</div></a>
)rawliteral";

const char* PAUSED = R"rawliteral(
<a href='#' onClick="document.getElementById('ARG').value='3'; document.getElementById('theform').submit();">
<div class="btn">Resume Logging</div></a>
)rawliteral";

const char* FORM_FOOTER = R"rawliteral(
<input type='hidden' name='ARG' id='ARG'>
<input type='hidden' name='ARG2' id='ARG2'>
</form></div>
)rawliteral";

const char* DEVICE_BUTTON_TOP = R"rawliteral(
<a href='#' onClick="document.getElementById('ARG').value='4'; document.getElementById('ARG2').value=')rawliteral";

const char* DEVICE_BUTTON_BOTTOM = R"rawliteral('; document.getElementById('theform').submit();">
<div class="btn">
)rawliteral";

// Creates the index page
void startPage() {
  server.sendHeader("Content-Type", "text/html");
  server.sendContent(INDEX_HEADER);
}

// Creates the top of every page
void makePageTop() {
  time_t now = time(nullptr);
  server.sendContent("<div id='top'><div class='a'>");
  server.sendContent(ctime(&now));
  server.sendContent("</div>");
  server.sendContent("<div class='a'>");
  server.sendContent(temp);
  server.sendContent("F</div>");
  server.sendContent("<div class='a'>");
  server.sendContent(humidity);
  server.sendContent("%</div></div>");
  
}

// Creates the side bar buttons
void deviceBtns() {

  for (int i=0; i<8; i++) {
    String f = String(i+1);
    server.sendContent(DEVICE_BUTTON_TOP);
    server.sendContent(f);
    server.sendContent(DEVICE_BUTTON_BOTTOM);
    if (devices[i] > 0) {
      server.sendContent("Device ");
      server.sendContent(f);
      server.sendContent(" is ON</div></a>");
    }
    else {
      server.sendContent("Device ");
      server.sendContent(f);
      server.sendContent(" is OFF</div></a>");
    }
  }
  
}

// Creats the side bar
void makeSideBar() {
  server.sendContent(FORM_HEADER);
  server.sendContent(ip);
  server.sendContent(FORM_MIDDLE);
  if (pollingPaused) {
      server.sendContent(PAUSED);
  }
  else {
      server.sendContent(UNPAUSED);
  }
  deviceBtns();
  server.sendContent(FORM_FOOTER);
  
}

// Creates the footer of every page
void makePageBottom() {
  server.sendContent(INDEX_FOOTER);
  server.client().stop();
}

// Creates the error page
void returnFail(String msg) {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(500, "text/plain", msg + "\r\n");
}

// Stores the temperature and humidity retreived from the ATMEGA328P
// as well as all of the current device on/off states. This is used
// to display the buttons in the side bar
void assignDev() {
  temp = receivedChars[1];
  temp += receivedChars[2];
  humidity = receivedChars[3];
  humidity += receivedChars[4];
  for(int i=0; i<8; i++) {
    devices[i] = receivedChars[i+5] - '0';
  }
}

// Parses the form request. All actions are given a single digit code
// stored in Davalue.
void handleSubmit() {
  String Davalue;
  String dev;
  Davalue = server.arg("ARG");
  dev = server.arg("ARG2");
  if (Davalue == "1") {       // Delete all data on the SD card
    Serial.println("<2>");    // Returns a success code to the ATMEGA328P (only indicates code was received)
    time_t timer = time(nullptr);
    while (!Serial.available()) { 
      time_t now = time(nullptr);  // Every action has a 10 second timeout period
      if (now > timer + 10) {
        startPage();
        makeSideBar();
        makePageTop();
        server.sendContent("<div id='content'><h3>Could not delete data</h3></div>");
        makePageBottom();
        dataErr = true;
        break; 
      }
    }
    if (!dataErr) {
      startPage();
      makeSideBar();
      makePageTop();
      server.sendContent("<div id='content'><h3>SD card has been deleted</h3></div>");
      makePageBottom();
      dataErr = false;
    }
  }
  else if (Davalue == "2") {  // Retieves the logging data from the SD card and display it on the webpage
    Serial.print("<1>");      // Send the success code to the ATMEGA328P (only indicates code was received)
    waitForData = true;
    time_t timer = time(nullptr);  // 10 second timeout period
    while (!Serial.available()) { 
      time_t now = time(nullptr);
      if (now > timer + 10) {
        startPage();
        makeSideBar();
        makePageTop();
        server.sendContent("<div id='content'><h3>Could not get data</h3></div>");
        makePageBottom();
        waitForData == false;
        break; 
      }
    }
  }
  else if (Davalue == "3") {  // Pauses logging
    Serial.print("<5>");
    time_t timer = time(nullptr);   // 10 second timeout period
    while (!Serial.available()) {
      time_t now = time(nullptr);
      if (now > timer + 10) {
        startPage();
        makeSideBar();
        makePageTop();
        server.sendContent("<div id='content'><h3>Logging could not be paused</h3></div>");
        makePageBottom();
        break; 
      }
    }
  }
  else if (Davalue == "4") {  // Changes the on/off state of a device when the user clicks a button
    Serial.print("<D");
    Serial.print(dev);
    Serial.print(">");
    time_t timer = time(nullptr);
    while (!Serial.available()) {
      time_t now = time(nullptr);
      if (now > timer + 10) {
        startPage();
        makeSideBar();
        makePageTop();
        server.sendContent("<div id='content'><h3>Device could not be reached</h3></div>");
        makePageBottom();
        break; 
      }
    }
  }
  else {returnFail("Bad value");}
}

// Initial startup creates the index page
void handleRoot() {
  if (server.hasArg("ARG")) {
    handleSubmit();
  }
  else {
    startPage();
    makeSideBar();
    makePageTop();
    server.sendContent("<div id='content'><h3>Welcome</h3></div>");
    makePageBottom();
  }

}

void handleNotFound() { server.send ( 404, "text/plain", "File Not Found\n\n" ); }

// This function parses Serial data sent by the ATMEGA328P
void recvData() {
    static byte ndx = 0;
    char startMarker = '<';  // The start and end markers are used to delineate the data
    char endMarker = '>';
    char rc;
    if (waitForData) {
      startPage();
      makeSideBar();
      makePageTop();
      server.sendContent("<div id='content'>");
    }
    time_t timer = time(nullptr);
    while ((Serial.available() > 0 && newData == false) || (recvInProgress)) {
        time_t now = time(nullptr);
        if (now > timer + 30) {    // 30 second timeout
          recvInProgress = false;
          break;
        }
        if (Serial.available()) {
            rc = Serial.read();
            if (recvInProgress == true) {
                if (rc != endMarker) {  // Wait for the end marker
                    if (waitForData) {
                      if (rc == '\n') {
                        server.sendContent("<br>");  // Replace linebreaks with br tags
                      }
                      else {
                        server.sendContent(String(rc));  // Print the received data to the webpage 
                      }
                    }
                    receivedChars[ndx] = rc;  // We do this one character at a time
                    ndx++;
                    if (ndx >= numChars) {
                        ndx = numChars - 1;
                    }
                }
                else {
                    receivedChars[ndx] = '\0'; // terminate the string
                    recvInProgress = false;
                    ndx = 0;
                    newData = true;
                }
            }
            else if (rc == startMarker) {
                recvInProgress = true;  // Keep going
            }
        }
    }
    if (waitForData) {
      server.sendContent("</div>");  // All done
      makePageBottom(); 
      waitForData = false; 
    }
}

void setup ( void ) {
	Serial.begin ( 9600 );
	WiFi.mode ( WIFI_STA );
	WiFi.begin ( ssid, password );
	while ( WiFi.status() != WL_CONNECTED ) {delay ( 500 );}
  configTime(-7 * 3600, 0, "pool.ntp.org", "time.nist.gov");  // We fetch the current time. -7 is for U.S. timezone
                                                              // Change as needed. plus 12 timezones going east from 
                                                              // Greenwich, negative 12 timezones going west
  while (!time(nullptr)) {delay(1000);}
  iplen = WiFi.localIP().toString().length() + 1;             // Get the IP address for the webpage
  WiFi.localIP().toString().toCharArray(ip, iplen);
	server.on ( "/", handleRoot );
	server.onNotFound ( handleNotFound );
	server.begin();
}

// Main loop
void loop ( void ) {
  server.handleClient(); // Did anything happen on the webpage?
  if ((Serial.available()) && (recvInProgress == false)){ // Is there any Serial data?
      recvData();
  }
  if (newData) {
      if (receivedChars[0] == '1') {  // The single digit code 1 requests the current time
                                      // This request is used for data logging
          time_t t = time(nullptr);
          int seconds = (int) t;
          char ds[10];
          itoa(seconds,ds,10);      // the time is sent as a UNIX timestamp
          Serial.print ("<");
          for(int i=0;i<10;i++) {
            Serial.print(ds[i]);    // Send the data back to ATMEGA328P
          }
          Serial.print (">");
      }
      else if (receivedChars[0] == '2') { // Code 2 requests the ESP01 sets it's own time to the current time
                                          // This request is used for displaying the time and date
          time_t t = time(nullptr);
          setTime(t);
          char buf[20];
          sprintf(buf,"%d-%02d-%02d %02d:%02d:%02d", year(), month(),
          day(), hour(), minute(), second());
          Serial.print ("<");
          for(int i=0;i<20;i++) {
            Serial.print(buf[i]);
          }
          Serial.print (">");
      }
      else if (receivedChars[0] == '3') { // code 3 requests the IP address
          Serial.print ("<");
          for(int i=0;i<iplen;i++) {
            Serial.print(ip[i]);
          }
          Serial.print (">");
      }
      else if (receivedChars[0] == 's') { // The ESP01 receives the single letter code "s"
                                          // as a response to data logging 
          assignDev();
      }
      else if (receivedChars[0] == 'd') { // The code "d" is received in response to changing the
                                          // device on/off state.
          assignDev();
          startPage();
          makeSideBar();
          makePageTop();
          server.sendContent("<div id='content'><h3>Device command accepted</h3></div>");
          makePageBottom();
      }
      else if (receivedChars[0] == 'p') { // Code "p" pauses the data logging
          pollingPaused = true;
          startPage();
          makeSideBar();
          makePageTop();
          server.sendContent("<div id='content'><h3>Logging has been paused</h3></div>");
          makePageBottom();
      }
      else if (receivedChars[0] == 'u') { // Code "u" unpauses the data logging
          pollingPaused = false;
          startPage();
          makeSideBar();
          makePageTop();
          server.sendContent("<div id='content'><h3>Logging has resumed</h3></div>");
          makePageBottom();
      }
      newData = false;
  } 
}
