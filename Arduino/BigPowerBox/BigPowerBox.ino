/* 
 *  BigPowerBox implementation
 *  License: GPLv3
 *  
 *  
 *  Michel Moriniaux 2023
*/

#include "myDefines.h"
#include "board.h"
#include <Arduino.h>
#include <EEPROM.h>                     // needed for EEPROM
#include <Wire.h>
#include <Adafruit_MCP23X17.h>
#include <Adafruit_SHT31.h>             // for SHT3x sensor attached to RJ12 port
#include <SparkFun_I2C_Mux_Arduino_Library.h>
#include <Adafruit_AHTX0.h>
#include <PIDController.h>

#ifdef DEBUG
#include <MemoryFree.h>
#endif

Adafruit_MCP23X17 mcp;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
QWIICMUX imux;
Adafruit_AHTX0 aht10;
PIDController pid[4];   // up to 4 pid controllers, one for each PWM port

int probeCount = 0;
int memfree = 0;
int prevmemfree = 0;

const String programName = "BigPowerBox";
const String programVersion = "003";
const String programAuthor = "Michel Moriniaux";

struct config_t powerBoxConf;
struct status_t powerBoxStatus;

extern String boardSignature;
extern const byte ports2Pin[];
extern const byte port2bin[];

char* queue[QUEUELENGTH];
int queueHead = -1;
int queueCount = 0;

// Machine states
enum FSMStates { stateIdle, stateRead, stateDew, stateSwap };
// PWM port modes
enum PWMModes { variable, switchable, dewHeater, tempFeedback};
// Commands
String line;                              // command buffer
String status;                            // status string buffer

int currentConfAddr = 0;                  // EEPROM address of the current valid config storage block
int portIndex = 0;                        // index of the current port being measured
int portMax;                              // number of ports
int idx = 0;                              // index into the command string
bool haveTemp;                                 // stores whether the SHT sensor was found (true)
bool dsel = true;                         // we start with dsel HIGH for port 1
int chip = 0;                             // chip index being measured
// time
long int now;                             // now time in millis
long int last;                            // last time in millis
long int lastm;                           // last time we updated the dewheaters in millis

//-----------------------------------------------------------------------
// Utility functions
//-----------------------------------------------------------------------

char* pop() {
  --queueCount;
  DPRINT(F("- pop queueCount="));
  DPRINT(queueCount);
  DPRINT(F(" content="));
  DPRINTLN(queue[queueHead]);
  return queue[queueHead--];
}


void push(char command[MAXCOMMAND]) {
  queueCount++;
  queueHead++;
  DPRINT(F("- push queueCount="));
  DPRINT(queueCount);
  DPRINT(F(" content="));
  strncpy(queue[queueHead], command, MAXCOMMAND);
  DPRINTLN(queue[queueHead]);
}


// check whether to write EEPROM if contents of config are different
//   than what is in the EEPROM at currentConfAddr
// return true if any member is different
bool updateEEPROMCheck( config_t savedConfig ) {
  if ( savedConfig.portStatus != powerBoxConf.portStatus )
    return true;
  for ( int i=0; i < sizeof(powerBoxConf.pwmPorts); i++ ) {
    if ( savedConfig.pwmPorts[i] != powerBoxConf.pwmPorts[i] )
      return true;
    if ( savedConfig.pwmPortMode[i] != powerBoxConf.pwmPortMode[i] )
      return true;
    if ( savedConfig.pwmPortPreset[i] != powerBoxConf.pwmPortPreset[i] )
      return true;
    if ( savedConfig.pwmPortTempOffset[i] != powerBoxConf.pwmPortTempOffset[i] )
      return true;
  }
  DPRINTLN(F("- updateEEPROMCheck False"));
  return false;
}


// Write to the EEPROM if the config has changed
void writeConfigToEEPROM() {
  config_t savedConfig;
  EEPROM.get(currentConfAddr, savedConfig);

  if ( updateEEPROMCheck( savedConfig ) ) {
    DPRINT(F("- writing new config to EEPROM at="));
    // make the saved config obsolete
    EEPROM.write(currentConfAddr, OLDCONFIGFLAG);
    // calculate the new config slot addr
    currentConfAddr = currentConfAddr + sizeof(config_t);
    // check if the new config is going to fit, if not restart at the begining
    if ( currentConfAddr + sizeof(config_t) >= EEPROM.length() ) {
      currentConfAddr = EEPROMCONFBASE;
    }
    DPRINTLN(currentConfAddr);
    // write the new config at the next memory slot
    EEPROM.put(currentConfAddr, powerBoxConf);
  }
}


