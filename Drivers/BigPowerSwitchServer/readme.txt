# BigPowerBox Arduino Firmware
An open source power distribution switch for 12VDC applications

Please see the README for the firmware before reading this file

- [BigPowerBox Arduino Firmware](#bigpowerbox-arduino-firmware)
- [Introduction](#introduction)
- [Principles of Operation](#principles-of-operation)
- [Installing the driver](#installing-the-driver)
- [Building the driver](#building-the-driver)

# Introduction
This Ascom driver will work with the open source hardware BigPowerBox or any hardware that respects the BigPowerBox Protocol.

The driver exposes all the power ports and sensors of the device via Ascom Switches. The polling frequency will depend on the client software. For eg. N.I.N.A polls each switch's name and value every 2sec.

The configuration pane allows to set the names of the power ports. Note that upon launch the configuration dialog does not know the names saved on the device, please set the comm port and press OK, connect and then re-access the configuration dialog. The names saved on the device will be populated and will be editable. Any modifications done to the port names prior to connecting to the device will be discarded.

# Principles of Operation
The driver is built as a COM server. This allows multiple clients to connect and control the switches concurrently.
The server driver is started when the first client connects. The server connects to the hardware via a USB serial port, retrieves the board description and populates the internal data structures.  
The client polling and hardware polling are decoupled for performance reasons. Once connected the hardware driver starts a polling thread that polls the hardware ( status command ) every ***UPDATEINTERVAL*** seconds ( 2 in the Release version) and updates the internal data structures. When a client requests a port value or status the hardware driver responds with the value stored in the data structure. When a client wants to update a switch the command is directly forwarded to the hardware.  
So in short *Read* operations are asynchronous and *Write* oprations are synchronous.  
The sevrer maintains a list of clients and shuts down once all clients have disconnected.

# Installing the driver 
Downlad the installer from the realeases tab and run it, it should register the server and make the driver available in the ASCOM choser. 

# Building the driver
Open the solution in Visual Studio 2022 and press `CTRL+B` the driver should build.  
Open a command prompt and run the local server exe `ASCOM.ShortCircuitBigPowerSwitch.exe` with the `/regserver` parameter (requires admin privileges), which will create the entry that appears in the ASCOM Chooser. This registration only needs to be done once on each PC. Use the `/unregserver` parameter to unregister the server when no longer required. Never use `REGASM` on the local server executable.
