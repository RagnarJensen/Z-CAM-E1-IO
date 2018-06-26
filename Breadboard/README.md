# Z-CAM-E1-IO

## Cabling for the camera and the breadboard.

I have made a couple of cables (I have several breadboards for the E1 going) using ordinary pin headers and plugs I've found on eBay.
 
Finding the plugs has not been easy, as there is scarce info about which type to use. I have found two different, working plugs on eBay, [one of which is no longer available](https://www.ebay.co.uk/itm/8Pcs-Mini-USB-12-Pin-Male-Plug-Connector-For-DIY/170580271467) and [this one](https://www.ebay.co.uk/itm/12-x-Mini-USB-Plug-Male-12-Pin-Used-for-SAMSUNG-Product/180396604436), which I'm using right now.

I made this picture of the camera's I/O port pinout a while ago. I have taken the pin numbering from [Z CAM's own picture](https://github.com/imaginevision/Z-Camera-Doc/blob/master/expansion/pin.jpg), which is the only illustration showing all the port's pins I've found. The pin numbering they use does not match up with the one the plug manufacturer uses, but I'll use Z CAM's numbering throughout anyway, to avoid confusion.
![Z CAM E1 IO port pinout](https://github.com/RagnarJensen/Z-CAM-E1-IO/blob/master/Flash/e1_io_pinout.jpg)

This is what one of my cables looks like.
![My home-made cable](connectors_pinout.png)
Standard pin headers with some hot glue over the solder points, to keep the pins from escaping from the plastic part they are sitting in.
There are two pin headers, one with the odd numbered pins and the other with the even numbered ones.
The header pin numbers shown are those of the camera's I/O port pins. Camera's pins 10 and 12 (CVBS) goes directly to a RCA connector and are not connected to the breadboard headers.


Here's another of my cables, in the breadboard.

![Blue cable in BB odd pins](connector_odd_pins.png)
Odd numbered pins, showing pin 11 connected to the breadboard's ground rail (orange and green wires) and the pull-up resistor for i2c pin 7 (blue wire).
In the background, you can see that I've not used a coaxial cable for the CVBS connector. For short runs of composite video, a twisted pair suffices.
![Blue cable in BB even pins](connector_even_pins.png)
Even numbered pins. No pins 10 and 12 (CVBS), since they go directly from the camera to the RCA connector.
Pin 2 (VCOM) is connected to the board's positive power rail, providing power to the entire breadboard. Another pull-up resistor connected to i2c pin 8.

## Breadboard set-up
The pins I use on the Teensy 2.0
![Teensy 2.0 pinout](https://github.com/RagnarJensen/Z-CAM-E1-IO/blob/master/Flash/Connections_Teensy.jpg)

Fritzing image of the breadboard:
![Fritzing BB](z_cam_io_focus_bb.png)

Detail showing the camera connection to the breadboard:
![Fritzing detail camera connection](breadboard_camera_connection.png)

A schematic of the above:
![Frizing schematic](z_cam_io_focus_schem.png)

## Caveats
NOTE!!  In the pictures above, the breadboard is set up to power the microcontroller from the camera.
If you will power the controller separately, e.g. from its USB connector while developing your sketch, remove the pink wire.
Otherwise the camera's and the controller's power supplies will fight each other and that seldom ends well...
![Pink wire providing power to MC from camera](breadboard_controller_pink_power_link.png)