// Write the new port name to the EEPROM
void writeNameToEEPROM(int port, const String &name) {
  int address;
  char buf[NAMELENGTH];
  char old[NAMELENGTH];

  // determine the EEPROM address
  address = port * NAMELENGTH;
  // get the previous name and make sure we are not wasting write cycles
  EEPROM.get(address, old);
  DPRINT(F("- writeNameToEEPROM Old="));
  DPRINT(old);
  DPRINT(F(" New="));
  name.toCharArray(buf, NAMELENGTH);
  DPRINTLN(buf);
  if (strcmp(old, buf)) {
    DPRINT(F("- writeNameToEEPROM writing="));
    DPRINTLN(buf);
    EEPROM.put(address, buf);
  }
}


// reset config to defaults 
// only called if we did not find a current config
void setDefaults() {
  powerBoxConf.currentData = CURRENTCONFIGFLAG;
  powerBoxConf.portStatus = ALLOFF;
  for ( int i=0; i < sizeof(powerBoxConf.pwmPorts); i++ ) {
    powerBoxConf.pwmPorts[i] = PWMMIN;
    powerBoxConf.pwmPortMode[i] = variable;
    powerBoxConf.pwmPortPreset[i] = PWMMIN;
    powerBoxConf.pwmPortTempOffset[i] = 0;
  }
  currentConfAddr = EEPROMCONFBASE;
  writeConfigToEEPROM();                                   // update values in EEPROM
}


void clearSerialPort() {
  while ( Serial.available() )
    Serial.read();
}


// SerialEvent occurs whenever new data comes in the serial RX.
void serialEvent() {

  // '>' starts the command, '#' ends the command, do not store these in the command buffer
  // read the command until the terminating # character
  char buf[MAXCOMMAND];
  while ( Serial.available() )
  {
    char inChar = Serial.read();
    switch ( inChar )
    {
      case '>':     // soc, reinit line
        // memset(line, 0, MAXCOMMAND);
        line = "";
        idx = 0;
        break;
      case '#':     // eoc
        // line[idx] = '\0';
        line.toCharArray(buf, MAXCOMMAND);
        idx = 0;
        DPRINT(F("- serialEvent push="));
        DPRINT(buf);
        DPRINTLN(F("|"));
        push(buf);
        break;
      default:      // anything else
        if ( idx < MAXCOMMAND - 1) {
          // line[idx++] = inChar;
          line += inChar;
          // DPRINTLN(line);
        }
        break;
    }
  }
}


// SERIAL COMMS
void sendPacket(const String &str) {
  DPRINT(F("- Send: "));
  DPRINTLN(str);
  Serial.print(str);
}


//-----------------------------------------------------------------------
// Port Operations
//-----------------------------------------------------------------------
void getStatusString() {
  // return a status string to the driver with the following info:
  // - a bitmap of port statuses following the boardSignature format
  // the current of each port
  // the in current
  // the in voltage
  // current temperature
  // current humidity
  // 0:0:0:0:0:0:0:0:127:255:195:100:1:1:5.54:5.49:5.42:5.37:5.44:5.49:5.54:5.49:5.39:5.49:5.44:5.37:0.22:0.23:0.07:3.37:0.00:0.00
  // TODO make this modular and based on boardSignature

  // port status
  status = "";
  for ( int i=0; i < 8; i++) {
    status += bitRead(powerBoxConf.portStatus, i);
    status += ":";
  }
  // PWM port duty cycles
  for ( int i=0; i < sizeof(powerBoxConf.pwmPorts); i++) {
    status += powerBoxConf.pwmPorts[i];
    status += ":";
  }
  // the 2 always-on ports
  status += "1:1:";
  // port currents
  for ( int i=0; i < (sizeof(powerBoxStatus.portAmps) / sizeof(float)); i++) {
    status += powerBoxStatus.portAmps[i];
    status += ":";
  }
  // input Amps
  status += powerBoxStatus.inputAmps;
  status += ":";
  // input Volts
  status += powerBoxStatus.inputVolts;
  // Temperatures
  if (haveTemp) {
    status += ":";
    // temperature
    status += powerBoxStatus.temp;
    status += ":";
    // humidity
    status += powerBoxStatus.humid;
    status += ":";
    // dewpoint
    status += powerBoxStatus.dewpoint;
    for ( int i = 1; i < probeCount ; i++ ) {
      status += ":";
      // temperature
      status += powerBoxStatus.tempProbe[i];
    }
  }
}


