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
#ifdef DEBUG
#include <MemoryFree.h>
#endif

Adafruit_MCP23X17 mcp;
Adafruit_SHT31 sht31 = Adafruit_SHT31();

const String programName = "BigPowerBox";
const String programVersion = "001";
const String programAuthor = "Michel Moriniaux";

struct config_t powerBoxConf;
struct status_t powerBoxStatus;

extern const String boardSignature;
extern const byte ports2Pin[];
extern const byte port2bin[];

char* queue[QUEUELENGTH];
int queueHead = -1;
int queueCount = 0;

// Machine states
enum FSMStates { stateIdle, stateRead, stateSwap }; 
// Commands
String line;                              // command buffer
String status;                            // status string buffer

int currentConfAddr = 0;                  // EEPROM address of the current valid config storage block
int portIndex = 0;                        // index of the current port being measured
int portMax;                              // number of ports
int idx = 0;                              // index into the command string
bool sht;                                 // stores whether the SHT sensor was found (true)
bool dsel = true;                         // we start with dsel HIGH for port 1
int chip = 0;                             // chip index being measured
// time
long int now;                             // now time in millis
long int last;                            // last time in millis

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
  for ( int i=0; i < sizeof(powerBoxConf.pwmPorts); i++ )
    if ( savedConfig.pwmPorts[i] != powerBoxConf.pwmPorts[i] )
      return true;

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
  address = ( port - 1 ) * NAMELENGTH;
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
  for ( int i=0; i < sizeof(powerBoxConf.pwmPorts); i++ )
    powerBoxConf.pwmPorts[i] = PWMMIN;
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
// 21 total, 16 chars name M:13:telescope 12 45\0
// M:13:ta belle suce des bites au bois de boulogne au fond des bois #

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
  if (sht) {
    status += ":";
    // temperature
    status += powerBoxStatus.temp;
    status += ":";
    // humidity
    status += powerBoxStatus.humid;
  }
}


void switchPortOn(int port) {
  // determine the type of port
  DPRINT(F("- spon port="));
  DPRINTLN(port);
  if ( boardSignature[port - 1] == 's' ) {
    // normal on/off port
    if ( bitRead(powerBoxConf.portStatus, port - 1) == 0x00 ) {
      digitalWrite(ports2Pin[port - 1], HIGH);
      powerBoxConf.portStatus = powerBoxConf.portStatus + port2bin[port - 1];
    }
  }
  if ( boardSignature[port - 1] == 'm' ) {
    // multiplex on/off port
    mcp.digitalWrite(ports2Pin[port - 1], HIGH);
    if ( bitRead(powerBoxConf.portStatus, port - 1) == 0x00 ) {
      powerBoxConf.portStatus = powerBoxConf.portStatus + port2bin[port - 1];
    }
    DPRINTLN(mcp.readGPIO());
  }
  if ( boardSignature[port - 1] == 'p' ) {
    // PWM on/off port
    if (powerBoxConf.pwmPorts[port - boardSignature.indexOf("p") - 1] != PWMMAX ) {
      analogWrite(ports2Pin[port - 1], PWMMAX);
      powerBoxConf.pwmPorts[port - boardSignature.indexOf("p") - 1] = PWMMAX;
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
  if ( boardSignature[port - 1] == 's' ) {
    // normal on/off port
    if ( bitRead(powerBoxConf.portStatus, port - 1) == 0x01 ) {
      digitalWrite(ports2Pin[port - 1], LOW);
      powerBoxConf.portStatus = powerBoxConf.portStatus - port2bin[port - 1];
    }
  }
  if ( boardSignature[port - 1] == 'm' ) {
    // multiplex on/off port
    mcp.digitalWrite(ports2Pin[port - 1], LOW);
    if ( bitRead(powerBoxConf.portStatus, port - 1) == 0x01 ) {
      powerBoxConf.portStatus = powerBoxConf.portStatus - port2bin[port - 1];
    }
    DPRINTLN(mcp.readGPIO());
  }
  if ( boardSignature[port - 1] == 'p' ) {
    // PWM on/off port
    if (powerBoxConf.pwmPorts[port - boardSignature.indexOf("p") - 1] != PWMMIN ) {
      analogWrite(ports2Pin[port - 1], PWMMIN);
      powerBoxConf.pwmPorts[port - boardSignature.indexOf("p") - 1] = PWMMIN;
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
  if ( boardSignature[port - 1] == 'p') {
    // PWM on/off port
    if (powerBoxConf.pwmPorts[port - boardSignature.indexOf("p") - 1] != level) {
      analogWrite(ports2Pin[port - 1], level);
      powerBoxConf.pwmPorts[port - boardSignature.indexOf("p") - 1] = level;
    }
    // we may have made a change so write the config to EEPROM
    writeConfigToEEPROM();
  }
}


void swapPorts() {
  // when the controller starts
  // portIndex = 0
  // DSEL = HIGH
  // MUX0=MUX1=MUX2=0
  // this points to the first logical port and output 2 of the first 
  // BTS7008 switch
  // portIndex starts at 0 but port numbers at 1

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
// Command processing
//-----------------------------------------------------------------------
void processSerialCommand() {
  int cmdval;
  int port;
  int level;
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
      EEPROM.get((port - 1 ) * NAMELENGTH, name);
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
    default:
      break;
  }
}


void setup() {
  DPRINTLN("Setup Start");
  status.reserve(STATUSSIZE);
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
  mcp.begin_I2C();

  // initialize the SHT3x sensor
  sht = sht31.begin(0x44);
  if (sht) {
    DPRINTLN(F("Found Temp/Humid monitor"));
  } else {
    DPRINTLN(F("Temp/Humid monitor not found"));
    boardSignature.remove(boardSignature.indexOf('h'),1);
    boardSignature.remove(boardSignature.indexOf('t'),1);
  }

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

  // initialize the status struct to 0
  memset(&powerBoxStatus, 0, sizeof(powerBoxStatus));
  portIndex = 0;
  portMax = sizeof(powerBoxStatus.portAmps) / sizeof(float);

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

      // get temp and humidity
      if ( sht ) {
        powerBoxStatus.temp = sht31.readTemperature();
        powerBoxStatus.humid = sht31.readHumidity();
      }
#ifdef DEBUG
      //char buf[50];
      //sprintf(buf, "- loop: iV=%d, iI=%d, oI=%d, p=%d, mem=%d", analogRead(VSIN), analogRead(ISIN), analogRead(ISOUT), portIndex, freeMemory());
      //DPRINTLN(buf);
      //DPRINTLN(mcp.readGPIO());
      // DPRINT(F("Mem: "));
      // DPRINTLN(freeMemory());
      // DPRINT(F(" Status "));
      // getStatusString();
      // DPRINTLN(status);
#endif
      FSMState = stateSwap;
      last = now;
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
