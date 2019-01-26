#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <time.h>
#include <TimeLib.h>

#if !defined(__time_t_defined) // avoid conflict with newlib or other posix libc
typedef int time_t;
#endif

ESP8266WebServer server ( 80 );

const char *ssid = "BuzzOff";
const char *password = "Dimodem@1950";
const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
int iplen = 16;
boolean newData = false;
boolean waitForData = false;
static boolean recvInProgress = false;
char ip[16];
boolean pollingPaused = false;
boolean dataErr = false;
String temp = "";
String humidity = "";
int devices[8];

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

void startPage() {
  server.sendHeader("Content-Type", "text/html");
  server.sendContent(INDEX_HEADER);
}

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
  
void makePageBottom() {
  server.sendContent(INDEX_FOOTER);
  server.client().stop();
}

void returnFail(String msg) {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(500, "text/plain", msg + "\r\n");
}

void assignDev() {
  temp = receivedChars[1];
  temp += receivedChars[2];
  humidity = receivedChars[3];
  humidity += receivedChars[4];
  for(int i=0; i<8; i++) {
    devices[i] = receivedChars[i+5] - '0';
  }
}

void handleSubmit() {
  String Davalue;
  String dev;
  Davalue = server.arg("ARG");
  dev = server.arg("ARG2");
  if (Davalue == "1") {
    Serial.println("<2>");
    time_t timer = time(nullptr);
    while (!Serial.available()) { 
      time_t now = time(nullptr);
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
  else if (Davalue == "2") {
    Serial.print("<1>");
    waitForData = true;
    time_t timer = time(nullptr);
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
  else if (Davalue == "3") {
    Serial.print("<5>");
    time_t timer = time(nullptr);
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
  else if (Davalue == "4") {
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

void recvData() {
    static byte ndx = 0;
    char startMarker = '<';
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
        if (now > timer + 30) {
          recvInProgress = false;
          break;
        }
        if (Serial.available()) {
            rc = Serial.read();
            if (recvInProgress == true) {
                if (rc != endMarker) {
                    if (waitForData) {
                      if (rc == '\n') {
                        server.sendContent("<br>");
                      }
                      else {
                        server.sendContent(String(rc));
                      }
                    }
                    receivedChars[ndx] = rc;
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
                recvInProgress = true;
            }
        }
    }
    if (waitForData) {
      server.sendContent("</div>");
      makePageBottom(); 
      waitForData = false; 
    }
}

void setup ( void ) {
	Serial.begin ( 9600 );
	WiFi.mode ( WIFI_STA );
	WiFi.begin ( ssid, password );
	while ( WiFi.status() != WL_CONNECTED ) {delay ( 500 );}
  configTime(-7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {delay(1000);}

  iplen = WiFi.localIP().toString().length() + 1;
  WiFi.localIP().toString().toCharArray(ip, iplen);

	server.on ( "/", handleRoot );
	server.onNotFound ( handleNotFound );
	server.begin();
}

void loop ( void ) {
  server.handleClient();
  if ((Serial.available()) && (recvInProgress == false)){
      recvData();
  }
  if (newData) {
      if (receivedChars[0] == '1') {
          time_t t = time(nullptr);

          int seconds = (int) t;
          
          char ds[10];
          itoa(seconds,ds,10);
          
          Serial.print ("<");
          for(int i=0;i<10;i++) {
            Serial.print(ds[i]);
          }
          Serial.print (">");
      }
      else if (receivedChars[0] == '2') {
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
      else if (receivedChars[0] == '3') {
        
          Serial.print ("<");
          for(int i=0;i<iplen;i++) {
            Serial.print(ip[i]);
          }
          Serial.print (">");
      }
      else if (receivedChars[0] == 's') {
          assignDev();
      }
      else if (receivedChars[0] == 'd') {
          assignDev();
          startPage();
          makeSideBar();
          makePageTop();
          server.sendContent("<div id='content'><h3>Device command accepted</h3></div>");
          makePageBottom();
      }
      else if (receivedChars[0] == 'p') {
          pollingPaused = true;
          startPage();
          makeSideBar();
          makePageTop();
          server.sendContent("<div id='content'><h3>Logging has been paused</h3></div>");
          makePageBottom();
      }
      else if (receivedChars[0] == 'u') {
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