void switchPortOn(int port) {
  // determine the type of port
  DPRINT(F("- spon port="));
  DPRINTLN(port);
  if ( boardSignature[port] == 's' ) {
    // normal on/off port
    if ( bitRead(powerBoxConf.portStatus, port) == 0x00 ) {
      digitalWrite(ports2Pin[port], HIGH);
      powerBoxConf.portStatus = powerBoxConf.portStatus + port2bin[port];
    }
  }
  if ( boardSignature[port] == 'm' ) {
    // multiplex on/off port
    mcp.digitalWrite(ports2Pin[port], HIGH);
    if ( bitRead(powerBoxConf.portStatus, port) == 0x00 ) {
      powerBoxConf.portStatus = powerBoxConf.portStatus + port2bin[port];
    }
    DPRINTLN(mcp.readGPIO());
  }
  if ( boardSignature[port] == 'p' ) {
    // PWM on/off port
    if (powerBoxConf.pwmPorts[port - boardSignature.indexOf("p")] != PWMMAX ) {
      analogWrite(ports2Pin[port], PWMMAX);
      powerBoxConf.pwmPorts[port - boardSignature.indexOf("p")] = PWMMAX;
    }
  }
  DPRINT(F("- PortStatus="));
  DPRINTLN(powerBoxConf.portStatus);

  // we may have made a change so write the config to EEPROM
  writeConfigToEEPROM();
}


void switchPortOff(int port) {
  // determine the type of port
  DPRINT(F("- spoff port="));
  DPRINTLN(port);
  if ( boardSignature[port] == 's' ) {
    // normal on/off port
    if ( bitRead(powerBoxConf.portStatus, port) == 0x01 ) {
      digitalWrite(ports2Pin[port], LOW);
      powerBoxConf.portStatus = powerBoxConf.portStatus - port2bin[port];
    }
  }
  if ( boardSignature[port] == 'm' ) {
    // multiplex on/off port
    mcp.digitalWrite(ports2Pin[port], LOW);
    if ( bitRead(powerBoxConf.portStatus, port) == 0x01 ) {
      powerBoxConf.portStatus = powerBoxConf.portStatus - port2bin[port];
    }
    DPRINTLN(mcp.readGPIO());
  }
  if ( boardSignature[port] == 'p' ) {
    // PWM on/off port
    if (powerBoxConf.pwmPorts[port - boardSignature.indexOf("p")] != PWMMIN ) {
      analogWrite(ports2Pin[port], PWMMIN);
      powerBoxConf.pwmPorts[port - boardSignature.indexOf("p")] = PWMMIN;
    }
  }
  DPRINT(F("- PortStatus="));
  DPRINTLN(powerBoxConf.portStatus);
  // we may have made a change so write the config to EEPROM
  writeConfigToEEPROM();
}


void shutdownAllPorts() {
  for ( int i=0; i < boardSignature.length(); i++) {
    if ( boardSignature[i] == 's' ) {
      // normal on/off port
      if ( bitRead(powerBoxConf.portStatus, i) == 1 ) {
        digitalWrite(ports2Pin[i], LOW);
        powerBoxConf.portStatus = powerBoxConf.portStatus - port2bin[i];
      }
    }
    if ( boardSignature[i] == 'm' ) {
      // multiplex on/off port
      if ( bitRead(powerBoxConf.portStatus, i) == 1 ) {
        mcp.digitalWrite(ports2Pin[i], LOW);
        powerBoxConf.portStatus = powerBoxConf.portStatus - port2bin[i];
      }
    }
    if ( boardSignature[i] == 'p' ) {
      // PWM on/off port
      if (powerBoxConf.pwmPorts[i - boardSignature.indexOf("p")] != PWMMIN) {
        analogWrite(ports2Pin[i], PWMMIN);
        powerBoxConf.pwmPorts[i - boardSignature.indexOf("p")] = PWMMIN;
      }
    }
  }
  // we may have made a change so write the config to EEPROM
  writeConfigToEEPROM();
}


void setPWMPortLevel(int port, int level) {
  if ( boardSignature[port] == 'p' ) {
    // PWM on/off port
    if (powerBoxConf.pwmPorts[port - boardSignature.indexOf("p")] != level) {
      analogWrite(ports2Pin[port], level);
      powerBoxConf.pwmPorts[port - boardSignature.indexOf("p")] = level;
    }
    // we may have made a change so write the config to EEPROM
    writeConfigToEEPROM();
  }
}


