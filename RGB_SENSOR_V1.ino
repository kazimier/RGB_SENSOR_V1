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

Adafruit_TCS34725 tcs[] = {Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_4X)};

const int SAMPLES[5][3] = { // Values from colour training (averaged raw r, g and b; actuator movement)
  {71, 22, 14},
  {3200, 800, 700},
  {3400, 5600, 4000},
  {1000, 2100, 3300},
  {8000, 10000, 9000},
};          // rows correspond to none, red, green, blue, white...

byte foundColour[] = {0, 0, 0, 0, 0, 0, 0, 0};

bool sensorTriggered = false; // Sample present yes or no
byte samplesCount = sizeof(SAMPLES) / sizeof(SAMPLES[0]); // Determine number of samples in array
byte arraySize = 4;   // number of colour sensors

//////////////////////////  OSC output messages:

String colour = "none";
// array of colour messages to send over OSC (indexed by order - yellow = 0, red = 1 etc)
String colours[5] = {"none", "red", "green", "blue", "white"};;
                            
// array of planet states (which colour are they) all set to "none" for game start
int winners[] = {0, 0, 0, 0, 0, 0, 0, 0};


void setup() {
  
  digitalWrite(ledPin, HIGH);    // LED off
  
  Ethernet.begin(mac,ip);
  Udp.begin(8000);
  Serial.begin(115200);
  Wire.begin();

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
    uint16_t r, g, b, c;
    tcs[sensorNum].getRawData(&r, &g, &b, &c); // reading the rgb values 16bits at a time from the i2c channel
    
    findColour(r, g, b, sensorNum);
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
  delay(5);
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

// check to see if a particular colour has been detected from the SAMPLES array

// is a running mean (or max from last 3 values...) of raw data useful here to stop a trigger of one colour immediately followed by another???

void findColour(int r, int g, int b, int count) {

  for (int i = 0; i < samplesCount; i++) {
    //normalise values for each color vs each other
    float nr = r*1.0/(r+g+b);
    float ng = g*1.0/(r+g+b); 
    float nb = b*1.0/(r+g+b);

  if (count == 3) {      
    if (nr > 0.4) {
      sendOSC("red", count);
      Serial.println("red");
    }
    if (ng > 0.38 && nr < 0.32) {
      sendOSC("green", count);  
      Serial.println("green");          
    }
    if (nb > 0.4 && nr < 0.3) {
      sendOSC("blue", count); 
      Serial.println("blue");           
    }    
    }
  //Serial.print(count); Serial.print(" ");Serial.println(foundColour[count]);
  }
}

// interrupt service routine for piezo fader state
void changeLED() {
  state = HIGH;
}
