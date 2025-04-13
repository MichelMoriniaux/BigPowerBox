# BigPowerBox Arduino Firmware
An open source power distribution switch for 12VDC applications

Please see the README for the hardware before reading this file
- [BigPowerBox Arduino Firmware](#bigpowerbox-arduino-firmware)
- [Introduction](#introduction)
- [Logic](#logic)
- [Required Libraries](#required-libraries)
- [Modularity](#modularity)
- [Hardware expansion](#hardware-expansion)
- [Temperature probes](#temperature-probes)
- [Storage](#storage)
- [Command Protocol](#command-protocol)
  - [Available commands:](#available-commands)
- [Building Options](#building-options)


# Introduction
This is the firmware for the BigPowerBox. It manages input commands and provides status updates to the driver. The Powerbox can function standalone without a USB connection to a host.

# Logic
The firmware for the power box is built as a state machine.
The state machine starts in an ***idle*** state. Every loop cycle it verifies if there is a command in queue and processes it.
Every *REFRESH* milliseconds the state changes to ***read***.
In ***read*** state the firmware polls the arduino ADC pins for values and verifies if the input voltage is below the shutdown value. If the input voltage is above, it calls a function to shutdown all the switchable and PWM output ports.
Once the values are read the FSM moves to ***dew*** state where it reads temperatures and adjusts the configured PWM ports. Once this is done the FSM goes to the ***swap*** state and uses the PCB's multiplexers to select the next port to read the output Current from. Once the swap is executed the FSM returns to ***idle*** state.

When a command is sent to the controller it is pushed into a LIFO queue. At each loop the code checks if there are commands in queue and if yes pops one command and processes it.

# Required Libraries
To build you will need to install the following packages into your Arduino Libraries:  
***EEPROM***      should be included by default in your base install  
***Wire***        One Wire Lib for i2c  
***Adafruit_MCP23XXX*** to operate the MCP23017 i2c controlled I/O expander on the board  
***Adafruit_SHT31*** for SHT3x temperature/humidity sensor  
***Adafruit_AHTX0*** for the cheap AHT10 temperature/humidity sensor
***Adafruit_BME280*** for the BME280 temperature/humidity/pressure sensor
***SparkFun_I2C_Mux_Arduino_Library*** for the PCA9548A i2c multiplexer
***PIDController*** for the PWM dew heater PID control
***[MemoryFree](https://github.com/mpflaga/Arduino-MemoryFree)*** only for debug purposes ( used it to make sure I was not fragmenting the memory )


# Modularity
I originaly wanted this code to be generic and customizable for any board layout by only customizing *board.h*. This is still a work in progreass as I took shortcuts to get a working prototype out. The main variable is the boardSignature string that defines the list of ports, their order and their types. This also defines how the status string is presented. The following comment in *board.h* defines the boardSignature:

    // Board signature
    //  s: arduino addressable switchable port
    //  m: multiplexed switchable port
    //  p: pwm port
    //  a: Allways-On port
    //  f: Temp/Humidity probe
    //  g: Temp/Humidity/pressure probe
    //  t: temperature probe
    //  h: humidity probe
    // always-on ports always last followed by t then h
    const String boardSignature = "mmmmmmmmppppaath";

# Hardware expansion
The board is expandable through the exposed i2c interface via the RJ12 connector. Currently are supported BME280, SHT31, AHT10 and the PCA9548A i2c multiplexer, allowing you to build complex temperature probe setups. The setups allow you to either have a simple Temperature / Humidity sensor to turn on the configured PWM ports when the temperature dips below the dewpoint or have a more complex setup with dedicated temperature feedback for each PWM port (adjusting each port output to maintain a configurable temperature offset above the dewpoint).  
The RJ12 port has the following pinout  

    pin 1: NC
    pin 2: GND
    pin 3: SDA
    pin 4: SCL
    pin 5: 5V
    pin 6: NC

# Temperature probes
The board will detect i2c temperature probes at boot. The first probe found will always be the global environment probe, each additional probe found will be assigned to the PWM ports in ascending order for PID control. the order of preference for primary probe is BME280, SHT31, AHT10

# Storage
The EEPROM on the Atmel328 is 1024 bytes and it's cells are limited to 100k writes.  
We reserve the first 16x14 bytes (224 bytes) to store the port names ( this includes the trailing '\0' so port names are limited to 15 chars). We don't expect these to change often so no special scheme is implemented to save write cycles. There is no provision to flag a name as valid, we could have used the first byte and limit the name to 14 chars.  
Starting at byte 224 we store the configuration. The configuration is 18 bytes long and contains the port statuses and a validity flag. The config is saved every time a port changes state. To limit EEPROM wear we write the config to the next 18 following bytes. At startup we need to find this config so that is where the validity flag comes into play.

# Command Protocol
Every command and it's reply starts with a '>' and ends with a '#' 
newlines and carriage returns are not required, fields inside commands are separated by ':' characters

example commands:
|send|reply|
|---|---|
|`>P#`|`>POK#`|
|`>M:01:Hello World#`|`>MOK#`|
|`>N:01#`|`>N:01:Hello World#`|

## Available commands:
|literal|command|response|description|
|---|---|---|---|
|`P`|Ping|`POK`|Ping the device, can be used for autodiscovery of comm ports or to maintain a keepalive for a watchdog|
|`D`|Discover|`D:<Name>:<Version>:<Signature>`|Discover the device and capabilities, returns the following fields
|||`<Name>`|the device name|
|||`<Version>`|hardware revision|
|||`<Signature>`|a string representing the device capabilities|
|`S`|Status|`S:<statuses>:<currents>:<Ic>:<Iv>:<t>:<h>`|The status of all ports and measurements|
||||`<statuses>` is a colon separated list of statuses|
||||        0: switchable port Off|
||||        1: switchable port On or always-on port|
||||        000 to 255: PWM port duty-cycle level|
||||`<currents>` same number of fields as `<statuses>` each indicating the output current in A|
||||`<Ic>` Input Current in A|
||||`<Iv>` Input Voltage in V|
||||`<t>` temperature in C Optional ( only present if the hardware is detected )|
||||`<h>` humidity in % Optional ( only present if the hardware is detected )|
||||`S:0:1:0:1:0:1:005:200:1:1:0.00:5.25:0.00:3.12:0.00:7.09:0.10:2.3:0.00:0.00:15.46:12.4:8.1:75.0`|
||||the example matches the ouput for the signature string example above: 10 status fields `ssmmmmppaa` followed by 10 current fields in the same order followed by the input current, input voltage, temp and humid|
|`N:<dd>`|Get port Name|`N:<dd>:<portname>`|get the stored port name for port `<dd>` ( 2 digit number 0-padded eg `05` or `12`)|
||||`<portname>` 15 character max port name|
|`M:<dd>:<portname>`|Set port name|`MOK`|set the port name `<portname>` of port `<dd>`|
|`O:<dd>`|ON|`OOK`|Turn port `<dd>` On|	
|`F:<dd>`|OFF|`FOK`|Turn port `<dd>` Off|
|`W:<dd>:<level>`|set PWM level|`WOK`|set the port `<dd>` to `<level>` level is an integer between 0 (Off) and 255 (full On)|
|`C:<dd>:<mode>`|set PWM port mode|`COK`|set the port `<dd>` to `<mode>` mode is an integer: |
||||0: pwm adjustable port|
||||1: behave like and ON/OFF port|
||||2: dewpoint mode: when the temperature measured by the reference probe dips below the dewpoint turn on the port to the preset value, turn it off if the temperature rises above the dewpoint. requires a connected temp/humid probe|
||||3: temperature controlled mode: the port has a dedicated temperature probe that allows to turn off the port when the temperature of the dedicated probe rises above the dewpoint |
|`G:<dd>`|get PWM port mode|`G:<dd>:<mode>`|get `<mode>` of the port `<dd>`|
|`T:<dd>:<temp>`|set a positive temperature offset for the dew control fir each PWM port|`TOK`|set `<temp>` offset for port `<dd>`|
|`H:<dd>`|get the temperature offset for a port|`H:<dd>:<temp>`|get `<temp>` offset of the port `<dd>`|

# Building Options
to build and flash the firmware you will need the following:
a pogo pin programmer for the initial bootflash, see the instructions in the PCB folders
For the firmware use the same option as for an Arduino Nano:
- Board: Arduino AVR Boards / Arduino Nano
- Processor: ATMega328P