// same function but don't write the EEPROM
void setDewPortLevel(int port, int level) {
    // PWM on/off port
    if ( boardSignature[port] == 'p' ) {
      analogWrite(ports2Pin[port], level);
	} 
}


void swapPorts() {
  // when the controller starts
  // portIndex = 0
  // DSEL = HIGH
  // MUX0=MUX1=MUX2=0
  // this points to the first logical port and output 2 of the first BTS7008 switch
  // portIndex starts at 0 but port numbers at 1 <- TODO stop this BS, do it in the UI

  // increment portIndex
  portIndex++;
  // rollover portIndex if we've reached the end
  if ( portIndex >= portMax )
    portIndex = 0;

  // each time we call swapPorts() we need to inverse DSEL except for the 
  // always on ports but since there are 2 of them we don't care
  dsel = !dsel;
  digitalWrite(DSEL, dsel);

  // every even portIndex we need to change the mux to the next value
  if ( (portIndex % 2) == 0 )
    chip++;
  // dirty ugly hack for our board but no good for modular hardware
  if ( portIndex == 13 )
    chip++;
  // rollover the chip if we are beyond the max
  if ( chip > portMax / 2 )
    chip = 0;
  // determine the values of MUXx from chip and write them
  digitalWrite(MUX0, bitRead(chip, 0));
  digitalWrite(MUX1, bitRead(chip, 1));
  digitalWrite(MUX2, bitRead(chip, 2));
  // done
#ifdef DEBUG
  //char buf[40];
  //sprintf(buf, "- swap: chip=%d, port=%d, dsel=%d", chip, portIndex, dsel);
  //DPRINTLN(buf);
  //sprintf(buf, "- mux: c=%d, 0=%d, 1=%d, 2=%d", chip, bitRead(chip, 0), bitRead(chip, 1), bitRead(chip, 2));
  //DPRINTLN(buf);
#endif
}


//-----------------------------------------------------------------------
// Dew Control
//-----------------------------------------------------------------------
void adjustDewHeaters() {
  int pwmPort;
  int pwmPortNum = 0;
  int level;

  pwmPortNum = sizeof(powerBoxConf.pwmPorts);
  // PWM ports start at boardSignature.indexOf("p")
  //
  for ( int port=boardSignature.indexOf("p"); port < pwmPortNum + boardSignature.indexOf("p"); port++) {

    // Zero based index for probes 
    //
    int index = port - boardSignature.indexOf("p");
    if ( powerBoxConf.pwmPortMode[index] == dewHeater ) {
      if (powerBoxStatus.temp < powerBoxStatus.dewpoint + powerBoxConf.pwmPortTempOffset[index]) {
        // As of now powerBoxConf.pwmPortPreset is not being initialized anywhere. Use powerBoxConf.pwmPorts for now 
        // TBD: do something meaningful with powerBoxConf.pwmPortPreset
        //
        setDewPortLevel(port, int(powerBoxConf.pwmPorts[index]));
      } else if ( powerBoxStatus.temp > powerBoxStatus.dewpoint + powerBoxConf.pwmPortTempOffset[index]) {
        setDewPortLevel(port, PWMMIN);;
      }
    }
    if ( powerBoxConf.pwmPortMode[index] == tempFeedback ) {
      if ( powerBoxStatus.tempProbe[index] < powerBoxStatus.dewpoint + powerBoxConf.pwmPortTempOffset[index]) {
        pid[port- boardSignature.indexOf("p")].setpoint(powerBoxStatus.dewpoint + powerBoxConf.pwmPortTempOffset[index]);
        level = int(pid[index].compute(powerBoxStatus.tempProbe[index]));
        DPRINT(F("tempfeedbck set port "));
        DPRINT(port);
        DPRINT(F(" power: "));
        DPRINTLN(level);
        setDewPortLevel(port, level);
      }
    }
  }
}


