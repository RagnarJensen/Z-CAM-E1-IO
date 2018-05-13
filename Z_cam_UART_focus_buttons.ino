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
 *  Press button 2 to focus farther away, in a stepwise fashion. There are five steps.
 *  Press button 3 to focus closer, in the same steps as above.
 *  Press button 7 to take a picture.
 *  Blinks the on-board LED in different patterns, to show status.
 *
 *  @Author: Ragnar Jensen
 *  
 *
*/

#include <Wire.h>

const int ledPin = 11;      // Teensy 2.0 

// Arrays of bytes, that make up commands to send to the camera.
// To send commands to the camera, we just push out one of these arrays onto the UART with Serial1.write() 
// And that's Serial with the number one at it's end, not Serial with an extra letter L.
char    switchToMovie[]  = {0xea, 0x02, 0x01, 0x02};
char    switchToStill[]  = {0xea, 0x02, 0x01, 0x03};
char    switchToMF[]     = {0xea, 0x02, 0x04, 0x0e, 0x16, 0x01, 0x00};
char    switchToAF[]     = {0xea, 0x02, 0x04, 0x0e, 0x16, 0x01, 0x01};
/*
 * These next three arrays hold commands to set the focus position at different points.
 * I got the values for the first two (infinity and close focus) by simply performing AF operations by hand, with the camera's buttons, 
 * and then sending a GET_LENS_FOCUS_POSITION command to the camera.
 * 
 * It is very possible that the focus point values for inifinity and closest focus are different with other lenses. Someone really ought to test that ;-)
 * The focus point is held in the last four bytes of the arrays. I have not seen anything but 0x00 in the first two of those four bytes,
 * making me think that the position is represented by a 16-bit integer held in the last two bytes. Someone really ought to test that, too ;-)
 */
char    focusToInf[]     = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x00, 0x9b};  // The four last bytes are the "inifinity" lens position of my Olympus 25/1.8 lens.
char    focusToClose[]   = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x04, 0x84};  // Ditto, for when the lens is focused as close as it can.
char    focusToX[]       = {0xea, 0x02, 0x07, 0x0e, 0x34, 0x02, 0x00, 0x00, 0x00, 0x9b};  // We manipulate the next to last byte of this array, with values between 0 and 4, to focus in 5 "steps".

char    capture[]        = {0xea, 0x02, 0x01, 0x07}; // Command string to take a picture.

char    *buf;
int     buflen = 7;           // Number of bytes in buffer, varies depending on command.
char    responseBuf[20];      // Responses from camera go into this buffer. I have no idea whether 20 bytes is enough...
size_t  responseLen = 0;

unsigned int focusPosition;   // Not used to it's full potential, yet... I just stuff fixed values into it for now.

bool    ledMode = HIGH;
bool    focusModeIsAF = true;  // Is camera set to MF or AF?
bool    focusToInfinity = true;// Holds direction to slew focus when button 1 is pressed.

void setup() {
  Serial.begin (9600); // Console
   
  Serial.println("INIT: Start");
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
  pinMode(PIN_B2, INPUT_PULLUP);  // Focus farther away.
  pinMode(PIN_B3, INPUT_PULLUP);  // Focus closer.
  pinMode(PIN_B7, INPUT_PULLUP);  // Shutter.
  
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
  sendCommand(switchToStill, 4);                // Push the switchToStill[] array out on the UART.

  focusPosition = focusToX[8] ;                 // PEEK MSB of focus position from buffer.
  Serial.println("INIT: End");
}

void loop() {
  // Read button 0, toggle focus mode if pressed.
  if (! digitalRead(PIN_B0)) {
    Serial.println("Button B0");
    if (focusModeIsAF) {
      setMF(); 
       focusModeIsAF = false; 
    } else {
      setAF();
      focusModeIsAF = true;
    }
  }
    
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
    delay(5000);                  // It takes a while to slew the lens from end to end at default (slow) speed.
    Serial.println("done");
  }

  // Read buttons 2 and 3, focus further away and focus closer, respectively.
  if (! digitalRead(PIN_B2)) {
    Serial.println("Button B2");
    focusFarther();
  }
  if (! digitalRead(PIN_B3)) {
    Serial.println("Button B3");
    focusCloser();
  }
  
if (! digitalRead(PIN_B7)) {
  // Take a picture.
  Serial.println("Button B7");
  Serial.println("Shutter");
  sendCommand(capture, sizeof(capture));
}
  
  digitalWrite(ledPin, ledMode);   // Slow blink the LED, to show that the board is active in the main loop.
  ledMode = !ledMode;
  delay(500);                       // Just to slow down reading of the buttons.
} // loop


void focusFarther() {
  if (focusPosition <= 0) {
    Serial.print ("Too far! ");
    Serial.println(focusPosition, HEX);
    alarm();
    return;
    } 
  focusPosition --;
  focusToX[8] = focusPosition;      // POKE value into byte 8 of the command buffer.
  setFocus();
}

void focusCloser() {
   if (focusPosition >= 4) {
    Serial.print ("Too close! ");
    Serial.println(focusPosition, HEX);
    alarm();
    return;
   }
   focusPosition ++;
   focusToX[8]= focusPosition;
   setFocus();
}

void setMF() {
  buflen = 7;
  buf = switchToMF;
  Serial.println("Switch to MF");
  Serial1.write(buf, buflen);
  focusModeIsAF = false;
  delay(500);
}

void setAF() {
  buflen = 7;
  buf = switchToAF;
  Serial.println("Switch to AF");
  Serial1.write(buf, buflen);
  focusModeIsAF = true;
  delay(500);
}

// Set focus to an arbitrary point.
// Last four bytes in focusToX array hold the desired focus point.
// This sketch only manipulates the second to last one of the the four.
 void setFocus() {
  if (!focusModeIsAF) {
    setAF();
    delay(100);
  }
  buflen = 10;
  buf = focusToX;                     
  Serial.print("Focus to step ");
  Serial.println(focusPosition);
  Serial1.write(buf, buflen);
  delay(1000); // Give it some time to move focus.
}


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
}

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
}

// Send arbitrary string to camera.
void sendCommand(char *buf, int buflen) {
  Serial.println("Send buffer");
  Serial1.write(buf, buflen);
}

size_t readResponse(char *buffer, int length) {
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

// i2c eeprom simulator. 
void requestEvent() {
   // Respond to i2c request from camera, to read device id 0x51.
   Wire.write(0x03);        // Send 3, telling the camera to confígure itself for UART on its pins 5 and 6.
   Serial.println ("i2c: Sent 0x3");
}

void alarm(){
  // Blink the LED rapidly for two seconds.
  for(int i = 0; i < 40; i++) {
    digitalWrite(ledPin, ledMode);   // set the LED
    ledMode = !ledMode;
    delay(50);    
  }
}

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

