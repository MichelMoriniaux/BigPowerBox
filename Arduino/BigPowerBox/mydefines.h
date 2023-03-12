/*-----------------------------------------------------------------------
 * BigPowerBox Definitions
 * License: GPLv3
 * Michel Moriniaux, 2023
-----------------------------------------------------------------------*/

#ifndef myDefines_h
#define myDefines_h

#include <Arduino.h>

//-----------------------------------------------------------------------
// Constant Definitions
//-----------------------------------------------------------------------
#define MAXINVOLTS          14.4          // maximum allowed volts In, shutdown all output ports if exceeded
#define REFRESH             200           // read port values every REFRESH milliseconds
#define SERIALPORTSPEED     9600          // 9600, 14400, 19200, 28800, 38400, 57600
#define QUEUELENGTH         5             // number of commands that can be saved in the serial queue
#define MAXCOMMAND          21            // max length of a command
#define NAMELENGTH          16            // max lenght of a port name
#define EOFSTR              '\n'
#define EOCOMMAND           '#'           // defines the end character of a command
#define SOCOMMAND           '>'           // defines the start character of a command
#define CURRENTCONFIGFLAG   99            // the config struct has a currentdata field indicating whether it is in use
#define OLDCONFIGFLAG       0             // currentdata set this when eeprom structure is no longer in use
#define PWMMIN              0
#define PWMMAX              255
// port states bitmap
#define ALLOFF              0             // 00000000 ALL OFF
#define ALLON               255           // 11111111 ALL ON

//-----------------------------------------------------------------------
// DEBUGGING
//-----------------------------------------------------------------------
//#define DEBUG 1
#ifdef DEBUG
#define DPRINT(...) Serial.print(__VA_ARGS__)
#define DPRINTLN(...) Serial.println(__VA_ARGS__)
#else
#define DPRINT(...)
#define DPRINTLN(...)
#endif

#endif