//-----------------------------------------------------------------------
// Temperature probes discovery and helpers
//-----------------------------------------------------------------------
void discoverProbes(int muxPort) {
  bool probeFound = false;
  bool skip_44 = false;
  bool skip_45 = false;
  bool skip_10 = false;

  // determine if we need to skip some discoveries (eg. skip the non muxed probes already found)
  if (muxPort != 255) {
    // only skip if we are discovering muxed probes
    for (int probe = 0; probe < probeCount; probe++){
      if (powerBoxStatus.tempProbePort[probe] == 255) {
        if (powerBoxStatus.tempProbeType[probe] == SHT31_0x44) { skip_44 = true; }
        if (powerBoxStatus.tempProbeType[probe] == SHT31_0x45) { skip_45 = true; }
        if (powerBoxStatus.tempProbeType[probe] == AHT10) { skip_10 = true; }
      }
    }
  }

  // check for SHT3x sensor
  // the SHT3x is much more precise and reliable than the AHT10 but is also MUCH more expensive ~8$
  // we check for these first as we use the first sensor found as the reference sensor, all subsequent sensors
  // are used only for temperature for PWM dew heater feedback

  // if we have this one as native (muxPort = 255) we cannot have one muxed so lets check if we should skip it
  if (!skip_44) {
    probeFound = sht31.begin(0x44);
    if (probeFound) {
      DPRINTLN(F("Found SHT3x_44 Temp/Humid monitor"));
      if (probeCount > 0)
        boardSignature += 't';
      else
        boardSignature += 'f';
      haveTemp = true;
      powerBoxStatus.tempProbePort[probeCount] = muxPort;
      powerBoxStatus.tempProbeType[probeCount++] = SHT31_0x44;
      probeFound = false;
    }
  }
  // check for an SHT3x at the other address
  // if we have this one as native (muxPort = 255) we cannot have one muxed so lets check if we should skip it
  if (!skip_45) {
    probeFound = sht31.begin(0x45); // 0x45 is an alternate address for the SHT31
    if (probeFound) {
      DPRINTLN(F("Found SHT3x_45 Temp/Humid monitor"));
      if (probeCount > 0)
        boardSignature += 't';
      else
        boardSignature += 'f';
      haveTemp = true;
      powerBoxStatus.tempProbePort[probeCount] = muxPort;
      powerBoxStatus.tempProbeType[probeCount++] = SHT31_0x45;
      probeFound = false;
    }
  }

  // check for AHT10
  // the AHT10 is cheap ~1$ but less reliable
  // if we have this one as native (muxPort = 255) we cannot have one muxed so lets check if we should skip it
  if (!skip_10) {
    probeFound = aht10.begin();
    if (probeFound) {
      DPRINTLN(F("found AHT10 Temp/Humid monitor"));
      if (probeCount > 0)
        boardSignature += 't';
      else
        boardSignature += 'f';
      haveTemp = true;
      powerBoxStatus.tempProbePort[probeCount] = muxPort;
      powerBoxStatus.tempProbeType[probeCount++] = AHT10;
      probeFound = false;
    }
  }
}


