# flight_tracker

[main project]
This is a pendant that will connect to the internet via bluetooth from a device, allowing it to grab and display the flight information of any plane within 5km of your location which was taken using GPS. Very similar to the prototype in terms of features and display, but still a work in progress.

Buzzer was taken out because I could not source a part small enough. The routing for the buzzer is still going to be on the pcb design but I will leave it unpopulated when I put it together. 

The schematic, pcb, and gerber files are all included, but it has yet to be built and tested. 

Materials:
- screen --> Waveshare 0.71-inch Round IPS LCD
- microcontroller --> Seeed Studio XIAO ESP32-C3
- battery ---> 402020 3.7V 160mAh LiPo Battery

[prototype]
This is a raspberry pi pico w connected to an LCD screen that detects live flights within 5km, buzzes, and then displays the callsign, altitude, km away, country origin, and an arrow pointing in the flight direction.

Materials used:
- raspberry pi pico w
- 1.54 LCD screen
- breadboard/jumper wires
- piezo buzzer

API used: https://opensky-network.org/api/states/all
