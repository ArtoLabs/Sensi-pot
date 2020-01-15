#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <time.h>
#include <TimeLib.h>
#include <avr/pgmspace.h>
#if !defined(__time_t_defined) // avoid conflict with newlib or other posix libc
typedef int time_t;
#endif
#define timezoneEepromAddr 0
#define ssidEepromAddr 10
#define passwordEepromAddr 40
#define SSID_SIZE 25
#define PASSWORD_SIZE 25

//   USED TO DEBUG MEMORY PROBLEMS
//   REPORTS AVAILABLE MEMORY LEFT IN THE HEAP
extern "C" {
#include "user_interface.h"
}
void freeMemory() {
  Serial.print(F("< bytes left: "));
  Serial.print(system_get_free_heap_size());
  Serial.print(F(">"));
}

ESP8266WebServer server (80); // Start up the web server
ESP8266WebServer APserver (81);

int timezone = 0;
const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
char myPassword[PASSWORD_SIZE];
char ssid[SSID_SIZE];
char interval[6];
boolean newData = false, waitForData = false, recvInProgress = false, logPaused = false, haveSettings = false, triedServer = false;
int iplen = 16; // Length of the IP address string
char ip[16]; // The IP address
char currentDev = '1';

// function prototypes
void parseSettings();
void startPage();
void middlePage(const __FlashStringHelper*);
void endPage();
void ajaxReply(const __FlashStringHelper*);
void ajaxReply(char&); // Overloaded function
void returnFail(const __FlashStringHelper* );
void scanAllNetworks();
boolean handleTimeout(const __FlashStringHelper*, boolean);
void handleSubmit();
void handleRoot();
void handleAPSubmit();
void handleAPRoot();
void handleNotFound();
void recvData();
void startAP();
boolean startServer();