//-----------------------------------------------------------------------
// Command processing
//-----------------------------------------------------------------------
void processSerialCommand() {
  int cmdval;
  int port;
  int level;
  int mode;
  char name[NAMELENGTH];
  
  String receiveString = "";
  String optionString = "";
  String replyString = "";
  char replyChars[17];

  if ( queueCount == 0 )
    return;
  receiveString = String(pop());
  char cmd = receiveString[0];
  #ifdef DEBUG
  DPRINT(F("- receive string="));
  DPRINTLN(receiveString);
  DPRINT(F("- cmd="));
  DPRINTLN(cmd);
  #endif
  switch (cmd)
  {
    case 'P':       // Ping commmand '>P#', respond with '>POK#'
      sendPacket(">POK#");
      break;
    case 'D':       // Discover command '>D#', respond with boardSignature and versions
      replyString += ">D:";
      replyString += programName;
      replyString += ":";
      replyString += programVersion;
      replyString += ":";
      replyString += boardSignature;
      replyString += "#";
      sendPacket(replyString);
      break;
    case 'S':       // Status command '>S#', return a formatted string with all currents and voltages as well as a port bitmap
      getStatusString();
      replyString += ">S:";
      replyString += status;
      replyString += "#";
      sendPacket(replyString);
      break;
    case 'N':       // get port n name command '>N:nn#'
      optionString = receiveString.substring(2, receiveString.length());
      port = (int)optionString.toInt();
      EEPROM.get(port * NAMELENGTH, name);
      sprintf(replyChars, ">N:%02d:%s#", port, name);
      sendPacket(replyChars);
      break;
    case 'M':       // set port n name s command '>M:nn:s#'
      optionString = receiveString.substring(2, receiveString.indexOf(":",3));
      port = (int)optionString.toInt();
      DPRINT(F("- port="));
      DPRINTLN(port);
      optionString = receiveString.substring(receiveString.indexOf(":",3) + 1, receiveString.length());
      DPRINT(F("- workstring="));
      DPRINTLN(optionString);
      writeNameToEEPROM(port, optionString);
      sendPacket(">MOK#");
      break;
    case 'O':       // Set switch port n ON command '>O:nn#', return OK
      optionString = receiveString.substring(2, receiveString.length());
      port = (int)optionString.toInt();
      switchPortOn(port);
      sendPacket(">OOK#");
      break;
    case 'F':       // Set swithport n OFF command '>F:nn#', return OK
      optionString = receiveString.substring(2, receiveString.length());
      port = (int)optionString.toInt();
      switchPortOff(port);
      sendPacket(">FOK#");
      break;
    case 'W':       // set PWM port n level n command '>W:nn:l#', return OK
      optionString = receiveString.substring(2, receiveString.indexOf(":",3));
      port = (int)optionString.toInt();
      optionString = receiveString.substring(receiveString.indexOf(":",3) + 1, receiveString.length());
      level = (int)optionString.toInt();
      setPWMPortLevel(port, level);
      sendPacket(">WOK#");
      break;
    case 'C':       // configure PWM port mode command '>C:nn:m#', return OK
      optionString = receiveString.substring(2, receiveString.indexOf(":",3));
      port = (int)optionString.toInt();
      optionString = receiveString.substring(receiveString.indexOf(":",3) + 1, receiveString.length());
      mode = (int)optionString.toInt();
      powerBoxConf.pwmPortMode[port - boardSignature.indexOf("p")] = byte(mode);
      sendPacket(">COK#");
      writeConfigToEEPROM();
      break;
    case 'G':       // get PWM port mode command '>G:nn#', return '>G:nn:m#'
      optionString = receiveString.substring(2, receiveString.indexOf(":",3));
      port = (int)optionString.toInt();
      mode = int(powerBoxConf.pwmPortMode[port - boardSignature.indexOf("p")]);
      if ( mode < 0 || mode > 3 ) {
        powerBoxConf.pwmPortMode[port - boardSignature.indexOf("p")] = byte(variable);
        mode = byte(variable);
      }
      sprintf(replyChars, ">G:%02d:%d#", port, mode);
      sendPacket(replyChars);
      break;
    case 'T':       // configure PWM port temp offset command '>T:nn:m#', return OK
      optionString = receiveString.substring(2, receiveString.indexOf(":",3));
      port = (int)optionString.toInt();
      optionString = receiveString.substring(receiveString.indexOf(":",3) + 1, receiveString.length());
      mode = (int)optionString.toInt();
      powerBoxConf.pwmPortTempOffset[port - boardSignature.indexOf("p")] = byte(mode);
      sendPacket(">TOK#");
      writeConfigToEEPROM();
      break;
    case 'H':       // get PWM port temp Offset command '>H:nn#', return '>H:nn:m#'
      optionString = receiveString.substring(2, receiveString.indexOf(":",3));
      port = (int)optionString.toInt();
      mode = int(powerBoxConf.pwmPortTempOffset[port - boardSignature.indexOf("p")]);
      sprintf(replyChars, ">H:%02d:%d#", port, mode);
      sendPacket(replyChars);
      break;
    default:
      break;
  }
}


