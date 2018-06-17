/*
   This is proof-of-concept code, only to show how you might control the Z-cam E1 with serial communications on its UART.

   This code is utterly NOT quality assured. Don't be surprised if there are bugs and errors. I would be surprised if there aren't any ;-)
   There's little to no error checking, result codes and responses from the camera are regularly thrown away without checking them, and so on and so forth...
   No optimization or attempts to write pretty code have been made. This is truly ugly-hack level code :-)

   Tested on a Teensy 2.0 - https://www.pjrc.com/teensy/
   The Teensy is not a pure clone of the Arduino, but it is a very close relative.
   You use the regular Arduino IDE, with PJRC's additional Teensyduino, for coding and programming it.
   It has a hardware UART, that you reach with the Serial1.X(...) functions.
   I think the closest Arduino counterpart to the Teensy 2.0 is the Leonardo.
   For all practical purposes, the Teensy can be seen as an Arduino, but some specs, pin names and numbers may differ.

   I have only used an Olympus 25/1.8 lens for testing, while writing this sketch.

   What this sketch can do:
    Simulate an i2c EEPROM, to make the camera configure itself for UART mode.
    At start-up, it waits for the camera to come alive.
    At start-up, it sets the camera to Stills mode.
    At start-up, it sets focus mode to AF.
    At start-up, it focuses the lens to infinity.
    Press button 0 to toggle between MF and AF.
    Press button 1 to toggle playback on and off. While in playback, buttons 2 and 3 goes to the next and previous file, respectively.
    Press button 2 to increase flash delay.
    Press button 3 to decrease flash delay.
    Press button 7 to take a picture and fire an external flash.
    Blinks the on-board LED in different patterns, to show status.
    Take analog input (e.g. from a potentiometer) and focus the lens based on the analog value.
    Reads PWM input from a model car/boat Radio Control receiver, changes focus and takes pictures or starts/stops movie recording based on PWM pulse width.
    
    @Author: Ragnar Jensen

*/
#define HWUART  Serial1
#include <Wire.h>

const int           ledPin = 11;            // Teensy 2.0
const int           pwmpin = 9;             // Teensy 2.0.
const int           flashPin = 13;          // B4
const int           pwmInPin1 = 14;         // B5
const int           pwmInPin2 = 15;         // B6

const unsigned int  infinity = 0x009b;      // Hardcoded value representing infinity on my Olympus 25/1.8 lens.
const unsigned int  closefocus = 0x0484;    // Ditto, for closest focus.
//const unsigned int  infinity = 0x7fff;    // Hardcoded value representing infinity on my Olympus 25/1.8 lens.
//const unsigned int  closefocus = 0xffff;  // Ditto, for closest focus.


unsigned int        span;

unsigned int        focusTo, oldfocusTo;
unsigned int        flashDelay;             // How many milliseconds to wait between a CAPTURE command and firing the flash.

float               stepsize;
int                 focusStepSize = 5;    // How much analog input should change, before we actually take action and refocus.
                                          // Set to more than 1, to avoid refocusing if there is jitter on the analog input.
                                          // The downside is that the number of focus steps gets smaller and the step size gets larger.
                                          // On the other hand, the number of actual, physical focus positions the lens has is probably a fair bit smaller yet.
                                          // I get some jitter just by touching the metal shield on the potentiometer...
                                       
float               flashStepSize = 1;                                          

int                 focusSpeed = 2;       // How fast the lens should move. Camera's default is 1, which is fairly slow (good for those smoooooth focus pulls in video).
                                          // I have tested up to 10, the upper limit is supposedly 32.
float               focusValue;

int                 pwmFocusStepSize=10;    // How much the pulse width (in microseconds)on PWM input has to change, before we actually re-focus the lens.
int                 pwmInValue1, oldpwmInValue1;
int                 pwmInValue2, oldpwmInValue2;

// Used for converting 32 bit integers to byte array and vice versa.
union ArrayToInteger {
  byte focusPosition[4];
  uint32_t integer;
};

ArrayToInteger converter = {0, 0, 0, 0x9b}; // Create a converter between 32-bit integer and byte array.
                                            // Initialize with value for infinity, just to have a sane value in there when sketch starts.


// Arrays of bytes, that make up commands to send to the camera.
// To send commands to the camera, we just push out one of these arrays onto the UART with HWUART.write()
byte    switchToMovie[] = {0xea, 0x02, 0x01, 0x02};
byte    switchToStill[] = {0xea, 0x02, 0x01, 0x03};
byte    switchToPB[]    = {0xea, 0x02, 0x01, 0x04};
byte    startRec[]      = {0xea, 0x02, 0x01, 0x05};
byte    stopRec[]       = {0xea, 0x02, 0x01, 0x06};