const char ap_welcome[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Seni-Pot Sensor and Device Hub</title><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no"><style>.form-style-6{font:95%Arial,Helvetica,sans-serif;max-width:400px;margin:10px auto;padding:16px;background:#F7F7F7}.form-style-6 h1{background:#5bc0de;padding:20px 0;font-size:140%;font-weight:300;text-align:center;color:#fff;margin:-16px-16px 16px-16px}.form-style-6 input[type="text"]
{-webkit-transition:all 0.30s ease-in-out;-moz-transition:all 0.30s ease-in-out;-ms-transition:all 0.30s ease-in-out;-o-transition:all 0.30s ease-in-out;outline:none;box-sizing:border-box;-webkit-box-sizing:border-box;-moz-box-sizing:border-box;width:100%;background:#fff;margin-bottom:4%;border:1px solid #ccc;padding:3%;color:#555;font:95%Arial,Helvetica,sans-serif}.form-style-6 input[type="text"]:focus
{box-shadow:0 0 5px #52aac4
padding:3%;border:1px solid #52aac4}.form-style-6 input[type="submit"],.form-style-6 input[type="button"]{box-sizing:border-box;-webkit-box-sizing:border-box;-moz-box-sizing:border-box;width:100%;padding:3%;background:#5bc0de;border-bottom:2px solid #499eb8;border-top-style:none;border-right-style:none;border-left-style:none;color:#fff}.form-style-6 input[type="submit"]:hover,.form-style-6 input[type="button"]:hover{background:#52aac4}</style></head><body><div class="form-style-6"><h1>Welcome!</h1>Please enter your router's SSID and password for Internet connection.<br><br><form action="http://192.168.4.1:81" method="POST"><input name="ARG" type="text" placeholder="SSID"><input name="ARG1" type="text" placeholder="Password"><input type="submit" value="submit"></form></div></body></html>
)rawliteral";


const char ap_thankyou[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Seni-Pot Sensor and Device Hub</title><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no"><style>.form-style-6{font:95%Arial,Helvetica,sans-serif;max-width:400px;margin:10px auto;padding:16px;background:#F7F7F7}.form-style-6 h1{background:#43D1AF;padding:20px 0;font-size:140%;font-weight:300;text-align:center;color:#fff;margin:-16px-16px 16px-16px}</style><script language="javascript">var ajaxRequest=null;var ajaxResult=null;var count=15;var attempts=0;if(window.XMLHttpRequest){ajaxRequest=new XMLHttpRequest()}
else{ajaxRequest=new ActiveXObject("Microsoft.XMLHTTP")}
function ajaxLoad(ajaxURL){if(!ajaxRequest){alert("AJAX is not supported.");return}
ajaxRequest.open("GET",ajaxURL,!0);ajaxRequest.onreadystatechange=function(){if(ajaxRequest.readyState==4&&ajaxRequest.status==200){document.getElementById("msg").innerHTML=ajaxRequest.responseText}}
ajaxRequest.send()}
var si=setInterval(function(){if(attempts>2){document.getElementById("msg").innerHTML="It seems the WiFi connection could not be established. Please click back and try again."}
else if(document.getElementById("counter")){document.getElementById("counter").innerHTML=count;count--;if(count<1){ajaxLoad("http://192.168.4.1:81?ARG=getipaddress");count=15;attempts++}}
else{clearInterval(si)}},1000);</script></head><body><div class="form-style-6"><h1>Thank you!</h1><h3 id="msg">Please wait for this page to automatically reload in&nbsp;<span id="counter"></span>&nbsp;seconds with the IP address you will need to access this device on your router.</h3></div></body></html>
)rawliteral";


const char header[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head><title>Seni-Pot Sensor and Device Hub</title><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no"><link rel="stylesheet" href="https://stackpath.bootstrapcdn.com/bootstrap/4.3.1/css/bootstrap.min.css" integrity="sha384-ggOyR0iXCbMQv3Xipma34MD+dH/1fQ784/j6cY/iJTQUOhcWr7x9JvoRxT2MZw1T" crossorigin="anonymous"><link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css"><script src="https://code.jquery.com/jquery-3.2.1.slim.min.js" integrity="sha384-KJ3o2DKtIkvYIK3UENzmM7KCkRr/rE9/Qpg6aAZGJwFDMVNA/GpGFF93hXpG5KkN" crossorigin="anonymous"></script><script src="https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.12.9/umd/popper.min.js" integrity="sha384-ApNbgh9B+Y1QKtv3Rn7W3mgPxhU9K/ScQsAP7hUibX39j7fakFPskvXusvfa0b4Q" crossorigin="anonymous"></script><script src="https://maxcdn.bootstrapcdn.com/bootstrap/4.0.0/js/bootstrap.min.js" integrity="sha384-JZR6Spejh4U02d8jOt6vLEHfe/JQGiRRSQQxSfFWpi1MquVdAyjUar5+76PVCmYl" crossorigin="anonymous"></script><script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.6.0/Chart.min.js"></script><style>canvas{margin-top:40px;margin-bottom:100px}.alert{position:absolute;left:0;width:100%;z-index:100;transition:opacity 0.4s}</style></head><body><div class="navbar navbar-expand-lg navbar-dark bg-primary"><a class="navbar-brand" href="#"><i class="fa fa-leaf"></i>&nbsp;Sensi-pot</a><button class="navbar-toggler" type="button" data-toggle="collapse" data-target="#MyNav"
aria-controls="MyNav" aria-expanded="false" aria-label="Toggle navigation"><span class="navbar-toggler-icon"></span></button><div class="collapse navbar-collapse" id="MyNav"><ul class="navbar-nav mx-auto"><h2 class="text-white" id="devicetitle">Device 1</h2></ul><ul class="navbar-nav ml-auto"><li class="nav-item dropdown"><a class="nav-link dropdown-toggle" href="#" id="timezoneDropdown" role="button"
data-toggle="dropdown" aria-haspopup="true" aria-expanded="false"><i class="fa fa-globe"></i>&nbsp;<span id="6">Timezone</id></a><div class="dropdown-menu" aria-labelledby="timezoneDropdown"><a class="dropdown-item" href="#" onclick="tsubmit(-12); return false;">-12</a><a class="dropdown-item" href="#" onclick="tsubmit(-11); return false;">-11</a><a class="dropdown-item" href="#" onclick="tsubmit(-10); return false;">-10</a><a class="dropdown-item" href="#" onclick="tsubmit(-9); return false;">-9</a><a class="dropdown-item" href="#" onclick="tsubmit(-8); return false;">-8</a><a class="dropdown-item" href="#" onclick="tsubmit(-7); return false;">-7</a><a class="dropdown-item" href="#" onclick="tsubmit(-6); return false;">-6</a><a class="dropdown-item" href="#" onclick="tsubmit(-5); return false;">-5</a><a class="dropdown-item" href="#" onclick="tsubmit(-4); return false;">-4</a><a class="dropdown-item" href="#" onclick="tsubmit(-3); return false;">-3</a><a class="dropdown-item" href="#" onclick="tsubmit(-2); return false;">-2</a><a class="dropdown-item" href="#" onclick="tsubmit(-1); return false;">-1</a><a class="dropdown-item" href="#" onclick="tsubmit(0); return false;">0</a><a class="dropdown-item" href="#" onclick="tsubmit(1); return false;">1</a><a class="dropdown-item" href="#" onclick="tsubmit(2); return false;">2</a><a class="dropdown-item" href="#" onclick="tsubmit(3); return false;">3</a><a class="dropdown-item" href="#" onclick="tsubmit(4); return false;">4</a><a class="dropdown-item" href="#" onclick="tsubmit(5); return false;">5</a><a class="dropdown-item" href="#" onclick="tsubmit(6); return false;">6</a><a class="dropdown-item" href="#" onclick="tsubmit(7); return false;">7</a><a class="dropdown-item" href="#" onclick="tsubmit(8); return false;">8</a><a class="dropdown-item" href="#" onclick="tsubmit(9); return false;">9</a><a class="dropdown-item" href="#" onclick="tsubmit(10); return false;">10</a><a class="dropdown-item" href="#" onclick="tsubmit(11); return false;">11</a><a class="dropdown-item" href="#" onclick="tsubmit(12); return false;">12</a></div></li><li class="nav-item dropdown"><a class="nav-link dropdown-toggle" href="#" id="intervalDropdown" role="button"
data-toggle="dropdown" aria-haspopup="true" aria-expanded="false"><i class="fa fa-clock-o"></i>&nbsp;<span id="7">Interval:1 hour</id></a><div class="dropdown-menu" aria-labelledby="intervalDropdown"><a class="dropdown-item" href="#" onclick="lsubmit(60); return false;">1 minute</a><a class="dropdown-item" href="#" onclick="lsubmit(300); return false;">5 minutes</a><a class="dropdown-item" href="#" onclick="lsubmit(600); return false;">10 minutes</a><a class="dropdown-item" href="#" onclick="lsubmit(1200); return false;">20 minutes</a><a class="dropdown-item" href="#" onclick="lsubmit(3600); return false;">1 hour</a><a class="dropdown-item" href="#" onclick="lsubmit(7200); return false;">2 hours</a><a class="dropdown-item" href="#" onclick="lsubmit(10800); return false;">3 hours</a><a class="dropdown-item" href="#" onclick="lsubmit(21600); return false;">6 hours</a><a class="dropdown-item" href="#" onclick="lsubmit(43200); return false;">12 hours</a><a class="dropdown-item" href="#" onclick="lsubmit(86400); return false;">24 hours</a></div></li><li class="nav-item dropdown"><a class="nav-link dropdown-toggle" href="#" id="settingsDropdown" role="button"
data-toggle="dropdown" aria-haspopup="true" aria-expanded="false"><i class="fa fa-cogs"></i>&nbsp;<span id="settings">Settings</span></a><div class="dropdown-menu" aria-labelledby="settingsDropdown"><a href="#" onclick="fsubmit(1); return false;" class="dropdown-item"><i class="fa fa-balance-scale"></i>&nbsp;&nbsp;&nbsp;<span id="1">Reset Calibration&nbsp;(this device)</span></a><a href="#" onclick="fsubmit(2); return false;" class="dropdown-item"><i class="fa fa-podcast"></i>&nbsp;&nbsp;&nbsp;<span id="2">Locate&nbsp;(this device)</span></a><a href="#" onclick="fsubmit(3); return false;" class="dropdown-item"><i class="fa fa-eraser"></i>&nbsp;&nbsp;&nbsp;<span id="3">Erase Memory</span></a><a href="#" onclick="fsubmit(4); return false;" class="dropdown-item"><i class="fa fa-server"></i>&nbsp;&nbsp;&nbsp;<span id="4">Memory Status</span></a><a href="#" onclick="fsubmit(5); return false;" class="dropdown-item"><i id="toggleicon" class="fa fa-toggle-off"></i>&nbsp;&nbsp;&nbsp;<span id="5">Pause Logging</span></a></div></li><li class="nav-item dropdown" style="margin-right: 150px;"><a class="nav-link dropdown-toggle" href="#" id="navbarDropdown" role="button"
data-toggle="dropdown" aria-haspopup="true" aria-expanded="false"><i class="fa fa-hdd-o"></i>&nbsp;<span id="8">Select Device</span></a><div class="dropdown-menu" aria-labelledby="navbarDropdown"><a class="dropdown-item" href="#" onclick="updateChart(1); return false;">Device 1</a><a class="dropdown-item" href="#" onclick="updateChart(2); return false;">Device 2</a><a class="dropdown-item" href="#" onclick="updateChart(3); return false;">Device 3</a><a class="dropdown-item" href="#" onclick="updateChart(4); return false;">Device 4</a><a class="dropdown-item" href="#" onclick="updateChart(5); return false;">Device 5</a><a class="dropdown-item" href="#" onclick="updateChart(6); return false;">Device 6</a><a class="dropdown-item" href="#" onclick="updateChart(7); return false;">Device 7</a><a class="dropdown-item" href="#" onclick="updateChart(8); return false;">Device 8</a></div></li></ul></div></div><div class="container-fluid"><div class="row"><div class="col-md-12" align="center"><div id="alertzone">
)rawliteral";


const char afteralert[] PROGMEM = R"rawliteral(
</div><div style="width: 95%;"><div class="container-fluid mt-3"><div class="row"><div class="col-md-4"><div class="card border border-danger mt-3"><div class="card-header">Soil Temperature</div><div class="card-body"><h1 class="card-title text-dark"><i class="fa fa-thermometer-3"></i>&nbsp;<span id="temp"></span>°</h1></div></div></div><div class="col-md-4"><div class="card border border-primary mt-3"><div class="card-header">Soil Humidity</div><div class="card-body"><h1 class="card-title text-dark"><i class="fa fa-cloud"></i>&nbsp;<span id="hum"></span>%</h1></div></div></div><div class="col-md-4"><div class="card border border-warning mt-3"><div class="card-header">Light</div><div class="card-body"><h1 class="card-title text-dark"><i class="fa fa-sun-o"></i>&nbsp;<span id="light"></span>%</h1></div></div></div></div></div><canvas id="myChart" width="400" height="250"></canvas></div></div></div></div><script>rawdata=[
)rawliteral";


const char footer[] PROGMEM = R"rawliteral(
var config;var chart;var labels;var timestamp_list=[];var temp_list=[];var hum_list=[];var light_list=[];var temperature;var humidity;var light;var originalText;var waiting=0;var intervallabel="1 minute";setTimeout(function(){document.getElementById('alertbox').style.opacity=0},4000);var ajaxRequest=null;var ajaxResult=null;if(window.XMLHttpRequest){ajaxRequest=new XMLHttpRequest()}
else{ajaxRequest=new ActiveXObject("Microsoft.XMLHTTP")}
function updatePauseBtn(){if(logpaused=="1"){document.getElementById("toggleicon").className="fa fa-toggle-on";document.getElementById("5").innerHTML="Resume logging"}
else{document.getElementById("toggleicon").className="fa fa-toggle-off";document.getElementById("5").innerHTML="Pause logging"}}
function popupAlert(msg){document.getElementById('alertzone').innerHTML='';document.getElementById('alertzone').innerHTML='<div class="alert alert-info" role="alert" id="alertbox">'+msg+'</div>';document.getElementById('alertbox').style.opacity=1;setTimeout(function(){document.getElementById('alertbox').style.opacity=0},4000)}
function updateInterval(){var minutes=interval/60;var hours;var e=document.getElementById("7");if(minutes>59){hours=minutes/60;e.innerHTML="Interval: "+hours+" hours"}
else{e.innerHTML="Interval: "+minutes+" minutes"}}
function ajaxLoad(ajaxURL){if(!ajaxRequest){alert("AJAX is not supported.");return}
ajaxRequest.open("GET",ajaxURL,!0);ajaxRequest.onreadystatechange=function(){if(ajaxRequest.readyState==4&&ajaxRequest.status==200){ajaxResult=ajaxRequest.responseText;if((ajaxResult=="Logging has been paused")&&(logpaused=="0")){logpaused="1";updatePauseBtn();document.getElementById("settings").innerHTML="Settings"}
else if((ajaxResult=="Logging has resumed")&&(logpaused=="1")){logpaused="0";updatePauseBtn();document.getElementById("settings").innerHTML="Settings"}
else if(ajaxResult=="Timezone has been updated"){document.getElementById(waiting).innerHTML="Timezone: "+timezone}
else if(ajaxResult=="Logging interval has been updated"){updateInterval()}
else{document.getElementById(waiting).innerHTML=originalText;document.getElementById("settings").innerHTML="Settings"}
popupAlert(ajaxResult);waiting=0}}
ajaxRequest.send()}
function fsubmit(btnnum){if(waiting==0){e=document.getElementById(btnnum);f=document.getElementById("settings");originalText=e.innerHTML;e.innerHTML='<div class="spinner-border spinner-border-sm text-info" role="status"></div>';f.innerHTML='<div class="spinner-border spinner-border-sm text-info" role="status"></div>';waiting=btnnum;url="http://"+thisip+"?ARG="+btnnum+"&ARG2="+thisdev;if(btnnum==3){if(window.confirm("Are you sure you want to delete the entire log history? This cannot be undone.")){ajaxLoad(url)}
else{return!1}}
else{ajaxLoad(url)}}}
function tsubmit(tzone){if(waiting==0){e=document.getElementById("6");e.innerHTML='<div class="spinner-border spinner-border-sm text-info" role="status"></div>';waiting=6;url="http://"+thisip+"?ARG=6&ARG2="+thisdev+"&ARG3="+tzone;timezone=tzone;ajaxLoad(url)}}
function lsubmit(inter){if(waiting==0){e=document.getElementById("7");e.innerHTML='<div class="spinner-border spinner-border-sm text-info" role="status"></div>';waiting=7;url="http://"+thisip+"?ARG=7&ARG2="+thisdev+"&ARG4="+inter;interval=inter;ajaxLoad(url)}}
function process_data(tid){timestamp_list.length=0;hum_list.length=0;temp_list.length=0;light_list.length=0;var p=0;for(i=0;i<rawdata.length;i++){var e=rawdata[i].split(":");var id=e[0];if(tid==id){timestamp_list[p]=e[1];hum_list[p]=e[2].substring(0,2);temp_list[p]=e[2].substring(2,4);light_list[p]=e[2].substring(4,6);p++}}
console.log(temp_list)}
function updateHeader(id){document.getElementById("temp").innerHTML=temp_list[temp_list.length-1];document.getElementById("hum").innerHTML=hum_list[hum_list.length-1];document.getElementById("light").innerHTML=light_list[light_list.length-1];document.getElementById("devicetitle").innerHTML="Device "+id}
function make_labels(){var ml=[];ml.length=0;var sameDate="";for(i=0;i<timestamp_list.length;i++){var date=new Date(timestamp_list[i]*1000);var hours=date.getHours();var minutes=date.getMinutes();var ampm=hours>=12?'pm':'am';hours=hours%12;hours=hours?hours:12;minutes=minutes<10?'0'+minutes:minutes;var day=date.getDate();var month=date.getMonth();var formattedTime=hours+':'+minutes+' '+ampm;var formattedDate=month+'/'+day;if(formattedDate==sameDate){ml[i]=formattedTime}
else{ml[i]=formattedDate+" "+formattedTime;sameDate=formattedDate}}
return ml}
function make_config(){config=null;config={type:'line',data:{labels:labels,datasets:[{label:'Soil Temperature',backgroundColor:'rgb(255, 99, 132)',borderColor:'rgb(255, 99, 132)',data:temp_list,fill:!1,},{label:'Humidity',backgroundColor:'rgb(23, 37, 255)',borderColor:'rgb(23, 37, 255)',fill:!1,data:hum_list,},{label:'Light',backgroundColor:'rgb(255, 255, 23)',borderColor:'rgb(255, 255, 23)',fill:!1,data:light_list,}]},options:{responsive:!0,title:{display:!1,text:'Plant Stats'},legend:{display:!1},tooltips:{mode:'index',intersect:!1,},hover:{mode:'nearest',intersect:!0},scales:{xAxes:[{display:!0,scaleLabel:{display:!1,labelString:'Time'}}],yAxes:[{display:!0,scaleLabel:{display:!1,labelString:'Value'}}]}}}}
function updateChart(id){chart.update();process_data(id);labels=make_labels();chart.data.labels=labels;make_config();updateHeader(id);chart.update()}
window.onload=function(){process_data(1);labels=make_labels();make_config();updateHeader(1);document.getElementById("6").innerHTML="Timezone: "+timezone;updatePauseBtn();updateInterval();var ctx=document.getElementById('myChart').getContext('2d');chart=new Chart(ctx,config)};</script></body></html>
)rawliteral";


void setup() {
  delay(1000);
  Serial.begin (9600);
  Serial.println(F("Initializing..."));
  WiFi.mode ( WIFI_AP_STA );
  startAP();
  delay(1000);
  Serial.print(F("<z>")); // Request the settings from the ATMEGA
}


void loop() {
  if (WiFi.status() == WL_CONNECTED) { server.handleClient(); }
  APserver.handleClient();
  if ((haveSettings) && !(triedServer)) {
    startServer();
    triedServer = true;  // Ensures we ask only once
  }
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
      Serial.print (F("<"));
      for(int i=0;i<10;i++) {
        Serial.print(ds[i]);    // Send the data back to ATMEGA328P
      }
      Serial.print (F(">"));
    }
    else if (receivedChars[0] == '2') { // Code 2 requests the ESP01 sets it's own time to the current time
                                        // This request is used for displaying the time and date
      time_t t = time(nullptr);
      setTime(t);
      char buf[20];
      sprintf(buf,"%d-%02d-%02d %02d:%02d:%02d", year(), month(),
      day(), hour(), minute(), second());
      Serial.print (F("<"));
      for(int i=0;i<20;i++) {
        Serial.print(buf[i]);
      }
      Serial.print (F(">"));
    }
    else if (receivedChars[0] == '3') { // code 3 requests the IP address
      Serial.print(F("<"));
      for(int i=0;i<iplen;i++) { Serial.print(ip[i]); }
      Serial.print(F(">"));
    }
    else if (receivedChars[0] == '4') { // code 4  scans all available networks
      Serial.println(F("Scanning available networks..."));
      scanAllNetworks();
    }
    else if (receivedChars[0] == '5') { // reports the IP to the serial monitor
      Serial.print(F("Currently connected to: "));
      Serial.println(WiFi.SSID());
    }
    else if (receivedChars[0] == '6') { // reports the router's password to the serial monitor
      Serial.print(F("Password: "));
      Serial.println(myPassword);
    }
    else if (receivedChars[0] == '7') { // reports the router's ssid to the serial monitor
      Serial.print(F("SSID: "));
      Serial.println(ssid);
    }
    else if (receivedChars[0] == 'S') { 
      parseSettings();
    }
    else if (receivedChars[0] == 'M') { 
      ajaxReply(*receivedChars);
    }
    else if (receivedChars[0] == 'c') {
      ajaxReply(F("Calibration has been reset"));
    }
    else if (receivedChars[0] == 'u') {
      logPaused = false;
      ajaxReply(F("Logging has resumed"));
    }
    else if (receivedChars[0] == 'p') {
      logPaused = true;
      ajaxReply(F("Logging has been paused"));
    }
    else if (receivedChars[0] == 'd') {
      ajaxReply(F("The log has been deleted"));
    }
    else if (receivedChars[0] == 't') {
      ajaxReply(F("The device calibration has been reset"));
    }
    else if (receivedChars[0] == 'f') {
      ajaxReply(F("The device calibration could not be reset: device could not be reached."));
    }
    else if (receivedChars[0] == 'y') {
      ajaxReply(F("The location alarm is activated"));
    }
    else if (receivedChars[0] == 'z') {
      ajaxReply(F("The location alarm could not be activated: device could not be reached."));
    }
    newData = false;
  }
}