void setup() {
  DPRINTLN("Setup Start");
  status.reserve(STATUSSIZE);
  boardSignature.reserve(boardSignature.length() + 5);
  // prereserve the queue
  for ( int i=0; i < QUEUELENGTH; i++)
    queue[i] = (char*)malloc(MAXCOMMAND);
  line.reserve(MAXCOMMAND);

  // initialize all of our hardware first
  // initialize serial port
  Serial.begin(SERIALPORTSPEED);
  // clear any garbage from serial buffer
  clearSerialPort();

  // initialize the MCP27017
  if ( boardSignature.indexOf('m') != -1 ) 
    mcp.begin_I2C();

  // initialize the status struct to 0
  memset(&powerBoxStatus, 0, sizeof(powerBoxStatus));

  // discover external i2c peripherals
  // currently we support a 1:8 multiplexer for peripherals with the same address
  // and idividual SHT31 or AHT10 probes

  // lets start by discovering the mux and disable all ports
  if (imux.begin())
  {
    DPRINT(F("I2C Mux detected. disabling all ports, port: "));
    for ( int i=0; i < 8 ; i++ ) {
      DPRINT(i);
      imux.disablePort(i);
    }
    DPRINTLN(F(" Done"));
  }
  
  // now lets look for non=muxed sht31 or AHT10 probe, if we find one this will be our refence environmental probe
  // note: if we find an AHT probe at this stage we cannot have anymore multiplexed as they only have one address
  // same-ish goes for SHT31.So possible setups is an SHT31 and multiple AHT10s on the mux or an AHT10 and multiple 
  // SHT31s on the mux or even simpler: every probe on the mux
  // corner case: if we find both then we can't have a mux for temp probes so the SHT31 will be the reference and
  // the AHT10 will control the first PWM port 
  discoverProbes(255);
  // now discover the probes on the mux, if we have not found previous probes then the first one found here (attached to mux port 0)
  // will be our reference env probe all subsequent ones will be used to control PWM ports if they are configured for
  // dew heaters

  DPRINT(F("I2C Mux Discovery started, port: "));
  for ( int i=0; i < 8 ; i++ ) {
    DPRINT(i);
    imux.enablePort(i);
    imux.setPort(i);
    discoverProbes(i);
  }
  DPRINTLN(F(" Done"));

  //----- PWM frequency for D3 & D11 -----
  // may be usefull to drive flat panels
  // TODO: create a command to enable this
  //Timer2 divisor = 2, 16, 64, 128, 512, 2048
  //TCCR2B = TCCR2B & B11111000 | B00000001;    // 31KHz
  //TCCR2B = TCCR2B & B11111000 | B00000010;    // 3.9KHz
  //TCCR2B = TCCR2B & B11111000 | B00000011;    // 980Hz
  //TCCR2B = TCCR2B & B11111000 | B00000100;    // 490Hz (default)
  //TCCR2B = TCCR2B & B11111000 | B00000101;    // 245Hz
  //TCCR2B = TCCR2B & B11111000 | B00000110;    // 122.5Hz
  //TCCR2B = TCCR2B & B11111000 | B00000111;    // 30.6Hz

  // initialize pins
  for ( int i=0; i < boardSignature.length(); i++) {
    if (boardSignature[i] == 'm')
      mcp.pinMode(ports2Pin[i], OUTPUT);
    if (boardSignature[i] == 's')
      pinMode(ports2Pin[i], OUTPUT);
    if (boardSignature[i] == 'p')
      pinMode(ports2Pin[i], OUTPUT);
  }
  pinMode(ISIN, INPUT);
  pinMode(VSIN, INPUT);
  pinMode(ISOUT, INPUT);
  pinMode(DSEL, OUTPUT);
  pinMode(MUX0, OUTPUT);
  pinMode(MUX1, OUTPUT);
  pinMode(MUX2, OUTPUT);
  pinMode(OLEN, OUTPUT);
  
  // enable Open Load detection by default
  digitalWrite(OLEN, HIGH);
  // set the diag to chip 1 and DSEL to High ( lower order ports are DSEL HIGH)
  digitalWrite(MUX0, LOW);
  digitalWrite(MUX1, LOW);
  digitalWrite(MUX2, LOW);
  digitalWrite(DSEL, dsel);

  // find a valid config
  // we're not allways writing to the same location and kill the EEPROM
  // so we need to find the last place we stored the config
  bool found = false;
  for ( int addr=EEPROMCONFBASE; addr < EEPROM.length(); addr = addr + sizeof(config_t)) {
    EEPROM.get(addr, powerBoxConf);
    if ( powerBoxConf.currentData == CURRENTCONFIGFLAG ) {
      DPRINT(F("- Found Valid config at="));
      DPRINTLN(addr);
      found = true;
      currentConfAddr = addr;
      break;
    }
  }
  if ( found ) {
    // restore ports per config
    DPRINT(F("- PortStatus="));
    DPRINTLN(powerBoxConf.portStatus);
    for ( int i=0; i < 8; i++)
      mcp.digitalWrite(ports2Pin[i], bitRead(powerBoxConf.portStatus, i));
    // PWM port duty cycles
    for ( int i=0; i < sizeof(powerBoxConf.pwmPorts); i++)
      analogWrite(ports2Pin[i], powerBoxConf.pwmPorts[i]);

  } else {
    setDefaults();
  }

  // initialize our delays
  now = millis();
  last = now;
  lastm = now;

  portIndex = 0;
  portMax = sizeof(powerBoxStatus.portAmps) / sizeof(float);

  // initialize the PID controllers even if we don't use themn
  for (int i=0; i < 4; i++) {
    pid[i].begin();          // initialize the PID instance
    pid[i].tune(KP, KI, KD);       // Tune the PID, arguments: kP, kI, kD
    pid[i].limit(0, 255);
  }

  DPRINTLN("Setup done");

}