byte    initPB[]        = {0xea, 0x02, 0x01, 0x21};   // Start playback
byte    nextPB[]        = {0xea, 0x02, 0x01, 0x22};   // Playback next file.
byte    prevPB[]        = {0xea, 0x02, 0x01, 0x23};   // Playback previous file.

byte    switchToMF[]    = {0xea, 0x02, 0x04, 0x0e, 0x16, 0x01, 0x00};
byte    switchToAF[]    = {0xea, 0x02, 0x04, 0x0e, 0x16, 0x01, 0x01};
byte    setFocusSpeed[] = {0xea, 0x02, 0x07, 0x0e, 0x35, 0x02, 0x00, 0x00, 0x00, 0x01}; // Last byte (bytes?) hold(s) focus speed, camera's default is 1.
/*
   These next three arrays hold commands to set the focus position at different points.
   I got the values for the first two (infinity and close focus) by simply performing AF operations by hand, with the camera's buttons,
   and then sending a GET_LENS_FOCUS_POSITION command to the camera.

   It is very possible that the focus point values for infinity and closest focus are different with other lenses. Someone really ought to test that ;-)
   The focus point is held in the last four bytes of the arrays. I have not seen anything but 0x00 in the first two of those four bytes,
   making me think that the position is represented by a 16-bit integer held in the last two bytes. Someone really ought to test that, too ;-)
*/
byte    focusToInf[]     = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x00, 0x9b};    // The four last bytes are the "inifinity" lens position of my Olympus 25/1.8 lens.
byte    focusToClose[]   = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x04, 0x84};    // Ditto, for when the lens is focused as close as it can.

//byte    focusToInf[]     = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x7f, 0xff};  // The four last bytes are the "inifinity" lens position of my Olympus 25/1.8 lens.
//byte    focusToClose[]   = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0xff, 0xff, 0xff, 0xff};  // Ditto, for when the lens is focused as close as it can.


byte    focusToX[]       = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x00, 0x9b};    // We manipulate the two last bytes of this array, with values derived from the potentiometer's position.

byte    capture[]        = {0xea, 0x02, 0x01, 0x07}; // Command string to take a picture.

byte    getLensFocusPosition[] = {0xea, 0x02, 0x02, 0x0f, 0x34};

byte    *buf;
int     buflen = 7;           // Number of bytes in buffer, varies depending on command.
byte    responseBuf[64];      // Responses from camera go into this buffer. I have no idea whether 64 bytes is enough...
size_t  responseLen = 0;

unsigned int focusPosition;   // Not used to it's full potential, yet... I just stuff fixed values into it for now.

bool    ledMode = HIGH;
bool    flashMode = LOW;
bool    focusModeIsAF = true;     // Is camera set to MF or AF?
bool    focusToInfinity = true;   // Holds direction to slew focus when button 1 is pressed.

bool    stillsMode = true;        // Is camera set to stills or movie mode?
bool    isRecording = false;      // In movie mode, are we recording?

int analogFocusPot;               // Values from potentiometer on pin A0.
// =================================================================================================================

