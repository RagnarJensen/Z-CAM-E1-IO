/*
 * This is proof-of-concept code, only to show how you might control the Z-cam E1 with serial communications on its UART.
 * 
 * This code is utterly NOT quality assured. Don't be surprised if there are bugs and errors. I would be surprised if there aren't any ;-)
 * There's little to no error checking, result codes and responses from the camera are regularly thrown away without checking them, and so on and so forth...
 * No optimization or attempts to write pretty code have been attempted. This is truly ugly-hack level code :-)
 * 
 * Tested on a Teensy 2.0 - https://www.pjrc.com/teensy/
 * The Teensy is not a pure clone of the Arduino, but it is a very close relative. 
 * You use the regular Arduino IDE, with PJRC's additional Teensyduino, for coding and programming it.
 * It has a hardware UART, that you reach with the Serial1.X(...) functions.
 * I think the closest Arduino counterpart to the Teensy 2.0 is the Leonardo.
 * For all practical purposes, the Teensy can be seen as an Arduino, but some specs, pin names and numbers differ.
 * 
 * I have only used an Olympus 25/1.8 lens for testing, while writing this sketch.
 * 
 * What this sketch can do:
 *  Simulate an i2c EEPROM, to make the camera configure itself for UART mode.
 *  At start-up, it waits for the camera to come alive. 
 *  At start-up, it sets the camera to Stills mode.
 *  At start-up, it sets focus mode to AF.
 *  At start-up, it focuses the lens to infinity.
 *  Press button 0 to toggle between MF and AF.
 *  Press button 1 to slew focus between infinity and closest focus. Press again to go in the other direction.
 *  Press button 2 to increase flash delay.
 *  Press button 3 to decrease flash delay.
 *  Press button 7 to take a picture and fire an external flash.
 *  Blinks the on-board LED in different patterns, to show status.
 *  Take analog input (e.g. from a potentiometer) and focus the lens based on the analog value.
  *  
 *  @Author: Ragnar Jensen
 *  
*/

#include <Wire.h>

const int           ledPin = 11;          // Teensy 2.0 
const int           flashPin = 13;        // B4

const unsigned int  infinity = 0x009b;    // Hardcoded value representing infinity on my Olympus 25/1.8 lens.
const unsigned int  closefocus = 0x0484;  // Ditto, for closest focus.
unsigned int        span;
unsigned int        focusTo, oldfocusTo;
unsigned int        flashDelay;           // How many milliseconds to wait between a CAPTURE command and firing the flash.
    
float               stepsize;
int                 focusStepSize = 3;    // How much analog input should change, before we actually take action and refocus.
                                          // Set to more than 1, to avoid refocusing if there is jitter on the analog input.
                                          // The downside is that the number of steps the lens can make gets smaller and the step size gets larger.
                                          // I get some jitter just by touching the metal shield on the potentiometer...

int                 focusSpeed = 2;      // How fast the lens should move. Camera's default is 1, which is fairly slow (good for those smoooooth focus pulls in video).
                                          // I have tested up to 10, I don't know the upper limit...
float               focusValue;

// Used for converting 32 bit integers to byte array and vice versa.
union ArrayToInteger {
  byte focusPosition[4]; 
 uint32_t integer;
};

ArrayToInteger converter = {0,0,0,0x9b};  // Create a converter between 32-bit integer and byte array.
                                          // Initialize with value for infinity, just to have a sane value in there when sketch starts.


// Arrays of bytes, that make up commands to send to the camera.
// To send commands to the camera, we just push out one of these arrays onto the UART with Serial1.write() 
// And that's Serial with the number one at it's end, not Serial with an extra letter L.
byte    switchToMovie[] = {0xea, 0x02, 0x01, 0x02};
byte    switchToStill[] = {0xea, 0x02, 0x01, 0x03};
byte    switchToPB[]    = {0xea, 0x02, 0x01, 0x04};
byte    startPB[]       = {0xea, 0x02, 0x01, 0x0a};
byte    stopPB[]        = {0xea, 0x02, 0x01, 0x0b};

