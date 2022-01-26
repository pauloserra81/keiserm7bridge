# keiserm7bridge
Bridge from keiser m7 BTLE profile to BIKEPOWER profile

Initial project will use 2 ESP32 boards.

One will connect to the Keiser M7 and parse cadence and power out of it, and send that data through ESP-NOW to the other board.

Other ESP32 will create a BTLE service of type BIKEPOWER, and provide power and cadence information so that it is readable by any bike computer/training app