void setup() {
  Serial.begin (9600); // Console

  Serial.println("INIT: Start");
  
  
  span = closefocus - infinity;             // How many steps there are between infinity and closest focus, as an integer.
  stepsize = (float)span / 1024;            // Anlog values range 0 - 1023. stepsize is how much to change the focus position value when the analog value changes by 1.
                                            // With my Olympus 25/1.8 lens' values for infinity and close focus span is 1001, stepsize ends up being 0.98.
  Serial.print("infinity: ");
  Serial.println(infinity);
  Serial.print("closefocus: ");
  Serial.println(closefocus);
  Serial.print("span: ");
  Serial.println(span);
  Serial.print("stepsize: ");
  Serial.println(stepsize);

  // Simulate eeprom on 0x51
  Serial.println ("INIT: i2c: Listen on 0x51");
  Wire.begin(0x51);
  Wire.onRequest(requestEvent);

  // Set up UART communication to camera.
  Serial.println("INIT: UART");
  HWUART.begin(115200);

  // Buttons on digital input pins.
  Serial.println("INIT: Buttons");
  pinMode(PIN_B0, INPUT_PULLUP);  // Switch focus mode.
  pinMode(PIN_B1, INPUT_PULLUP);  // Focus to near or far end.
  pinMode(PIN_B2, INPUT_PULLUP);  // Increase flash delay.
  pinMode(PIN_B3, INPUT_PULLUP);  // Decrease flash delay.
  pinMode(PIN_B7, INPUT_PULLUP);  // Shutter.

  Serial.println("INIT: FLASH TRIGGER");
  flashDelay = 148;   // Lots of trial and error to find this... It may very well be different for your camera and/or flash.
  Serial.printf("Flash delay: %i ms\n", flashDelay);
  pinMode(flashPin, OUTPUT);
  Serial.print("flashspan: ");
  Serial.print("flashstepsize: ");
  Serial.println(flashStepSize);
  Serial.println("INIT: LED");
  pinMode(ledPin, OUTPUT);
  Serial.println("INIT: PWM input");
  pinMode(pwmInPin1, INPUT);   // Read PWM pulses from RC receiver
  pinMode(pwmInPin2, INPUT);   // Read PWM pulses from RC receiver
  oldpwmInValue1 = pwmInValue1 = map(pulseIn(pwmInPin1, HIGH),1000,2000,closefocus,infinity);
  oldpwmInValue2 = pwmInValue2 = pulseIn(pwmInPin2, HIGH);
 
  delay(100);

  // Set AF mode and focus to infinity.
  Serial.println("INIT: Set AF");
  while (true) {
    // This is the very first command we send to the camera.
    // If the camera isn't powered on, we loop here until we get a response.
    HWUART.clear();                             // Make sure RX buffer is empty
    setAF();
    responseLen = readResponse(responseBuf, 5); // Camera's response to the Set AF command is 5 bytes long, so that's what we tell the readResponse function to expect.
    if (responseLen == 5) {                     // Camera sent 5 bytes back, so we assume it's alive and well.
      break;                                    // You really should do a more thorough check on the response, but this isn't production code ;-)
    }
    // Didn't get the expected response, loop and try again...
    doubleBlink();                              // Double-blink the LED, to show that we're still in INIT.
    delay(1600);
  }
  Serial.println("INIT: Focus infinity");
  sendCommand(focusToInf, sizeof(focusToInf));
  delay(2000);

  // Switch to stills mode.
  Serial.println("INIT: Switch to stills mode");
  sendCommand(switchToStill, sizeof(switchToStill));   // Push the switchToStill[] array out on the UART.
  Serial.print("INIT: Set focus speed: ");
  Serial.println(focusSpeed);
  setFocusSpeed[9] = (char) focusSpeed;               // POKE focus speed value into the array...
  sendCommand(setFocusSpeed, sizeof(setFocusSpeed));  // and send the array out on the UART.
  focusPosition = focusToX[8] ;                       // PEEK MSB of focus position from buffer.
  HWUART.clear();
  Serial.println("INIT: End");
  
}