void parseSettings() {
  char myTimezone[4];
  char pausestate[2];
  short int t = 0;
  short int s = 0;
  for(short int i=1;i<sizeof(receivedChars);i++) {
    if (s == 0) { 
      if (receivedChars[i] == ':') { ssid[t] = '\0'; s++; t=0; }
      else { ssid[t] = receivedChars[i]; t++; }
    }
    else if (s == 1) {
      if (receivedChars[i] == ':') { myPassword[t] = '\0'; s++; t=0; }
      else { myPassword[t] = receivedChars[i]; t++; }
    }
    else if (s == 2) {
      if (receivedChars[i] == ':') { myTimezone[t] = '\0'; s++; t=0; } 
      else { myTimezone[t] = receivedChars[i]; t++; }
    }
    else if (s == 3) {
      if (receivedChars[i] == ':') { pausestate[t] = '\0'; s++; t=0; } 
      else { pausestate[t] = receivedChars[i]; t++; }
    }
    else if (s == 4) {   // The ESP is all colons, the ATMEGA \0
      if (receivedChars[i] == ':') { interval[t] = '\0'; s++; t=0; } 
      else { interval[t] = receivedChars[i]; t++; }
    }
    else { break; }
  }
  timezone = String(myTimezone).toInt(); // This evil String is neceassry to help easily convert negative numbers
  haveSettings = true;
}