byte    switchToMF[]    = {0xea, 0x02, 0x04, 0x0e, 0x16, 0x01, 0x00};
byte    switchToAF[]    = {0xea, 0x02, 0x04, 0x0e, 0x16, 0x01, 0x01};
byte    setFocusSpeed[] = {0xea, 0x02, 0x07, 0x0e, 0x35, 0x02, 0x00, 0x00, 0x00, 0x01}; // Last byte (bytes?) hold(s) focus speed, camera's default is 1.
/*
 * These next three arrays hold commands to set the focus position at different points.
 * I got the values for the first two (infinity and close focus) by simply performing AF operations by hand, with the camera's buttons, 
 * and then sending a GET_LENS_FOCUS_POSITION command to the camera.
 * 
 * It is very possible that the focus point values for infinity and closest focus are different with other lenses. Someone really ought to test that ;-)
 * The focus point is held in the last four bytes of the arrays. I have not seen anything but 0x00 in the first two of those four bytes,
 * making me think that the position is represented by a 16-bit integer held in the last two bytes. Someone really ought to test that, too ;-)
 */
byte    focusToInf[]     = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x00, 0x9b};  // The four last bytes are the "inifinity" lens position of my Olympus 25/1.8 lens.
byte    focusToClose[]   = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x04, 0x84};  // Ditto, for when the lens is focused as close as it can.
byte    focusToX[]       = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x00, 0x9b};  // We manipulate the two last bytes of this array, with values derived from the potentiometer's position.

byte    capture[]        = {0xea, 0x02, 0x01, 0x07}; // Command string to take a picture.

byte    *buf;
int     buflen = 7;           // Number of bytes in buffer, varies depending on command.
byte    responseBuf[20];      // Responses from camera go into this buffer. I have no idea whether 20 bytes is enough...
size_t  responseLen = 0;

unsigned int focusPosition;   // Not used to it's full potential, yet... I just stuff fixed values into it for now.

bool    ledMode = HIGH;
bool    flashMode = LOW;
bool    focusModeIsAF = true;  // Is camera set to MF or AF?
bool    focusToInfinity = true;// Holds direction to slew focus when button 1 is pressed.

int analogFocusPot;                 // Values from potentiometer on pin A0.
 // =================================================================================================================

void setup() {
  Serial.begin (9600); // Console

  Serial.println("INIT: Start");
  span = closefocus - infinity;  // How many steps there are between infinity and closest focus, as an integer.
  stepsize = (float)span / 1024; // Anlog values range 0 - 1023. stepsize is how much to change the focus position value when the analog value changes by 1.
                                  // With my Olympus 25/1.8 lens' values for infinity and close focus, stepsize ends up being 0.98.
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
  Serial1.begin(115200);
    
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
  
  Serial.println("INIT: LED");
  pinMode(ledPin, OUTPUT);
  delay(100);
 
  // Set AF mode and focus to infinity.
  Serial.println("INIT: Set AF");
  while (true) {
    // This is the very first command we send to the camera.
    // If the camera isn't powered on, we loop here until we get a response.
    
    setAF();
    responseLen = readResponse(responseBuf, 5); // Camera's response to the Set AF command is 5 bytes long,
                                                // so that's what we tell the readResponse function to expect.
    if (responseLen == 5) {                     // Camera sent 5 bytes back, so we assume it's alive and well.
      break;                                    // You really should do a more thorough check on the response, but this isn't production code ;-)
    }
    // Didn't get the expected response, loop and try again...
    doubleBlink();                              // Double-blink the LED, to show that we're still in INIT.
    delay(1600);
  }
  Serial.println("INIT: Focus infinity");
  setInfinity();
  
  // Switch to stills mode.
  Serial.println("INIT: Switch to stills mode");
  sendCommand(switchToStill, sizeof(switchToStill));   // Push the switchToStill[] array out on the UART.
  Serial.print("INIT: Set focus speed: ");
  Serial.println(focusSpeed);
  setFocusSpeed[9] = (char) focusSpeed;               // POKE focus speed value into the array...
  sendCommand(setFocusSpeed, sizeof(setFocusSpeed));  // and send the array out on the UART.
  focusPosition = focusToX[8] ;                       // PEEK MSB of focus position from buffer.
  Serial.println("INIT: End");
}