void loop() {
  // =================================================================================================================
  // Read PWM signal from Radio Control receiver. Pulse width is between about 1000 and 2000 microseconds (varies slightly between units), depending on stick position.
  // Move Channel 1 (steering) stick to the left to switch between stills and movie mode, move the stick to the right to take a still picture or start/stop movie recording.
  // Channel 2 (throttle) controls lens focus.

  pwmInValue1 = map(pulseIn(pwmInPin1, HIGH), 1000, 2000, infinity, closefocus);
  pwmInValue2 = pulseIn(pwmInPin2, HIGH);

 // Channel 2. Set lens focus position, depending on throttle stick position.
  if((pwmInValue1 < oldpwmInValue1 - pwmFocusStepSize) || (pwmInValue1 > oldpwmInValue1 + pwmFocusStepSize)) {
    Serial.printf("oldpwmInValue1 %i pwmInValue1 %i\n", oldpwmInValue1, pwmInValue1);
    converter.integer = pwmInValue1;
    SwapFourBytes();
    oldpwmInValue1 = pwmInValue1;
    doAnalogFocus();
  }

  // Channel 1. Stick left to switch Stills <--> Movie mode. Stick right to take picture or start/stop movie recording.
  if(pwmInValue2 < 500) {
    // Receiver not connected, do nothing.
  } else {  
    if(pwmInValue2 < 1250) {
    
      if (stillsMode) {
        Serial.println("RC: Switch Stills --> Movie");
        sendCommand(switchToMovie, sizeof(switchToMovie));
      } else {
        Serial.println("RC: Switch Movie --> Stills ");
        sendCommand(switchToStill, sizeof(switchToStill));
      }
      stillsMode = !stillsMode;
    }
    if(pwmInValue2 > 1750) {
      // Take a picture.
      if (stillsMode) {
        Serial.println("RC: Shutter");
        sendCommand(capture, sizeof(capture));  
      } else {
        if (isRecording) {
          Serial.println("RC: Stop REC");
          sendCommand(stopRec, sizeof(stopRec)); 
        } else {
          Serial.println("RC: Start REC");
          sendCommand(startRec, sizeof(startRec));
        }
        isRecording = !isRecording;  
      }
    
    }
  }
  //Serial.printf("oldpwmInValue1 %i pwmInValue2 %i oldpwmInValue2 %i pwmInValue2 %i\n", oldpwmInValue1, pwmInValue1, oldpwmInValue2, pwmInValue2);
  
  // =================================================================================================================
  // Read analog input A0 and refocus if it has changed by more than what's in the "focusStepSize" variable.
  analogFocusPot = analogRead(0);
  //  Serial.print("analog 0 is: ");
  //  Serial.println(analogFocusPot);
  focusValue = analogFocusPot * stepsize;
  // Serial.print("focusValue: ");
  // Serial.println(focusValue);
  focusValue += infinity;
  // Serial.print("focusValue+infinity: ");
  // Serial.println(focusValue);
  focusTo = (unsigned int) focusValue;
  if (focusTo > closefocus) focusTo = closefocus;
  if (focusTo < infinity) focusTo = infinity;
  //  Serial.print("focusTo: ");
  //  Serial.println(focusTo);
  //  Serial.print("oldfocusTo: ");
  //  Serial.println(oldfocusTo);
  //  Serial.printf("Before conversion: focusPosition[]  is 0x%0X 0x%0X 0x%0X 0x%0X\n", converter.focusPosition[0], converter.focusPosition[1], converter.focusPosition[2], converter.focusPosition[3]);
  converter.integer = focusTo;
  //  Serial.printf("After conversion:  focusPosition[]  is 0x%0X 0x%0X 0x%0X 0x%0X\n", converter.focusPosition[0], converter.focusPosition[1], converter.focusPosition[2], converter.focusPosition[3]);
  // Convert LSB first to MSB first.
  SwapFourBytes();
  //  Serial.printf("After LSB <-> MSB swap:  focusPosition[]  is 0x%0X 0x%0X 0x%0X 0x%0X\n", converter.focusPosition[0], converter.focusPosition[1], converter.focusPosition[2], converter.focusPosition[3]);

  // Has the analog value changed more than the set threshold?
  // If it has, refocus the lens.
  if (( focusTo > (oldfocusTo + focusStepSize)) || (focusTo < (oldfocusTo - focusStepSize))) {
    Serial.print("analog 0 is: ");
    Serial.println(analogFocusPot);
    Serial.printf("focusPosition[] is 0x%0X 0x%0X 0x%0X 0x%0X\n", converter.focusPosition[0], converter.focusPosition[1], converter.focusPosition[2], converter.focusPosition[3]);
    oldfocusTo = focusTo;

    doAnalogFocus();
  }
  // =================================================================================================================
  
  
  
  
  // =================================================================================================================
  // Read button 0, toggle focus mode if pressed.
  if (! digitalRead(PIN_B0)) {
    Serial.println("Button B0");
    if (focusModeIsAF) {
      setMF();
    } else {
      setAF();
    }
  }
  // =================================================================================================================
  // Read button 1, toggle playback.
  if (! digitalRead(PIN_B1)) {
    Serial.println("Button B1 Toggle Playback");

//Display the picture ...
    Serial.println("Switch to PlayBack");
    sendCommand(switchToPB, sizeof(switchToPB));
    while(true) {
      tripleBlink();                      // Blink the LED, to show that we are in Playback mode.
      if(! digitalRead(PIN_B1)) break;    // Loop until button 1 is pressed again.
      if(! digitalRead(PIN_B2)) sendCommand(nextPB, sizeof(nextPB));
      if(! digitalRead(PIN_B3)) sendCommand(prevPB, sizeof(prevPB));
    }
    Serial.println("Switch to Stills mode");
    sendCommand(switchToStill, sizeof(switchToStill));
    HWUART.clear();
    delay(550);
  }
  // =================================================================================================================
  // Read buttons 2 and 3, adjust delay between triggering a shot and when the flash fires.
  if (! digitalRead(PIN_B2)) {
    Serial.println("Button B2");
    Serial.printf("Increase flashDelay to %i ms\n", ++flashDelay);
  }
  if (! digitalRead(PIN_B3)) {
    Serial.println("Button B3");
    Serial.printf("Decrease flashDelay to %i ms\n", --flashDelay);
  }
  // =================================================================================================================
  if (! digitalRead(PIN_B7)) {
    // Take a picture.
    Serial.println("Button B7");
    Serial.printf("Shutter. flashDelay is %i ms\n", flashDelay);
sendCommand(capture, sizeof(capture));

    delay(flashDelay);              // Wait for a short while and then...
    digitalWrite(flashPin, HIGH);   // ... fire the flash. Setting the digital pin HIGH will make the transistor conduct, pulling the flash's trigger pin low.
    delay(25);                      // Keep flash's trigger voltage low for at least 25 ms (ISO 10330 Photography -- Synchronizers, ignition circuits and connectors for cameras and photoflash units)
    digitalWrite(flashPin, LOW);    // Release the flash's trigger pin.
    HWUART.clear();
  }

  digitalWrite(ledPin, ledMode);   // Slow blink the LED, to show that the board is active in the main loop.
  // digitalWrite(quenchPin, ledMode);
  ledMode = !ledMode;
  delay(500);                       // Just to slow down reading of the buttons.
} // loop