void startPage() {
  server.sendHeader("Content-Type", "text/html");
  server.sendContent_P(header);
}


void middlePage(const __FlashStringHelper* msg3) {
  PGM_P p = reinterpret_cast<PGM_P>(msg3);
  unsigned char c = pgm_read_byte(p++);
  if (c != 0) { 
    server.sendContent(F("<div class=\"alert alert-info\" role=\"alert\" id=\"alertbox\">"));
    server.sendContent(msg3);
    server.sendContent(F("</div>"));
  }
  server.sendContent_P(afteralert);
}


void endPage() {
  char a[17] = "];var thisip = \"";   // 16 characters
  char b[18] = "\";var thisdev = \""; // 17 characters
  char e[20] = "\";var logpaused = \""; // 19 characters
  char f[19] = "\";var timezone = \""; // 18 characters
  char h[19] = "\";var interval = \""; // 18 characters
  char c[3] = "\";";  // 2 characters
  unsigned long l = 95 + sizeof(ip);
  char s[l];
  s[0] = '\0';
  strcat(s, a);
  strcat(s, ip);
  strcat(s, b);
  strcat(s, &currentDev);
  strcat(s, e);
  if (logPaused) { strcat(s, "1"); }
  else { strcat(s, "0"); }
  strcat(s, f);
  char g[4];  // 3 characters (-12)
  itoa(timezone, g, 10);
  strcat(s, g);
  strcat(s, h);
  strcat(s, interval);
  strcat(s, c);
  server.sendContent(s);
  server.sendContent_P(footer);
  server.client().stop();
}


