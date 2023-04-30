/*-----------------------------------------------------------------------
 * BigPowerBox Board configuration
 * License: GPLv3
 * Michel Moriniaux, 2023
-----------------------------------------------------------------------*/
#ifndef board_h
#define board_h

#include <Arduino.h>
#include "mydefines.h"

// Board signature
//  s: arduino addressable switchable port
//  m: multiplexed switchable port
//  p: pwm port
//  a: Allways-On port
// the following are optional and only appear if they are plugged into the device
//  t: temperature probe
//  h: humidity probe
//  f: temp + humidity probe
// always-on ports always last followed by t then h
String boardSignature = "mmmmmmmmppppaa";
// status string
// 0:0:0:0:0:0:0:0:127:255:195:100:1:1:15.54:15.49:15.42:15.37:15.44:15.49:15.54:15.49:15.39:15.49:15.44:15.37:10.22:10.23:10.07:13.37:-10.00:100.00:-10.00
#define STATUSSIZE      200             // size of thebuffer to hold the status line

//-----------------------------------------------------------------------
// EEPROM structures
//-----------------------------------------------------------------------
//      0       7  15
//      -------------
//    0|PORTNAME 1   |
//   16|PORTNAME 2   |
//    ....
//  208|PORTNAME 14  |
//  224|-------------|
//  ...| config      |
//  ...| space       |
// 1013|             |
//     ---------------

// there are 2 types of config values we want to store in EEPROM
// the first struct is rarely modified so it will live in the first 224 bytes of the EEPROM
// we believe that these will be seldom modified so we can live with the 100k writes 
// limitation of the EEPROM
// Configuration struct to store long lived configs in EEPROM

// The second struct will be modified at each change of a port status so might happen a couple
// times per session. This struct will use the remaining EEPROM space and be written at a
// differnet address of that space at each write. validdata will be chosen so that it has a
// low probability of collisions with other values in the struct.
// Configuration struct to store regularly changed configs in EEPROM
// Struct is 6 bytes long
// IMPROVE: do a dynamic declaration of this struct by using BoardSignature and making portPwm an array
struct config_t {
  byte  currentData;                      // if this is CURRENTCONFIGFLAG then data is valid
  byte  portStatus;                       // bitmap of all port statuses (0: Off, 1: On)
  byte  pwmPorts[4];                      // pwm value of ports 9-12 (0: Off, 255: On or 1: On if port in 's' mode)
  byte  pwmPortMode[4];                   // operation mode of the PWM ports (enum PWMModes)
  byte  pwmPortPreset[4];                 // last max value of the port, allows to store a preset
  byte  pwmPortTempOffset[4];             // adjustable temperature offset for the PWM port in mode 3
};

struct status_t {
    float portAmps[14];                   // size of array must be equal to boardSignature.length() this holds the current for each port
    float inputAmps;
    float inputVolts;
    float temp;
    float humid;
    float dewpoint;
    float tempProbe[5];                   // tempearture reading in C.
    byte  tempProbePort[5];               // i2c muc port on which the probe is found, 255 is used for non mux.
    byte  tempProbeType[5];               // type of probe found. limit them to 5 more would be overkill, like 640k RAM
};

//-----------------------------------------------------------------------
// Digital output pins
//-----------------------------------------------------------------------
// ports 1 - 8 are addresed via port A of an MCP23017 IC.
// they use the adafruit library and are driven by I2C commands
#define PORT1EN             0             // GPA0 en/disable port 1
#define PORT2EN             1             // GPA1 en/disable port 2
#define PORT3EN             2             // GPA2 en/disable port 3
#define PORT4EN             3             // GPA3 en/disable port 4
#define PORT5EN             4             // GPA4 en/disable port 5
#define PORT6EN             5             // GPA5 en/disable port 6
#define PORT7EN             6             // GPA6 en/disable port 7
#define PORT8EN             7             // GPA7 en/disable port 8
// ports 9 - 12 are PWM ports and are driven by the Arduino
#define PORT9EN             3             // D3 PWM port 9
#define PORT10EN            5             // D5 PWM port 10
#define PORT11EN            6             // D6 PWM port 11
#define PORT12EN            9             // D9 PWM port 12

