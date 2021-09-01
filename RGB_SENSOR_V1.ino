#include <Wire.h>
#include "Adafruit_TCS34725.h"
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    
#include <OSCMessage.h>
#include <OSCBundle.h>

//////////////////// Missed marble piezo sensor lighting:

volatile byte state = LOW;    // fade state (goes high when drop detected)
int inPin = 2;       // interrupt pin for piezo
int ledPin = 13;    
int fadeValue = 255;    // Set to max (255 is LED OFF - may need to be changed depending on driver)

#define ON 0            // define fader ON/OFF
#define OFF 1
byte fadeIncrement = 5;       // fade smoothness
unsigned long previousFadeMillis;   // timers
int fadeInterval = 10;      // fade speed
byte fader = OFF;       // State Variable for fader ON/OFF

//////////////////////  Network setup:

EthernetUDP Udp;
//the Arduino's IP
IPAddress ip(192, 168, 0, 101);
//destination IP
IPAddress outIp(192, 168, 0, 100);
const unsigned int outPort = 9999;
byte mac[] = {  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED }; // you can find this written on the board of some Arduino Ethernets or shields

///////////////////////  Colour detection:   look at gain and integration time settings....

byte multiAddress = 0x70;

Adafruit_TCS34725 tcs[] = {Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X)};

byte gammatable[256];
byte arraySize = 4;   // number of colour sensors
// data array for colours from each sensor
uint16_t data[4][4] = {{0, 0, 0, 0},
                       {0, 0, 0, 0},
                       {0, 0, 0, 0},
                       {0, 0, 0, 0}};


//////////////////////////  OSC output messages:

String colour = "none";
// array of colour messages to send over OSC (indexed by order - yellow = 0, red = 1 etc)
String colours[4][5] = {{"none", "blue", "red", "green", "white"},
                        {"none", "blue", "red", "green", "white"},
                        {"none", "blue", "red", "green", "white"},
                        {"none", "blue", "red", "green", "white"}};
                            
// array of planet states (which colour are they) all set to "none" for game start
int winners[] = {0, 0, 0, 0, 0, 0, 0, 0};


void setup() {
  
  digitalWrite(ledPin, HIGH);    // LED off
  
  Ethernet.begin(mac,ip);
  Udp.begin(8888);
  Serial.begin(115200);
  Wire.begin();
  
  // gammatable for more color accuracy when outputting on LED
  for(int i =0; i < 256; i++){
      float x = i;
      x /= 255;
      x = pow(x, 2.5);
      x *= 255;
      gammatable[i] = x;
  }

  initColorSensors();     // start sensors
  // piezo trigger interrupt:
  attachInterrupt(digitalPinToInterrupt(inPin), changeLED, RISING);
  
}

void loop(void) {

  //start timer
  unsigned long currentMillis = millis();
  
  // start black hole led fader if interrupt detected
  if (state == HIGH) {
    sendOSC("m", 1);
    fader = ON;
    fadeValue = 0;  // PWM 0 = LED ON
    state = LOW;    // reset interrupt state
  }
 
 // need to move this outside loop - or use interrupts for RGB detection
  doTheFade(currentMillis);

  // loop through all sensors and put rgb values in data array
  for(int i = 0; i < arraySize; i++){ // get all colors... not necessary right now 
      readColors(i);
      // do color detection and update array with new values
      colourDetect(i);
  }

}


void initColorSensors(){                  // happens once in setup
    for(int i = 0; i < arraySize; i++){
        Serial.println(i);
        chooseBus(i);
        if (tcs[i].begin()){
            Serial.print("Found sensor "); Serial.println(i+1);
        } else{
            Serial.println("No Sensor Found");
            while (true);
        }
    }
}

void readColors(byte sensorNum){
    chooseBus(sensorNum);
    uint16_t r, g, b, c, lux;
    tcs[sensorNum].getRawData(&r, &g, &b, &c); // reading the rgb values 16bits at a time from the i2c channel
    lux = tcs[sensorNum].calculateLux(r, g, b);
    processColors(r, g, b, c); // processing by dividng by clear value and then multiplying by 256
    data[sensorNum][0] = r;
    data[sensorNum][1] = g;
    data[sensorNum][2] = b;
    data[sensorNum][3] = lux;

    //Serial.print(b, DEC); Serial.print(" "); Serial.print(r, DEC); Serial.print(" "); Serial.print(g, DEC); Serial.print(" "); Serial.println(lux, DEC);

}

void processColors(uint16_t r, uint16_t g, uint16_t b, uint32_t c){
        // getting rid of IR component of light
       r /= c;
       g /= c;
       b /= c; 
       // adding it back in 
       r *= 256;
       g *= 256;
       b *= 256;
}

void chooseBus(uint8_t bus){
    Wire.beginTransmission(0x70);
    Wire.write(1 << (bus+4)); // will be using 2-7 instead of 0-5 because of convience (placed better on the breadboard)
    Wire.endTransmission();
}

// 

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

// check to see if a particular colour has been detected from the data array
void colourDetect(byte sensorNum) {
  
  if ( data[sensorNum][1]>=350 && data[sensorNum][0]<=450 && data[sensorNum][2]<=270){
  winners[0] = 3;     // set first planet colour in winners array (3 is index for "green")                       
  sendOSC("g", sensorNum);
  }
  
  else if ( data[sensorNum][0]>=400 && data[sensorNum][1]<=450 && data[sensorNum][2]<=270){  
  winners[0] = 2;    
  sendOSC("r", sensorNum);
  }

  else if ( data[sensorNum][2]>=260 && data[sensorNum][1]<=450 && data[sensorNum][0]<=450){
  winners[0] = 1;                             
  sendOSC("b", sensorNum);
  }
  
  else if ( data[sensorNum][3]>=850){  
  winners[0] = 4;                          
  sendOSC("w", sensorNum);  
}
else {
}
}


// interrupt service routine for piezo fader state
void changeLED() {
  state = HIGH;
}