void ajaxReply(const __FlashStringHelper* msg2) {
  server.sendHeader("Content-Type", "text/plain");
  server.sendContent(msg2);
  server.client().stop();
}


// Overloaded function
void ajaxReply(char &msg2) {
  server.sendHeader("Content-Type", "text/plain");
  server.sendContent(&msg2);
  server.client().stop();
}


void returnFail(const __FlashStringHelper* msg) {
  server.sendHeader("Connection", "close");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(500, "text/plain", msg);
}


void scanAllNetworks() {
  // scan for nearby networks:
  Serial.println(F("** Scan Networks **"));
  byte numSsid = WiFi.scanNetworks();
  // print the list of networks seen:
  Serial.print(F("SSID List:"));
  Serial.println(numSsid);
  // print the network number and name for each network found:
  for (int thisNet = 0; thisNet<numSsid; thisNet++) {
    Serial.print(thisNet);
    Serial.print(F(") Network: "));
    Serial.println(WiFi.SSID(thisNet));
  }
}


boolean handleTimeout(const __FlashStringHelper* msg, boolean ajax) {
  boolean dataErr = false;
  time_t timer = time(nullptr);
  while (!Serial.available()) { 
    time_t now = time(nullptr);  // Every action has a 10 second timeout period
    if (now > timer + 10) {
      if (ajax) { ajaxReply(msg); }
      else {
        startPage();
        middlePage(msg);
        endPage();
      }
      dataErr = true;
      break; 
    }
  }
  return dataErr;
}