void loop() {
  static byte FSMState = stateIdle;
  
  if ( queueCount >= 1 )                 // check for serial command
  {
    processSerialCommand();
  }
  switch (FSMState)
  {
    case stateIdle:
      // wait REFRESH milliseconds between Read cycles
      now = millis();
      if ( now > last + REFRESH )
        FSMState = stateRead;
      else
        FSMState = stateIdle;
      break;

    case stateRead:
      // lets read the values on our Analog ports
      // first the input
      // equations are given by the hardware implementation and the datasheets
      // to be truly modular (ie have the board.h be the SoT for the HW implementation)
      // we should use macros defined board.h
      powerBoxStatus.inputVolts = (( analogRead(VSIN) * (VCC / 1023.0) ) * RDIVIN ) / RDIVOUT;
      powerBoxStatus.inputAmps = (( analogRead(ISIN) * (VCC / 1023.0) ) - (VCC/2)) * 1000.0 / KINIS;
      // if input is above MAXINVOLTS then we need to shutdown power to all downstreams
      if ( powerBoxStatus.inputVolts > MAXINVOLTS )
        shutdownAllPorts();
      // next read the output current for the current port
      switch ( boardSignature[portIndex] ) {
        case 's':
        case 'm':
        case 'p':
          powerBoxStatus.portAmps[portIndex] = ( analogRead(ISOUT) * (VCC / 1023.0) ) * KILIS / ROUTIS;
          break;
        case 'a':
          powerBoxStatus.portAmps[portIndex] = (( analogRead(ISOUT) * (VCC / 1023.0)) - (VCC/2)) * 1000.0 / KOUTIS;
          break;
        default:
          break;
      }

#ifdef DEBUG
      //char buf[50];
      //sprintf(buf, "- loop: iV=%d, iI=%d, oI=%d, p=%d, mem=%d", analogRead(VSIN), analogRead(ISIN), analogRead(ISOUT), portIndex, freeMemory());
      //DPRINTLN(buf);
      //DPRINTLN(mcp.readGPIO());
      memfree = freeMemory();
      if (memfree != prevmemfree) {
        prevmemfree = memfree;
        DPRINT(F("Mem: "));
        DPRINTLN(memfree);
      }
#endif
      FSMState = stateDew;
      now = millis();
      last = now;
      break;

    case stateDew:
      // adjust PWM ports for dew heater control, do this every minute only
      now = millis();
      if ( now > lastm + (TEMPITVL * 60000) ) {
        // get temp and humidity
        if ( haveTemp ) {
          switch (powerBoxStatus.tempProbeType[0]) {
            case SHT31_0x44:
              sht31.begin(0x44);
              sht31.readBoth(&powerBoxStatus.temp, &powerBoxStatus.humid);
              break;
            case SHT31_0x45:
              sht31.begin(0x45);
              sht31.readBoth(&powerBoxStatus.temp, &powerBoxStatus.humid);
              break;
            case AHT10:
              sensors_event_t temp;
              sensors_event_t humid;
              aht10.getEvent(&humid, &temp);
              powerBoxStatus.temp = temp.temperature;
              powerBoxStatus.humid = humid.relative_humidity;
              break;
          }
          powerBoxStatus.tempProbe[0] = powerBoxStatus.temp;
          powerBoxStatus.dewpoint = powerBoxStatus.temp - ((100 - powerBoxStatus.humid) / 5);

          // cycle through the other sensors to get the temperatures for the PWM ports
          for ( int i = 1 ; i < probeCount ; i++ ) {
            imux.setPort(powerBoxStatus.tempProbePort[i]);
            switch (powerBoxStatus.tempProbeType[i]) {
              case SHT31_0x44:
                sht31.begin(0x44);
                powerBoxStatus.tempProbe[i] = sht31.readTemperature();
                break;
              case SHT31_0x45:
                sht31.begin(0x45);
                powerBoxStatus.tempProbe[i] = sht31.readTemperature();
                break;
              case AHT10:
                sensors_event_t temp;
                sensors_event_t humid;
                aht10.getEvent(&humid, &temp);
                powerBoxStatus.tempProbe[i] = temp.temperature;
                break;
            }
          }
        }
#ifdef DEBUG
        DPRINT(F(" Status: "));
        getStatusString();
        DPRINTLN(status);
#endif
        adjustDewHeaters();
        lastm = now;
      }
      FSMState = stateSwap;
      break;

    case stateSwap:
      // lets swap the measured port
      swapPorts();
      FSMState = stateIdle;
      break;

    default:
      FSMState = stateIdle;
      break;
  }

}
