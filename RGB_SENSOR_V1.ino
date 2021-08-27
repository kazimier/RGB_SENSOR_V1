#include <Wire.h>
#include "Adafruit_TCS34725.h"
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    
#include <OSCMessage.h>
#include <OSCBundle.h>

//////////////////// Missed marble piezo sensor lighting:

int piezo = A0;
int ledPin = 13;    
int fadeValue = 255;    // Set to max (255 is LED OFF - may need to be changed depending on driver)
#define ON 0            // define fader ON/OFF
#define OFF 1
byte fadeIncrement = 5;       // fade smoothness
unsigned long previousFadeMillis;   // timers
int fadeInterval = 10;      // fade speed
byte fader = OFF;       // State Variable for fader ON/OFF
int threshold = 300;      // trigger threshold for piezo (0-1023)

//////////////////////  Network setup:

EthernetUDP Udp;
//the Arduino's IP
IPAddress ip(192, 168, 0, 101);
//destination IP
IPAddress outIp(192, 168, 0, 100);
const unsigned int outPort = 9999;

///////////////////////  Colour detection:

Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X);

 byte mac[] = {  
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // you can find this written on the board of some Arduino Ethernets or shields
String colour = "none";

// array of colour messages to send over OSC (indexed by order - yellow = 0, red = 1 etc)
String colours[] = {"none", "blue", "red", "green", "white"};
// array of planet states (which colour are they) all set to "none" for game start
int winners[] = {0, 0, 0, 0, 0, 0, 0, 0};

void setup() {
  
  digitalWrite(ledPin, HIGH);    // LED off
  
  Ethernet.begin(mac,ip);
  Udp.begin(8888);
    
  Serial.begin(115200);
  
  if (tcs.begin()) {
    Serial.println("Found sensor");
  } else {
    Serial.println("No TCS34725 found ... check your connections");
    while (1);
  }
  
  // Now we're ready to get readings!
}

void loop(void) {

  //start timer
  unsigned long currentMillis = millis();
  // read piezo input
  int sensorValue = analogRead(piezo);
  // print out the value you read:
  //Serial.println(sensorValue);

  // start black hole led fade
  if (sensorValue > threshold) {
      Serial.println("trigger");
    fader = ON;
    fadeValue = 0;  // PWM 0 = LED ON
  }

  doTheFade(currentMillis);

  // check sensor and update string indicating which colour is detected
  colourDetect();

  // send OSC message once if a colour is read by sensor - minimise number of messages!
  if (colour != "none"){
  sendOSC(colour, 1);
  }
  // reset colour to "none" so no more messages sent
  colour = "none";
}

void colourDetect() {
    uint16_t r, g, b, c, colorTemp, lux;
  tcs.getRawData(&r, &g, &b, &c);
  colorTemp = tcs.calculateColorTemperature(r, g, b);
  lux = tcs.calculateLux(r, g, b);
  
  //Serial.print(b, DEC); Serial.print(" "); Serial.print(r, DEC); Serial.print(" "); Serial.println(g, DEC);
  Serial.println(lux, DEC);

  if ( g>=350 && r<=450 && b<=270){
  winners[0] = 3;     // set first planet colour in winners array (3 is index for "green")                       
  colour = "green";
  Serial.println("green");
  }
  
  if ( r>=400 && g<=450 && b<=270){  
  winners[0] = 2;    
  colour = "red";                    
  Serial.println("red");
  }

  if ( b>=260 && g<=450 && r<=450){
  winners[0] = 1;                             
  colour = "blue";
  Serial.println("blue");
  }
  
  if ( lux>=750){  
  winners[0] = 4;                          
  colour = "white";
  Serial.println("white");  
}
}

void sendOSC(String msg, unsigned int data) {

  OSCMessage msgOUT(msg.c_str());
  msgOUT.add(data);
  Udp.beginPacket(outIp, outPort);
  msgOUT.send(Udp);
  Udp.endPacket();
  msgOUT.empty();
  delay(10);
}

void doTheFade(unsigned long thisMillis) {
  // is it time to update yet?
  // if not, nothing happens
  if (thisMillis - previousFadeMillis >= fadeInterval) {
    // yup, it's time!
    if (fader == ON) {
      fadeValue = fadeValue + fadeIncrement;  
      if (fadeValue >= 255) {
        // At max, limit and change direction
        fadeValue = 255;     // LED OFF
        fader = OFF;
      }
    }
    // Only need to update when it changes
    analogWrite(ledPin, fadeValue);  
 
    // reset millis for the next iteration (fade timer only)
    previousFadeMillis = thisMillis;
  }
}
