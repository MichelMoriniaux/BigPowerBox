﻿# BigPowerBox ASCOM Driver
An open source power distribution switch for 12VDC applications

Please see the [README for the firmware](https://github.com/MichelMoriniaux/BigPowerBox/blob/main/Arduino/BigPowerBox/README.md) before reading this file

- [BigPowerBox ASCOM Driver](#bigpowerbox-ascom-driver)
- [Introduction](#introduction)
- [Principles of Operation](#principles-of-operation)
- [Installing the driver](#installing-the-driver)
- [Building the driver](#building-the-driver)

# Introduction
This Ascom driver will work with the open source hardware BigPowerBox or any hardware that respects the BigPowerBox Protocol.

The driver exposes all the power ports and sensors of the device via Ascom Switches. The polling frequency will depend on the client software. For eg. N.I.N.A polls each switch's name and value every 2sec.

The configuration pane allows to set the names of the power ports. Note that upon launch the configuration dialog does not know the names saved on the device, please set the comm port and press OK, connect and then re-access the configuration dialog. The names saved on the device will be populated and will be editable. Any modifications done to the port names prior to connecting to the device will be discarded. Note that the first time you read the port names on a brand new board you will get garbage as the firmware does not initialize the EEPROM at initial boot. Actually there is no mechanism for the firmware to know if a valid name is stored.

# Principles of Operation
The driver is built as a COM server. This allows multiple clients to connect and control the switches concurrently.
The server driver is started when the first client connects. The server connects to the hardware via a USB serial port, retrieves the board description and populates the internal data structures.  
The client polling and hardware polling are decoupled for performance reasons. Once connected the hardware driver starts a polling thread that polls the hardware ( status command ) every ***UPDATEINTERVAL*** seconds ( 2 in the Release version) and updates the internal data structures. When a client requests a port value or status the hardware driver responds with the value stored in the data structure. When a client wants to update a switch the command is directly forwarded to the hardware.  
So in short *Read* operations are asynchronous and *Write* oprations are synchronous.  
The server maintains a list of clients and shuts down once all clients have disconnected.  
You can now configure the PWM ports into 4 modes:
- 0: normal PWM mode
- 1: on/off mode this requires a connect/deconnect in N.i.n.a. for the UI to update
- 2: dew heater mode, when the ambient temperature dips under the dewpoint the associated port will turn on to a preset value
- 3: temperature control, this requires an additional temperature probe for the dew heater and will control the port according to a PID algorithm to set the temperature to dewpoint + configurable offset


# Installing the driver 
Downlad the installer from the [releases](https://github.com/MichelMoriniaux/BigPowerBox/releases) tab and run it, it should register the server and make the driver available in the ASCOM choser. 

# Building the driver
Open the solution in Visual Studio 2022 and press `CTRL+B` the driver should build.  
Open a command prompt and run the local server exe `ASCOM.ShortCircuitBigPowerSwitch.exe` with the `/regserver` parameter (requires admin privileges), which will create the entry that appears in the ASCOM Chooser. This registration only needs to be done once on each PC. Use the `/unregserver` parameter to unregister the server when no longer required. Never use `REGASM` on the local server executable.
Even better modify the included .iss file to match your paths and compile your own installer, .iss files are used with Inno Setup Compiler