const byte ports2Pin[12] = {PORT1EN, PORT2EN, PORT3EN, PORT4EN, PORT5EN, PORT6EN, PORT7EN, PORT8EN, PORT9EN, PORT10EN, PORT11EN, PORT12EN};

//-----------------------------------------------------------------------
// Analog Input pins
//-----------------------------------------------------------------------
// the first 12 ports are served by 4 BTS7008-2EPA switch ICs which provide a current measurement
// the 2 Always-On ports are measured by inline ammeters CC6900-10A
// the input is measured by an inline ammeter CC6900-30A as well as a voltage divider
// The outputs are multiplexed via 74HC4051 and the input has dedicated Arduino ports
#define ISIN                A2            // I sense In
#define VSIN                A0            // V sense In
#define ISOUT               A1            // I sense Out

//-----------------------------------------------------------------------
// Utility output pins
//-----------------------------------------------------------------------
// The DSEL ( Diagnostic SELect ) pin on BTS7008-2EPA switch ICs selects the port to read amperage
// this pins is mulitplexed to the ICs via 74HC4051 multiplexers 
#define DSEL                4            // DSEL for BTS7008-2EPA switch ICs
// The BTS7008-2EPA IC is selected on the 74HC4051 multiplexer via the 3 following pins
//    the BTS7008 switches are addressed as such:
//      000     Y0  ports 1 & 2
//      001     Y1  ports 3 & 4
//      010     Y2  ports 5 & 6
//      011     Y3  ports 7 & 8
//      100     Y4  PWM ports 9 & 10
//      101     Y5  PWM ports 11 & 12
//      110     Y6  Always-On port 1 ( only for Isense )
//      111     Y7  Always-ON port 2 ( only for Isense )
#define MUX0                10            // Bit/Pin S0 of the 3 bit address
#define MUX1                11            // Bit/Pin S1 of the 3 bit address
#define MUX2                12            // Bit/Pin S2 of the 3 bit address
// The OL ( Open Load ) port allows to diagnosed On but unconnected ports
#define OLEN                2             // D2 Open Load enable

#define CHIPNUM             6             // number of BTS7008-2EPA switches
#define VCC                 5.03          // measured VCC from regulator
#define ROUTIS              1126.0        // measured resistance of the Is voltage divider on BTS7008 used to calibrate Iout 
#define KILIS               5450.0        // KIlIs constant on BTS7008 used to calibrate Iout
#define KINIS               67.0          // multiplication factor for CC6900-30A in mV/A
#define KOUTIS              200.0         // multiplication factor for CC6900-10A in mV/A
#define RDIVIN              14100.0       // total summ of all voltage divider resistors
#define RDIVOUT             4700.0        // resitance of the output resitor of the voltage divider
#define PORT1ON             1             // 00000001 1
#define PORT2ON             2             // 00000010 2
#define PORT3ON             4             // 00000100 4
#define PORT4ON             8             // 00001000 8
#define PORT5ON             16            // 00010000 16
#define PORT6ON             32            // 00100000 32
#define PORT7ON             64            // 01000000 64
#define PORT8ON             128           // 10000000 128
const byte port2bin[8] = {PORT1ON, PORT2ON, PORT3ON, PORT4ON, PORT5ON, PORT6ON, PORT7ON, PORT8ON};

// temperature / Humidity Probe Ids
#define SHT31_0x44          1
#define SHT31_0x45          2
#define AHT10               3 

// Storage management
#define EEPROMNAMEBASE      0             // Base address of the port name config struct in EEPROM
#define EEPROMCONFBASE      224           // base address of the config struct in EEPROM

#endif
