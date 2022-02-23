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

volatile byte piezo_state = LOW;    // fade state (goes high when drop detected)
int inPin = 2;       // interrupt pin for piezo
int ledPin = 13;    
int brightness = 0;    // Set to max (255 is LED OFF - may need to be changed depending on driver)
int fadeAmount = 1;

#define ON 0            // define fader ON/OFF
#define OFF 1
unsigned long previousFadeMillis;   // timers
int fadeInterval = 2;      // fade speed
byte fader = OFF;       // State Variable for fader ON/OFF

int fade_count = 31;
const uint8_t CIEL8[] = {             // lookup table
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
const unsigned int inPort = 8888;
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
String colours[4] = {"red", "green", "blue", "yellow"};
                            
// array of planet states (which colour are they) all set to "none" for game start
// ( red = 1, green = 2, blue = 3, yellow = 4 )
int states[] = {0, 0, 0, 0, 0, 0, 0, 0};

// setup buttons on pins:
ezButton button1(5);
ezButton button2(6);

void setup() {
  
  digitalWrite(ledPin, LOW);    // LED off
  
  Ethernet.begin(mac,ip);
  Udp.begin(8888);
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

  receiveOSC();

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
  if (piezo_state == HIGH) {
    sendOSC("/piezo", 1);
    fade_count = 0;
    brightness = 255;  // PWM 0 = LED Off
    fader = ON;
    piezo_state = LOW;    // reset interrupt state
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

// do an average of ?? many samples here before passing result to findColur function?
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
      brightness = 255-CIEL8[fade_count];
      fade_count++;
      // stop after max number of steps (31)
      if (fade_count >= 31) {
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
void findColour(int r, int g, int b, int num) {

//  for (int i = 0; i < samplesCount; i++) {
    //normalise values for each color vs each other
    float nr = r*1.0/(r+g+b);
    float ng = g*1.0/(r+g+b); 
    float nb = b*1.0/(r+g+b);

    if (nr > 0.4 && ng< 0.33) {
      if (states[num] != 1) {       // check previous state of hole and update if its now red
        String x = "red"+String(num, DEC);
        sendOSC(x,0);
        // set state value for this hole to 1 (red=1,green=2,blue=3,yellow=4)
        states[num] = 1;        
      }
    }
    if (ng > 0.38 && nr < 0.33 && nb < 0.29) {
      if (states[num] != 2) {       // check previous state of hole and update if its a new colour            
        String y = "green"+String(num, DEC);
        sendOSC(y,0);
        states[num] = 2;    
      }               
    }
    if (nb > 0.34 && nr < 0.3) {
      if (states[num] != 3) {       // check previous state of hole and update if its a new colour         
        String z = "blue"+String(num, DEC);        
        sendOSC(z, 0); 
        states[num] = 3;      
      }                      
    } 
//    if (nr > 0.35 && nb < 0.27 && ng > 0.35) {
//      if (states[num] != 4) {       // check previous state of hole and update if its a new colour  
//        String a = "yellow"+String(num, DEC);        
//        sendOSC(a, 0);
//        states[num] = 4;  
//      }                 
//    }       
}

// interrupt service routine for piezo fader state
void changeLED() {
  piezo_state = HIGH;
}

// work out winner:
void winner() {
  int s = 0;
  for (int i=0; i<sizeof(states); i++) {
    s += states[i];               // sum values for all holes (red=1,green=2,blue=3,yellow=4)
  }
  if (s == 0) {
    // if zero then nobody scored...
    sendOSC("/no_score", 0);
    Serial.println("No Winner");
    return;                     // exit function
  }

  // calculate total for each colour (r,g,b,y)
  int game_winner [] = {0,0,0,0};
  for (int i=0; i<sizeof(states); i++) {
    if (states[i] == 1) {
      game_winner[0]+=1;
    }
    if (states[i] == 2) {
      game_winner[1]+=1;
    }
    if (states[i] == 3) {
      game_winner[2]+=1;
    }
    if (states[i] == 4) {
      game_winner[3]+=1;
    }
  }

  // work out which is highest (or if a draw)
  int maxIndex = 0;
  int maxValue = 0;
  for (int i=0; i<sizeof(game_winner); i++)
  {
    if (game_winner[i] > maxValue) {
        maxValue = game_winner[i];
        maxIndex = i;
    }
  }
  // check for a draw
  int draw = 0;
  for (int i=0; i<sizeof(game_winner); i++)
  {
    // check how many colours had the same high score
    if (game_winner[i] == maxValue) {      // check for number of instances of the max value
      draw += 1;
    }
  }
  if (draw > 1) {
    // if more than its a draw
    sendOSC("/draw", 0);
    Serial.println("Draw");
    return;                  // exit function
  }
  
  // send winner
  sendOSC(colours[maxIndex], maxValue);
  Serial.print("Winner: ");
  Serial.println(colours[maxIndex]);
  return;
}

//reads and dispatches the incoming OSC
void receiveOSC() {
  OSCMessage msg;
  int size = Udp.parsePacket();

  if (size > 0) {
    while (size--) {
      msg.fill(Udp.read());
    }
    if (!msg.hasError()) {
      msg.dispatch("/winner", winner);
    } else {
      Serial.print("error: ");
    }
  }
}
