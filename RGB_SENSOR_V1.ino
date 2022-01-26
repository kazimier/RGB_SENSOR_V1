#include <Wire.h>
#include "Adafruit_TCS34725.h"
#include <Adafruit_NeoPixel.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>    
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <avr/pgmspace.h>
#include <ezButton.h>


//////////////////// Missed marble piezo sensor lighting:

volatile byte state = LOW;    // fade state (goes high when drop detected)
int inPin = 2;       // interrupt pin for piezo
int ledPin = 13;    
int brightness = 0;    // Set to max (255 is LED OFF - may need to be changed depending on driver)
int fadeAmount = 1;

#define ON 0            // define fader ON/OFF
#define OFF 1
unsigned long previousFadeMillis;   // timers
int fadeInterval = 2;      // fade speed
byte fader = OFF;       // State Variable for fader ON/OFF

int count = 31;

const uint8_t CIEL8[] = {          // lookup table
0,    1,    2,    3,    4,    5,    7,    9,    12,
15,    18,    22,    27,    32,    38,    44,    51,    58,
67,    76,    86,    96,    108,    120,    134,    148,    163,
180,    197,    216,    235,    255
};

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

Adafruit_TCS34725 tcs[] = {Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),                           
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X),                           
                           Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_2_4MS, TCS34725_GAIN_4X)};

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
byte arraySize = 8;   // number of colour sensors

//////////////////////////  OSC output messages:

// array of colour messages to send over OSC (indexed by order - yellow = 4, red = 1 etc)
String colours[4] = {"red", "green", "blue", "yellow"};;
                            
// array of planet states (which colour are they) all set to "none" for game start
// ( red = 1, green = 2, blue = 3, yellow = 4 )
int states[] = {0, 0, 0, 0, 0, 0, 0, 0};

// setup buttons on pins:
ezButton button1(5);
ezButton button2(6);

void setup() {
  
  digitalWrite(ledPin, LOW);    // LED off
  
  Ethernet.begin(mac,ip);
  Udp.begin(8000);
  Serial.begin(115200);
  Wire.begin();

  initColorSensors();     // start sensors
  
  // piezo trigger interrupt:
  attachInterrupt(digitalPinToInterrupt(inPin), changeLED, RISING);
  // set button debounce times
  button1.setDebounceTime(50); // set debounce time to 50 milliseconds
  button2.setDebounceTime(50); // set debounce time to 50 milliseconds
}

void loop(void) {

  button1.loop(); // MUST call the loop() function first
  button2.loop(); // MUST call the loop() function first
  // add reset button to restart game
  // add ambient button to enter ambient mode
  int btn1State = button1.getState();
  int btn2State = button2.getState();
  if(button1.isPressed()) {
    sendOSC("/Reset/", 1);
  }    
  if(button2.isPressed()) {
    sendOSC("/Ambient/", 1);
  }
  //start timer
  unsigned long currentMillis = millis();

  // start black hole led fader if interrupt detected
  if (state == HIGH) {
    sendOSC("/piezo", 1);
    count = 0;
    brightness = 255;  // PWM 0 = LED Off
    fader = ON;
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
    Wire.write(1 << bus); // will be using 2-7 instead of 0-5 because of convience (placed better on the breadboard)
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
}

void doTheFade(unsigned long thisMillis) {
  // is it time to update yet?
  // if not, nothing happens
  if (thisMillis - previousFadeMillis >= fadeInterval) {
    // yup, it's time!  
    if (fader == ON) {
      brightness = 255-CIEL8[count];
      count++;
      Serial.println(count);
      // stop after max number of steps (31)
      if (count >= 31) {
        brightness = 0;     // LED OFF
        fader = OFF;
      }
    }
    // Only need to update when it changes
    // set the brightness of pin 9:, 0-31, 5 bit steps of brightness
    analogWrite(ledPin, brightness);
    //analogWrite(ledPin, fadeValue);  
    // reset millis for the next iteration (fade timer only)
    previousFadeMillis = thisMillis;
  }
}


// check to see if a particular colour has been detected from the SAMPLES array
void findColour(int r, int g, int b, int count) {

//  for (int i = 0; i < samplesCount; i++) {
    //normalise values for each color vs each other
    float nr = r*1.0/(r+g+b);
    float ng = g*1.0/(r+g+b); 
    float nb = b*1.0/(r+g+b);
    
    if (nr > 0.4 && ng< 0.33) {
      if (states[count] != 1) {       // check previous state of hole and update if its now red
        String x = "red"+String(count, DEC);
        sendOSC(x,0);
        states[count] = 1;        
      }
    }
    if (ng > 0.38 && nr < 0.33 && nb < 0.29) {
      if (states[count] != 2) {       // check previous state of hole and update if its a new colour            
        String y = "green"+String(count, DEC);
        sendOSC(y,0);
        states[count] = 2;    
      }               
    }
    if (nb > 0.34 && nr < 0.3) {
      if (states[count] != 3) {       // check previous state of hole and update if its a new colour         
        String z = "blue"+String(count, DEC);        
        sendOSC(z, 0); 
        states[count] = 3;      
      }                      
    } 
    if (nr > 0.35 && nb < 0.27 && ng > 0.35) {
      if (states[count] != 4) {       // check previous state of hole and update if its a new colour  
        String a = "yellow"+String(count, DEC);        
        sendOSC(a, 0);
        states[count] = 4;  
      }                 
    }       
}

// interrupt service routine for piezo fader state
void changeLED() {
  state = HIGH;
}

// work out winner:
void winner() {
  int winner [] = {0,0,0,0};
  for (int i=0; i<8; i++) {
    if (states[i] == 1) {
      winner[i]+=1;
    }
    if (states[i] == 2) {
      winner[i]+=1;
    }
    if (states[i] == 3) {
      winner[i]+=1;
    }
    if (states[i] == 4) {
      winner[i]+=1;
    }
  }
  byte maxIndex = 0;
  int maxValue = winner[maxIndex];

  for(byte i = 0; i < 4; i++)
  {
    if(winner[i] > maxValue) {
        maxValue = winner[i];
        maxIndex = i;
    }
  }
  // send OSC and reset counters
  sendOSC(colours[maxIndex], 1);
  int states[] = {0, 0, 0, 0, 0, 0, 0, 0};
}