// =================================================================================================================
void setMF() {
  buflen = 7;
  buf = switchToMF;
  Serial.println("Switch to MF");
  sendCommand(buf, buflen);
  focusModeIsAF = false;
  delay(500);
}
// =================================================================================================================
void setAF() {
  buflen = 7;
  buf = switchToAF;
  Serial.println("Switch to AF");
  sendCommand(buf, buflen);
  focusModeIsAF = true;
  delay(500);
}


// =================================================================================================================
// Send arbitrary string to camera.
void sendCommand(byte *buf, int buflen) {
  int i;
  Serial.printf("Send buffer. buflen: %i\n", buflen);
  for (i = 0; i < buflen; i++) {
    Serial.printf("0x%X ", (byte) buf[i]);
  }
  Serial.println();
  HWUART.clear();                        // Make sure RX buffer is empty;
  HWUART.write(buf, buflen);
}
// =================================================================================================================
size_t readResponse(byte *buffer, int length) {
  size_t rlen;
  unsigned int i;
  Serial.println("Waiting for camera response...");
  rlen = HWUART.readBytes(buffer, length);
  Serial.print("Response length: ");
  Serial.println(rlen);
  if (rlen) Serial.print("Response: ");
  for (i = 0; i < rlen; i++) {
    Serial.printf("0x%X ", (byte) buffer[i]);
  }
  Serial.println();
  return rlen;
}
// =================================================================================================================
// i2c eeprom simulator.
void requestEvent() {
  // Respond to i2c request from camera, to read device id 0x51.
  Wire.write(0x03);        // Send 3, telling the camera to confígure itself for UART on its pins 5 and 6.
  Serial.println ("i2c: Sent 0x3");
}
// =================================================================================================================
void alarm() {
  // Blink the LED rapidly for two seconds.
  for (int i = 0; i < 40; i++) {
    digitalWrite(ledPin, ledMode);   // set the LED
    ledMode = !ledMode;
    delay(50);
  }
}
// =================================================================================================================
void fastBlink(int blinks) {
  int i;
  for (i = 0; i < blinks; i++) {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
  }
}
void doubleBlink(){
  fastBlink(2);
}
void tripleBlink(){
  fastBlink(3);
}

// =================================================================================================================
void doAnalogFocus() {
  int i;
  Serial.println("FOCUS: Focus position (analog 0) changed, refocus!");
  focusToX[6] = converter.focusPosition[0];
  focusToX[7] = converter.focusPosition[1];
  focusToX[8] = converter.focusPosition[2];
  focusToX[9] = converter.focusPosition[3];
  Serial.println("FOCUS: Sending SET focus position command");
  for (i = 0; i < sizeof(focusToX); i++) {
    Serial.printf("0x%X ", (byte) focusToX[i]);
  }
  Serial.println();
  HWUART.clear();
  sendCommand(focusToX, sizeof(focusToX));
  Serial.println("FOCUS: Getting response from SET focus position command");
  responseLen = readResponse(responseBuf, 22);
  delay(100);
  Serial.println("FOCUS: Sending get focus position command");
  HWUART.clear();
  sendCommand(getLensFocusPosition, sizeof(getLensFocusPosition));
  Serial.println("FOCUS: Getting response from GET focus position command");

  responseLen = readResponse(responseBuf, 22);

}
// =================================================================================================================
void SwapFourBytes() {
  // gcc stores integers LSB first. Swap them around to create MSB first in the byte array)
  byte tempo[4];
  tempo[0] = converter.focusPosition[3];
  tempo[1] = converter.focusPosition[2];
  tempo[2] = converter.focusPosition[1];
  tempo[3] = converter.focusPosition[0];
  converter.focusPosition[0] = tempo[0];
  converter.focusPosition[1] = tempo[1];
  converter.focusPosition[2] = tempo[2];
  converter.focusPosition[3] = tempo[3];

}