void loop() {
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
  if(focusTo > closefocus) focusTo = closefocus;
  if(focusTo < infinity) focusTo = infinity;
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
  if(( focusTo > (oldfocusTo + focusStepSize)) || (focusTo < (oldfocusTo - focusStepSize))) {
  Serial.print("analog 0 is: ");
  Serial.println(analogFocusPot);
  Serial.printf("focusPosition[] is 0x%0X 0x%0X 0x%0X 0x%0X\n", converter.focusPosition[0], converter.focusPosition[1], converter.focusPosition[2], converter.focusPosition[3]);
    oldfocusTo = focusTo; 
    
    doAnalogFocus();
  }
  
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
  // Read button 1, alternately set focus to infinity or close, if pressed.
  if (! digitalRead(PIN_B1)) {
    Serial.println("Button B1");
     // Toggle between infinity and close focus.
    if (focusToInfinity) {
      setClose();
    } else {
      setInfinity();
    }
    focusToInfinity = !focusToInfinity;
    Serial.print("Focusing, please wait... ");
    digitalWrite(ledPin, HIGH);   // Turn on LED, to show that we're busy.
    delay(5000 / focusSpeed);     // It takes a while to slew the lens from end to end at default (slow) speed.
    Serial.println("done");
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
    delay(100);                     // Keep flash's trigger voltage low for at least 25 ms (ISO 10330 Photography -- Synchronizers, ignition circuits and connectors for cameras and photoflash units)
    digitalWrite(flashPin, LOW);    // Release the flash's trigger pin.
    
    //Display the picture for a while...
    Serial.println("Switch to PlayBack");
    sendCommand(switchToPB, sizeof(switchToPB));
    delay(7500);
    Serial.println("Switch to Stills mode");
    sendCommand(switchToStill, sizeof(switchToStill));
  }
  
  digitalWrite(ledPin, ledMode);   // Slow blink the LED, to show that the board is active in the main loop.
  ledMode = !ledMode;
  delay(500);                       // Just to slow down reading of the buttons.
} // loop

// =================================================================================================================
void setMF() {
  buflen = 7;
  buf = switchToMF;
  Serial.println("Switch to MF");
  Serial1.write(buf, buflen);
  focusModeIsAF = false;
  delay(500);
}
// =================================================================================================================
void setAF() {
  buflen = 7;
  buf = switchToAF;
  Serial.println("Switch to AF");
  Serial1.write(buf, buflen);
  focusModeIsAF = true;
  delay(500);
}

// =================================================================================================================
void setInfinity() {
  if (!focusModeIsAF) {
    setAF();
    delay(100);
  }
  buflen = 10;
  buf = focusToInf;
  Serial.println("Focus infinity");
  Serial1.write(buf, buflen);
  focusPosition = 0;
  focusToX[8] = focusPosition;
  delay(3000 / focusSpeed);
}
// =================================================================================================================
void setClose() {
    if (!focusModeIsAF) {
    setAF();
    delay(100);
  }

  buflen = 10;
  buf = focusToClose;
  Serial.println("Focus near");
  Serial1.write(buf, buflen);
  focusPosition = 4;
  focusToX[8] = focusPosition;
  delay(3000  / focusSpeed);
}
// =================================================================================================================
// Send arbitrary string to camera.
void sendCommand(byte *buf, int buflen) {
  Serial.println("Send buffer");
  Serial1.write(buf, buflen);
}
// =================================================================================================================
size_t readResponse(byte *buffer, int length) {
  size_t rlen;
  unsigned int i;
  Serial.println("Waiting for camera response...");
  rlen = Serial1.readBytes(buffer, length);
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
void alarm(){
  // Blink the LED rapidly for two seconds.
  for(int i = 0; i < 40; i++) {
    digitalWrite(ledPin, ledMode);   // set the LED
    ledMode = !ledMode;
    delay(50);    
  }
}
// =================================================================================================================
void doubleBlink() {
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
    digitalWrite(ledPin, HIGH);
    delay(100);
    digitalWrite(ledPin, LOW);
    delay(100);
}
// =================================================================================================================
void doAnalogFocus(){
  Serial.println("Focus position (analog 0) changed, refocus!");
  focusToX[6] = converter.focusPosition[0];
  focusToX[7] = converter.focusPosition[1];
  focusToX[8] = converter.focusPosition[2];
  focusToX[9] = converter.focusPosition[3];
  sendCommand(focusToX, sizeof(focusToX));
}
// =================================================================================================================
void SwapFourBytes() {
  // gcc stores integers LSB first. Swap them around to create MSB first in the byte array)
  byte tempo[4];
  tempo[0]=converter.focusPosition[3];
  tempo[1]=converter.focusPosition[2];
  tempo[2]=converter.focusPosition[1];
  tempo[3]=converter.focusPosition[0];
  converter.focusPosition[0]=tempo[0];
  converter.focusPosition[1]=tempo[1];
  converter.focusPosition[2]=tempo[2];
  converter.focusPosition[3]=tempo[3];

}