void handleSubmit() {
  String Davalue = server.arg("ARG");
  currentDev = server.arg("ARG2")[0];
  String z = server.arg("ARG3");
  String li = server.arg("ARG4");
  timezone = z.toInt();
  if (Davalue == "1") {
    Serial.print(F("<c"));
    Serial.print(currentDev);
    Serial.print(F(">"));
    handleTimeout(F("Could not reset calibration: timeout"), true);
  }
  else if (Davalue == "2") {
    Serial.print(F("<l"));
    Serial.print(currentDev);
    Serial.print(F(">"));
    handleTimeout(F("Could not send locate signal: timeout"), true);
  }
  else if (Davalue == "3") {
    Serial.println("<e>");
    handleTimeout(F("Could not delete log: timeout"), true);
  }
  else if (Davalue == "4") {
    Serial.print("<w>");
    handleTimeout(F("Winbond chip not responding: timeout"), true);
  }
  else if (Davalue == "5") {
    Serial.print("<r>");
    handleTimeout(F("Logging could not be paused: timeout"), true);
  }
  else if (Davalue == "6") {
    Serial.print(F("<u"));
    Serial.print(z);
    Serial.print(F(">"));
    configTime(timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");
    ajaxReply(F("Timezone has been updated"));
  }
  else if (Davalue == "7") {
    Serial.print(F("<y"));
    Serial.print(li);
    Serial.print(F(">"));
    li.toCharArray(interval, li.length()+1);
    ajaxReply(F("Logging interval has been updated"));
  }
  else {returnFail(F("Bad value"));}
}


void handleRoot() {
  if (server.hasArg("ARG")) { handleSubmit(); }
  else {
    Serial.print("<a>");
    waitForData = true;
    if (handleTimeout(F("could not get data"), false)) { waitForData = false; }
  }
}


void handleAPSubmit() {
  String ss = APserver.arg("ARG");
  String pa = APserver.arg("ARG1");
  if (ss == "getipaddress") {
    if (WiFi.status() == WL_CONNECTED) {
      char * buf = new char[iplen + 74];
      strncpy_P(buf, (PGM_P)(F("You can now access this device by going to this address on the Internet: ")), 74);
      strcat(buf, ip);
      buf[iplen + 75] = '\0';
      APserver.send(200, "text/plain", buf);
      delay(100);
      delete buf;
    }
    else {
      APserver.send(200, "text/plain", F("The WiFi has not yet connected. Will try again in&nbsp;<span id=\"counter\"></span>&nbsp;seconds..."));
    }
  }
  else {
    APserver.send_P(200, "text/html", ap_thankyou);
    short int w = ss.length() + 1;
    short int x = pa.length() + 1;
    ss.toCharArray(ssid, w);
    pa.toCharArray(myPassword, x);
    Serial.print("<s");  // Save the SSID to the Windbond via the ATMEGA
    Serial.print(ssid);
    Serial.print(">");
    delay(500);
    Serial.print("<p");     // Save the password to the Windbond via the ATMEGA
    Serial.print(myPassword);
    Serial.print(">");
    triedServer = false;
  }
}


void handleAPRoot() {
  if (APserver.hasArg("ARG")) { handleAPSubmit(); }
  else {
    APserver.send_P(200, "text/html", ap_welcome);
  }
}


void handleNotFound() { 
  server.send ( 404, "text/plain", "File Not Found\n\n" ); 
}


// This function parses Serial data sent by the ATMEGA328P
void recvData() {
  static byte ndx = 0;
  char startMarker = '<';  // The start and end markers are used to delineate the data
  char endMarker = '>';
  char rc;
  if (waitForData) { startPage(); middlePage(F("")); }
  time_t timer = time(nullptr);
  while ((Serial.available() > 0 && newData == false) || (recvInProgress)) {
    time_t now = time(nullptr);
    if (now > timer + 30) { recvInProgress = false; break; }
    if (Serial.available()) {
      rc = Serial.read();
      if (recvInProgress == true) {
        if (rc != endMarker) {  // Wait for the end marker
          if (waitForData) {
            if (rc == 'n') { server.sendContent("\",\""); }
            else if (rc == 'z') { server.sendContent("\""); }
            else { server.sendContent(String(rc)); }  // Evil String required
          }
          else { receivedChars[ndx] = rc; }
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
      else if (rc == startMarker) {
        recvInProgress = true;  // Keep going
        if (waitForData) { server.sendContent("\""); }
      }
    }
  }
  if (waitForData) { endPage(); waitForData = false; }
}


void startAP() {
  char NRValue[32];
  String(ESP.getChipId()).toCharArray(NRValue,8);
  strcat(NRValue, "SensiPotHub");
  //Start HOTspot removing password will disable security
  WiFi.softAP(NRValue, "SensipotAdmin123"); 
  IPAddress myIP = WiFi.softAPIP(); //Get IP address
  Serial.print(F("< HotSpt IP: "));
  Serial.print(myIP);
  Serial.print(F(">"));
  Serial.print(F("< SSID: SensiPotHub>"));
  Serial.print(F("< Password: SensipotAdmin123>"));
  APserver.on ( "/", handleAPRoot );
  APserver.begin();
}


boolean startServer () {
  Serial.print(F("< Attempting to authenticate>"));
  Serial.print(F("< SSID: "));
  Serial.print(ssid);
  Serial.print(F(">< Password: "));
  Serial.print(myPassword);
  Serial.print(F(">"));
  // Values must be of type char before sending to Wifi.begin
  WiFi.disconnect(); 
  WiFi.begin ( ssid, myPassword );
  unsigned long startMillis = millis();
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 1000 );
    unsigned long currentMillis = millis();
    if ((currentMillis - startMillis) > 10000) {
      Serial.println(F("< Wifi connection timed out>"));
      break; 
    }
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("< Connected!!\n>"));
    // We fetch the current time. -7 is for U.S. timezone
    // Change as needed. plus 12 timezones going east from 
    // Greenwich, negative 12 timezones going west
    configTime(timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");  
    Serial.print(F("< Timezone: "));
    Serial.print(timezone);
    Serial.print(F(">"));
    while (!time(nullptr)) {delay(1000);}
    iplen = WiFi.localIP().toString().length() + 1;
    WiFi.localIP().toString().toCharArray(ip, iplen);
    Serial.print(F("< Current ip: "));
    Serial.print(ip);
    Serial.print(F(">"));
    server.on ( "/", handleRoot );
    server.onNotFound ( handleNotFound );
    server.begin();
    Serial.print(F("<h>")); // Command signifies to ATMEGA that we have finished booting
    return true;
  } 
  else { return false; }
}
