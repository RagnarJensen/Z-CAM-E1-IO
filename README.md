# Z-CAM-E1-IO
Controlling the Z CAM E1 through its I/O port.

This repository will hold bits and bobs from my experiments with the Z CAM E1's I/O port
and the API described at https://github.com/imaginevision/Z-Camera-Doc.

For Arduino stuff, I test on a close relative to it, a [Teensy 2.0 by PJRC](https://www.pjrc.com/store/teensy.html).

![My test setup](Test_setup.jpg)
Bitscope oscilloscope/logic analyzer, breadboard, camera, batteries as focus targets and a CVBS monitor.
The grey, furry things on the camera are [Rycote Micro Windjammers](https://rycote.com/microphone-windshield-shock-mount/micro-windjammers/).

![Connections on the Teensy](Connections_Teensy.jpg)
Which pin is what on the Teensy 2.0

![The breadboard](Breadboard.jpg)
The breadboard set up for UART control.
The two resistors on the right are pull-ups for the i2c lines.

![The breadboard with analog input added](Focus_control_by_analog_input.jpg)
The breadbord with a potentiometer added, for analog input.
Code for it is in Z_cam_UART_focus_analog_pot.ino

