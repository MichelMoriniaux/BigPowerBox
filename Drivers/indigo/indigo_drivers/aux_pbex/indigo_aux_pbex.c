// Copyright (c) 2024 
// All rights reserved.
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHORS 'AS IS' AND ANY EXPRESS
// OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
// GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// version history
// 1.0 by Rohan Salodkar <rohan5sep@gmail.com>

/** INDIGO Power Box Exxxtreme aux driver
 \file indigo_aux_pbex.c
 */

#define DRIVER_VERSION 0x001
#define DRIVER_NAME "indigo_aux_pbex"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/termios.h>
#include <indigo/indigo_driver_xml.h>
#include <indigo/indigo_io.h>

#include "indigo_aux_pbex.h"

//  some basic commands to interact with the switch
char *SOC = ">";				 // Start of Command marker
char *EOC = "#";				 // End of Command marker
char *PINGCOMMAND = ">P#";		 // ping command
char *PINGREPLY = ">POK#";		 // ping reply
char *GETSTATUS = ">S#";		 // status request command
char *GETDESCRIPTION = ">D#";	 // board description request command
static char BoardSignature[128]; // string to store the board geometry
static char deviceName[50];	 // the device name stored on the board
static char hwRevision[10];	 // the HW revision sotred on the board
static char portsonly[50];
#define SWH 0				// switched port type
#define MPX 1				// Multiplexed port type
#define PWM 2				// PWM port type
#define AON 3				// Allways-On port type
#define CURRENT 4			// output Current port type (sensor)
#define INPUTA 5			// Input Current port type (sensor)
#define INPUTV 6			// Input Voltage port type (sensor)
#define TEMP 7				// Temperature port type (sensor)
#define HUMID 8				// Humidity port type (sensor)
#define DEWPOINT 9			// Dewpoint (sensor)
#define MODE 10				// PWM port mode switch
#define SETTEMP 11			// PWM port temperature offset switch
#define UPDATEINTERVAL 2000 // how often to update the status

#define PRIVATE_DATA ((pbex_private_data *)device->private_data)

#define AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY (PRIVATE_DATA->outlet_names_property)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_1 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 0)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_2 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 1)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_3 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 2)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_4 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 3)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_5 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 4)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_6 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 5)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_7 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 6)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_8 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 7)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_9 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 8)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_10 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 9)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_11 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 10)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_12 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 11)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_13 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 12)
#define AUX_SWITCH_POWER_OUTLET_NAME_ITEM_14 (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + 13)

#define AUX_SWITCH_POWER_OUTLETS_PROPERTY (PRIVATE_DATA->power_outlet_property)

#define AUX_PWM_POWER_OUTLETS_PROPERTY (PRIVATE_DATA->variable_power_outlet_property)
#define AUX_PWM_MODES_PROPERTY (PRIVATE_DATA->pwm_configuration_property)
#define AUX_PWM_MODE_ITEM_1				(AUX_PWM_MODES_PROPERTY->items + 0)
#define AUX_PWM_MODE_ITEM_2				(AUX_PWM_MODES_PROPERTY->items + 1)
#define AUX_PWM_MODE_ITEM_3				(AUX_PWM_MODES_PROPERTY->items + 2)
#define AUX_PWM_MODE_ITEM_4				(AUX_PWM_MODES_PROPERTY->items + 3)

#define AUX_PWM_TEMP_OFFSETS_PROPERTY (PRIVATE_DATA->pwm_temperature_offset_property)
#define AUX_PWM_TEMP_OFFSET_ITEM_1					(AUX_PWM_TEMP_OFFSETS_PROPERTY->items + 0)
#define AUX_PWM_TEMP_OFFSET_ITEM_2					(AUX_PWM_TEMP_OFFSETS_PROPERTY->items + 1)
#define AUX_PWM_TEMP_OFFSET_ITEM_3					(AUX_PWM_TEMP_OFFSETS_PROPERTY->items + 2)
#define AUX_PWM_TEMP_OFFSET_ITEM_4					(AUX_PWM_TEMP_OFFSETS_PROPERTY->items + 3)

#define AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY (PRIVATE_DATA->pwm_switches_property)
#define AUX_CURRENT_SENSOR_PROPERTY (PRIVATE_DATA->current_sensor_property)
#define AUX_WEATHER_PROPERTY								(PRIVATE_DATA->weather_property)
#define AUX_WEATHER_TEMPERATURE_ITEM				(AUX_WEATHER_PROPERTY->items + 0)
#define AUX_WEATHER_HUMIDITY_ITEM						(AUX_WEATHER_PROPERTY->items + 1)
#define AUX_WEATHER_DEWPOINT_ITEM						(AUX_WEATHER_PROPERTY->items + 2)

#define AUX_INFO_PROPERTY										(PRIVATE_DATA->info_property)
#define AUX_INFO_VOLTAGE_ITEM								(AUX_INFO_PROPERTY->items + 0)
#define AUX_INFO_CURRENT_ITEM								(AUX_INFO_PROPERTY->items + 1)
#define AUX_INFO_POWER_ITEM									(AUX_INFO_PROPERTY->items + 2)

#define AUX_STATE_PROPERTY								(PRIVATE_DATA->state_property)

#define AUX_ALWAYS_ON_PORTS_PROPERTY (PRIVATE_DATA->always_on_port_property)
#define AUX_ALWAYS_ON_PORTITEM_1					(AUX_ALWAYS_ON_PORTS_PROPERTY->items + 0)
#define AUX_ALWAYS_ON_PORTITEM_2					(AUX_ALWAYS_ON_PORTS_PROPERTY->items + 1)


#define AUX_GROUP "Powerbox"

typedef struct
{
	int handle;
	indigo_timer *aux_timer;
	indigo_property *outlet_names_property;
	indigo_property *power_outlet_property;
	indigo_property *power_outlet_current_property;
	indigo_property *variable_power_outlet_property;
	indigo_property *always_on_port_property;
	indigo_property *pwm_configuration_property;
	indigo_property *pwm_temperature_offset_property;
	indigo_property *pwm_switches_property;
	indigo_property *current_sensor_property;
	indigo_property *weather_property;
	indigo_property *info_property;
	indigo_property *state_property;
	int count;
	int version;

	pthread_mutex_t mutex;
} pbex_private_data;
// ============================================================
typedef struct
{
	bool canWrite;
	bool state;
	short type; // SWH, MPX, PWM, AON
	int port;	// port number
	double value;
	double minvalue;
	double maxvalue;
	char unit;
	char description[128];
	char name[128];
} Feature;
// Array of device features
//
Feature *deviceFeatures = NULL;
int nTotalFeatures = 0;
int portNum = 0;
bool havePWM = false;
static bool pbex_command(indigo_device *device, char *command, char *response, int max);
// Utility routines 
//
int GetNumPorts(char *string)
{
	int i, j;
	char *charsToRemove = "tf#";
	int len = strlen(charsToRemove);
	int idx = 0;

	for (i = 0; string[i] != '\0'; i++)
	{
		int removeChar = 0;
		for (j = 0; j < len; j++)
		{
			if (string[i] == charsToRemove[j])
			{
				removeChar = 1;
				break;
			}
		}
		if (!removeChar)
		{
			string[idx++] = string[i];
		}
	}
	string[idx] = '\0';
	return strlen(string);
}
int GetNumFeaturesToCreateForSensors(char *string)
{
	// D:BigPowerBox:001:mmmmmmmmppppaatffff
	int count = 0;
	// Iterate through the string
	for (int i = 0; string[i] != '\0'; i++)
	{
		// If the current character matches the target, increment the count
		if (string[i] == 't')
		{
			count++;
		}
		else if (string[i] == 'f')
		{
			count += 3;
		}
	}

	return count;
}
int GetNUMPWMPorts(char *string)
{
	// D:BigPowerBox:001:mmmmmmmmppppaatffff
	int count = 0;
	// Iterate through the string
	for (int i = 0; string[i] != '\0'; i++)
	{
		// If the current character matches the target, increment the count
		if (string[i] == 'p')
		{
			count++;
		}
	}

	return count;
}
int GetFirstIndexOf(char *string, char c, int offset)
{
	int len = strlen(string);

	if (offset < 0 || offset >= len)
	{
		return -1; // Invalid startIndex
	}

	for (int i = offset; i < len; i++)
	{
		if (string[i] == c)
		{
			return i; // Found the character
		}
	}

	return -1; // Character not found
}
bool Contains(const char *string, const char *c)
{
	return strstr(string, c) != NULL;
}


/// Return the name of switch device n.

/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <returns>The name of the device</returns>
char *GetSwitchName(short id)
{
	// this method is called by clients like N.i.n.a. every 2s for each port
	// ValidateGetSwitchName", id);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "GetSwitchName %s GetSwitchName(%d)", deviceFeatures[id].name, id);
	return deviceFeatures[id].name;
}

/// Gets the description of the specified switch device. This is to allow a fuller description of
/// the device to be returned, for example for a tool tip.

/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <returns>
/// String giving the device description.
/// </returns>
char *GetSwitchDescription(short id, char *description)
{
	// ValidateGetSwitchDescription", id);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME,"GetSwitchDescription %s GetSwitchDescription(%d)",deviceFeatures[id].description, id);
	return description, deviceFeatures[id].description;
}

/// Reports if the specified switch device can be written to, default true.
/// This is false if the device cannot be written to, for example a limit switch or a sensor.

/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <returns>
/// <c>true</c> if the device can be written to, otherwise <c>false</c>.
/// </returns>
bool CanWrite(short id)
{
	// ValidateCanWrite", id);
	//  default behavour is to report true
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "CanWrite %d CanWrite(%d)", deviceFeatures[id].canWrite, id);
	return deviceFeatures[id].canWrite;
}


/// Return the state of switch device id as a boolean

/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <returns>True or false</returns>
bool GetSwitch(short id)
{
	// ValidateGetSwitch", id);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME,"GetSwitch %d GetSwitch(%d)",deviceFeatures[id].state,id);
	return deviceFeatures[id].state;
}

void SetSwitch(indigo_device *device, short id, bool state)
{
	char command[20] ; // Assuming a maximum length of 20 characters for the command
	// ValidateSetSwitch", id);

	if (!CanWrite(id))
	{
		char str[30];
		snprintf(str, sizeof(str), "SetSwitch(%d) - Cannot Write", id);
		// LogMessageSetSwitch", str);
		//  Assuming MethodNotImplementedException is handled by terminating the program
		//  You may adjust this part based on your error handling mechanism
		// exit(EXIT_FAILURE);
	}

	deviceFeatures[id].state = state;

	if (state)
	{
		if (deviceFeatures[id].type == PWM)
			snprintf(command, sizeof(command), ">W:%02d:255#", id);
		else
			snprintf(command, sizeof(command), ">O:%02d#", id);
	}
	else
	{
		if (deviceFeatures[id].type == PWM)
			snprintf(command, sizeof(command), ">W:%02d:0#", id);
		else
			snprintf(command, sizeof(command), ">F:%02d#", id);
	}
	char response[20];
	pbex_command(device, command, response, sizeof(response));

	char logMessage[50];
	snprintf(logMessage, sizeof(logMessage), "SetSwitch(%d) = %B - %s", id, state, command);
	// LogMessageSetSwitch", logMessage);
}
/// Set a switch device name to a specified value.
/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <param name="name">The name of the device</param>
void SetSwitchName(indigo_device *device, short id, char *name)
{
	// ValidateSetSwitchName", id);
	//  ">M:%02d:%s#" return ">MOK#"
	//  EEPROM dies quick, lets not update it uselessly
	if (strcmp(deviceFeatures[id].name, name) != 0)
	{
		INDIGO_DRIVER_DEBUG(DRIVER_NAME,"SetSwitchName %s SetSwitchName(%d) = %s not modified",name,id,name);
		return;
	}

	char command[20] ;
	char response[20] ;
	sprintf(command, ">M:%02d:%s#", id, name);
	if (id < portNum)
		pbex_command(device, command, response, sizeof(response));

	strcpy(deviceFeatures[id].name, name);
	sprintf(deviceFeatures[id + portNum].name, "%s Current (A)", name);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME,"SetSwitchName SetSwitchName(%d) = %s",id,name);
}
/// Returns the step size that this device supports (the difference between successive values of the device).
/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <returns>The step size for this device.</returns>
double SwitchStep(short id)
{
	//	ValidateSwitchStep", id);
	//	INDIGO_DRIVER_DEBUG(DRIVER_NAME,SwitchStep", $"SwitchStep({id}) - 1.0");
	return 1.0;
}
/// Returns the value for switch device id as a double
/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <returns>The value for this switch, this is expected to be between <see cref="MinSwitchValue"/> and
/// <see cref="MaxSwitchValue"/>.</returns>
double GetSwitchValue(short id)
{
	//	ValidateGetSwitchValue", id);
	//	INDIGO_DRIVER_DEBUG(DRIVER_NAME,GetSwitchValue", $"GetSwitchValue({id}) - {deviceFeatures[id].value}");
	return deviceFeatures[id].value;
}
/// Set the value for this device as a double.
/// <param name="id">The device number (0 to <see cref="MaxSwitch"/> - 1)</param>
/// <param name="value">The value to be set, between <see cref="MinSwitchValue"/> and <see cref="MaxSwitchValue"/></param>
void SetSwitchValue(indigo_device *device, short id, double value)
{
	char command[50];
	INDIGO_DRIVER_DEBUG(DRIVER_NAME,"SetSwitchValue SetSwitchValue(%d) = %f",id,value);
	// ValidateSetSwitchValue", id, value);
	if (!CanWrite(id))
	{
		INDIGO_DRIVER_ERROR(DRIVER_NAME,"SetSwitchValue(%d) - Cannot write", id);	
	}
	else
	{
		deviceFeatures[id].value = value;
		if (value > 0)
		{
			switch (deviceFeatures[id].type)
			{
			case PWM:
				sprintf(command, ">W:%02d:%d#", id, (int)value);
				deviceFeatures[id].value = (int)value;
				break;
			case MODE:
				sprintf(command, ">C:%02d:%d#", deviceFeatures[id].port - 1, (int)value);
				// TODO: modify seviceFeatures[id - 1].type
				if ((int)value == 1)
				{
					deviceFeatures[deviceFeatures[id].port - 1].type = SWH;
					deviceFeatures[deviceFeatures[id].port - 1].maxvalue = 1;
				}
				else
				{
					deviceFeatures[deviceFeatures[id].port - 1].type = PWM;
					deviceFeatures[deviceFeatures[id].port - 1].maxvalue = 255;
				}
				break;
			case SETTEMP:
				sprintf(command, ">T:%02d:%d#", deviceFeatures[id].port - 1, (int)value);
				deviceFeatures[id].value = (int)value;
				break;
			case SWH:
			case MPX:
			default:
				sprintf(command, ">O:%02d#", id);
				deviceFeatures[id].value = (int)value;
				break;
			}
		}
		else
		{
			switch (deviceFeatures[id].type)
			{
			case PWM:
				sprintf(command, ">W:%02d:0#", id);
				deviceFeatures[id].value = (int)value;
				break;
			case MODE:
				sprintf(command, ">C:%02d:%d#", deviceFeatures[id].port - 1, (int)value);
				deviceFeatures[deviceFeatures[id].port - 1].type = PWM;
				deviceFeatures[deviceFeatures[id].port - 1].maxvalue = 255;
				break;
			case SETTEMP:
				sprintf(command, ">T:%02d:%d#", deviceFeatures[id].port - 1, (int)value);
				break;
			case SWH:
			case MPX:
			default:
				sprintf(command, ">F:%02d#", id);
				deviceFeatures[id].value = (int)value;
				break;
			}
		}
		// Make it so!
		char response[128];
		pbex_command(device, command, response, sizeof(response));
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "SetSwitchValue(%d) = %f - %s done",id,value,command);
	}
}

indigo_result CreateStateItems(indigo_device *device)
{
	AUX_STATE_PROPERTY = indigo_init_light_property(NULL, device->name, AUX_POWER_OUTLET_STATE_PROPERTY_NAME, AUX_GROUP, "Power outlet states", INDIGO_OK_STATE, portNum);
	if (AUX_STATE_PROPERTY == NULL) 
		return INDIGO_FAILED;

	for(int i = 0; i < portNum; i++)
	{
		char name[50];
		sprintf(name,"AUX_POWER_OUTLET_STATE_%d_ITEM_NAME",i+1);
		indigo_init_light_item(AUX_STATE_PROPERTY->items + i, 
		name,(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value , 
		deviceFeatures[i].state);
	}

	indigo_define_property(device,AUX_STATE_PROPERTY,NULL);
	indigo_update_property(device,AUX_STATE_PROPERTY,NULL);

	return INDIGO_OK;
}

indigo_result UpdateStateItems(indigo_device *device)
{
	
	for(int i = 0; i < portNum; i++)
	{
		(AUX_STATE_PROPERTY->items + i)->light.value = deviceFeatures[i].state;
	}
	indigo_update_property(device,AUX_STATE_PROPERTY,NULL);
	return INDIGO_OK;
}
indigo_result CreateCurrentSensorPorts(indigo_device *device){

	AUX_SWITCH_POWER_OUTLETS_PROPERTY = indigo_init_switch_property(NULL,device->name,
	"SWITCH_PORT_PROPERTY",AUX_GROUP,"Switchable power outlets",INDIGO_OK_STATE,
	INDIGO_RW_PERM,INDIGO_ANY_OF_MANY_RULE,8);
	if (AUX_SWITCH_POWER_OUTLETS_PROPERTY == NULL)
		return INDIGO_FAILED;

	AUX_CURRENT_SENSOR_PROPERTY = indigo_init_number_property(NULL, device->name, 
	"AUX_CURRENT_SENSOR_PROPERTY", AUX_GROUP, "Output gauges", 
	INDIGO_OK_STATE, INDIGO_RO_PERM, portNum);
	if (AUX_CURRENT_SENSOR_PROPERTY == NULL)
			return INDIGO_FAILED;

	AUX_WEATHER_PROPERTY = indigo_init_number_property(NULL, device->name, 
	"AUX_WEATHER_PROPERTY", AUX_GROUP, "Weather", 
	INDIGO_OK_STATE, INDIGO_RO_PERM, 3);

	if (AUX_WEATHER_PROPERTY == NULL)
			return INDIGO_FAILED;
	
	AUX_INFO_PROPERTY = indigo_init_number_property(NULL, device->name, AUX_INFO_PROPERTY_NAME, 
		AUX_GROUP, "Input gauges", INDIGO_OK_STATE, INDIGO_RO_PERM, 3);
	if (AUX_INFO_PROPERTY == NULL)
		return INDIGO_FAILED;

	AUX_PWM_MODES_PROPERTY = indigo_init_number_property(NULL, device->name, "AUX_PWM_MODES_PROPERTY", AUX_GROUP, "PWM outlet modes", INDIGO_OK_STATE, INDIGO_RW_PERM, 4);
	if (AUX_PWM_MODES_PROPERTY == NULL)
		return INDIGO_FAILED;
			
	AUX_PWM_TEMP_OFFSETS_PROPERTY = indigo_init_number_property(NULL, device->name, "AUX_PWM_TEMP_OFFSETS_PROPERTY", AUX_GROUP, "PWM temperature offsets", INDIGO_OK_STATE, INDIGO_RW_PERM, 4);
	if (AUX_PWM_TEMP_OFFSETS_PROPERTY == NULL)
		return INDIGO_FAILED;

	if(!Contains(BoardSignature,"f")) { AUX_WEATHER_PROPERTY->hidden = true; }
	
	int index = 0;
	int nSwitch = 0;
	int nPWMOffset = 0;
	int nPWMMode = 0;
	for(int i = 0; i < nTotalFeatures; i++){

		if(deviceFeatures[i].type == MPX){
			char name[20] = {0};
			sprintf(name,"SWITCH_PORT_ITEM_%d",nSwitch + 1);
			indigo_init_switch_item((AUX_SWITCH_POWER_OUTLETS_PROPERTY->items + nSwitch),name,
			(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value, deviceFeatures[i].value);
			nSwitch++;
		}

		if(deviceFeatures[i].type == CURRENT){
			char name[20] = {0};
			char label[50];
			sprintf(name,"CURRENT_SENSOR_%d",index + 1);

			indigo_init_number_item((AUX_CURRENT_SENSOR_PROPERTY->items + index),
			name,(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + index)->text.value,deviceFeatures[i].minvalue,
			deviceFeatures[i].maxvalue,0.1,deviceFeatures[i].value);
			index++;
		}

		if(deviceFeatures[i].type == TEMP){
			indigo_init_number_item(AUX_WEATHER_TEMPERATURE_ITEM,
			"AUX_WEATHER_TEMPERATURE_ITEM_NAME",deviceFeatures[i].name,
			deviceFeatures[i].minvalue, deviceFeatures[i].maxvalue,0.1,
			deviceFeatures[i].value);
		}

		if(deviceFeatures[i].type == HUMID){
			indigo_init_number_item(AUX_WEATHER_HUMIDITY_ITEM,
			"AUX_WEATHER_HUMIDITY_ITEM_NAME",deviceFeatures[i].name,
			deviceFeatures[i].minvalue, deviceFeatures[i].maxvalue,
			0.1,deviceFeatures[i].value);
		}

		if(deviceFeatures[i].type == DEWPOINT){
			indigo_init_number_item(AUX_WEATHER_DEWPOINT_ITEM,
			"AUX_WEATHER_DEWPOINT_ITEM_NAME",deviceFeatures[i].name,deviceFeatures[i].minvalue,
			deviceFeatures[i].maxvalue,0.1,deviceFeatures[i].value);
		}

		double power = 0.0;
		if(deviceFeatures[i].type == INPUTA){
			indigo_init_number_item(AUX_INFO_CURRENT_ITEM, AUX_INFO_CURRENT_ITEM_NAME, 
			deviceFeatures[i].name, 0, 20, 0.1,deviceFeatures[i].value);
			power = deviceFeatures[i].value;
		}

		if(deviceFeatures[i].type == INPUTV){
			indigo_init_number_item(AUX_INFO_VOLTAGE_ITEM, AUX_INFO_VOLTAGE_ITEM_NAME, 
			deviceFeatures[i].name, 0, 20, 0.1,deviceFeatures[i].value);
			power = power * deviceFeatures[i].value;
		}

		indigo_init_number_item(AUX_INFO_POWER_ITEM, AUX_INFO_POWER_ITEM_NAME, 
			"Power [W]", 0, 200, 0.1, power);

		if (deviceFeatures[i].type == MODE)
		{
			char name[50];
			char label[200];
			sprintf(name,"AUX_PWM_MODE_ITEM_%d",nPWMMode + 1);
			sprintf(label,"PWM mode %d\n0: variable, 1:on/off, 2: dew heater, 3: temperature PID",nPWMMode + 1);
			indigo_init_number_item((AUX_PWM_MODES_PROPERTY->items + nPWMMode),name, 
			label,0,3,1,deviceFeatures[i].value);
			nPWMMode++;
		}	
		
		if (deviceFeatures[i].type == SETTEMP)
		{
			char name[50];
			char label[200];
			sprintf(name,"AUX_PWM_TEMP_OFFSET_ITEM_%d",nPWMOffset + 1);
			sprintf(label,"PWM temperature offset %d",nPWMMode + 1);
			indigo_init_number_item((AUX_PWM_TEMP_OFFSETS_PROPERTY->items + nPWMOffset),
			name, label,0,10,1,deviceFeatures[i].value);
			nPWMOffset++;
		}
	}

	indigo_define_property(device,AUX_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
	indigo_update_property(device,AUX_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
	indigo_define_property(device,AUX_INFO_PROPERTY,NULL);
	indigo_update_property(device,AUX_INFO_PROPERTY,NULL);
	indigo_define_property(device,AUX_WEATHER_PROPERTY,NULL);
	indigo_update_property(device,AUX_WEATHER_PROPERTY,NULL);
	indigo_define_property(device,AUX_CURRENT_SENSOR_PROPERTY,NULL);
	indigo_update_property(device,AUX_CURRENT_SENSOR_PROPERTY,NULL);
	indigo_define_property(device, AUX_PWM_MODES_PROPERTY, NULL);
	indigo_define_property(device, AUX_PWM_TEMP_OFFSETS_PROPERTY, NULL);

	return INDIGO_OK;
}
void QueryDeviceStatus(indigo_device *device);

indigo_result UpdatePWMModeItems(indigo_device *device){
	
	int nItem = 0;
	for(int i = 0; i < nTotalFeatures; i++){
		if(deviceFeatures[i].type == MODE){
			(AUX_PWM_MODES_PROPERTY->items + nItem)->number.value = deviceFeatures[i].value;
			nItem++;
		}
	}

	indigo_update_property(device,AUX_PWM_MODES_PROPERTY,NULL);
}
indigo_result UpdateSwitchItems(indigo_device *device)
{
	int nSwitch = 0;
	for(int i = 0; i < portNum; i++){
		if(deviceFeatures[i].type == MPX){

			(AUX_SWITCH_POWER_OUTLETS_PROPERTY->items + nSwitch)->sw.value = deviceFeatures[i].value;
			nSwitch++;
		}
	}

	indigo_update_property(device,AUX_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
}
indigo_result UpdateDisplayItems(indigo_device *device)
{
	QueryDeviceStatus(device);
	int index = 0;
	int nAON = 0;
	int nTempOffset = 0;
	for(int i = 0; i < nTotalFeatures; i++){

		if(deviceFeatures[i].type == CURRENT){
			(AUX_CURRENT_SENSOR_PROPERTY->items + index)->number.value = deviceFeatures[i].value;
			index++;
		}

		if(deviceFeatures[i].type == AON){
			(AUX_ALWAYS_ON_PORTS_PROPERTY->items + nAON)->number.value = deviceFeatures[i].value;
			nAON++;
		}

		if(deviceFeatures[i].type == SETTEMP){
			(AUX_PWM_TEMP_OFFSETS_PROPERTY->items + nTempOffset)->number.value = deviceFeatures[i].value;
			nTempOffset++;
		}

		if(deviceFeatures[i].type == TEMP){
			AUX_WEATHER_TEMPERATURE_ITEM->number.value = deviceFeatures[i].value;
		}

		if(deviceFeatures[i].type == HUMID){
			AUX_WEATHER_HUMIDITY_ITEM->number.value = deviceFeatures[i].value;
		}

		if(deviceFeatures[i].type == DEWPOINT){
			AUX_WEATHER_DEWPOINT_ITEM->number.value = deviceFeatures[i].value;
		}

		if(deviceFeatures[i].type == INPUTA){
			AUX_INFO_CURRENT_ITEM->number.value = deviceFeatures[i].value;
		}

		if(deviceFeatures[i].type == INPUTV){
			AUX_INFO_VOLTAGE_ITEM->number.value = deviceFeatures[i].value;
		}


		AUX_INFO_POWER_ITEM->number.value = AUX_INFO_CURRENT_ITEM->number.value * 
		AUX_INFO_VOLTAGE_ITEM->number.value;
	}
	
	indigo_update_property(device,AUX_INFO_PROPERTY,NULL);
	indigo_update_property(device,AUX_CURRENT_SENSOR_PROPERTY,NULL);
	indigo_update_property(device,AUX_WEATHER_PROPERTY,NULL);
	indigo_update_property(device,AUX_ALWAYS_ON_PORTS_PROPERTY,NULL);
	indigo_update_property(device,AUX_PWM_TEMP_OFFSETS_PROPERTY,NULL);

	return INDIGO_OK;
}

indigo_result ReCreatePWMPorts(indigo_device *device)
{
	if(!deviceFeatures){ return INDIGO_FAILED; }

	int var[4] = {-1};
	int sw[4] = {-1};
	int numVar = 0;
	int numSw = 0;
	for(size_t i = 0; i < portNum; i++){
		if(deviceFeatures[i].type == PWM){	
			var[numVar++] = i;
		}else if(deviceFeatures[i].type == SWH){
			sw[numSw++] = i;
		}
	}

	if(numVar > 0){
		if(AUX_PWM_POWER_OUTLETS_PROPERTY == NULL){
			AUX_PWM_POWER_OUTLETS_PROPERTY = indigo_init_number_property(NULL, device->name, "AUX_PWM_POWER_OUTLETS_PROPERTY", AUX_GROUP, "PWM power outlets", INDIGO_OK_STATE, INDIGO_RW_PERM, numVar);
			if (AUX_PWM_POWER_OUTLETS_PROPERTY == NULL)
				return INDIGO_FAILED;
		}else{
			indigo_delete_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY,NULL);
			AUX_PWM_POWER_OUTLETS_PROPERTY = indigo_init_number_property(NULL, device->name, "AUX_PWM_POWER_OUTLETS_PROPERTY", AUX_GROUP, "PWM power outlets", INDIGO_OK_STATE, INDIGO_RW_PERM, numVar);
			if (AUX_PWM_POWER_OUTLETS_PROPERTY == NULL)
				return INDIGO_FAILED;
		}

		for(int item = 0; item < numVar; item++){
			char name[20] = {0};
			char label[20] = {0};
			sprintf(name,"OUTLET_%d",var[item]);
			indigo_init_number_item(AUX_PWM_POWER_OUTLETS_PROPERTY->items + item, 
			name, (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + (var[item]))->text.value, 
			0, 255, 1, deviceFeatures[var[item]].value);
		}		
		indigo_define_property(device,AUX_PWM_POWER_OUTLETS_PROPERTY,NULL);
		
	}else{
		indigo_delete_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY,NULL);
	}
	// PWM switches 
	//
	if(numSw > 0){
		if(AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY == NULL){
			AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY = indigo_init_switch_property(NULL, device->name, 
			"AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY", AUX_GROUP, "PWM Switches", INDIGO_OK_STATE, 
			INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE,numSw);
			if (AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY == NULL)
				return INDIGO_FAILED;
		}else{
			indigo_delete_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
			AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY = indigo_init_switch_property(NULL, device->name, 
			"AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY", AUX_GROUP, "PWM Switches", INDIGO_OK_STATE, 
			INDIGO_RW_PERM, INDIGO_ANY_OF_MANY_RULE,numSw);
			if (AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY == NULL)
				return INDIGO_FAILED;
		}
	
		for(int item = 0; item < numSw; item++){
			char name[20] = {0};
			char label[20] = {0};
			sprintf(name,"OUTLET_%d",sw[item]);
			sprintf(label,"PWM Switch %d",sw[item]);
			indigo_init_switch_item(AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY->items + item, 
			name, (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + (sw[item]))->text.value,
			deviceFeatures[sw[item]].state );
		}
		indigo_define_property(device,AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
		
	}else{
		indigo_delete_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
	}
	indigo_update_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY,NULL);
	indigo_update_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);

	return INDIGO_OK;
}
/// Queries the device PWM ports and updates the driver's internal datastructures
/// TODO: completely redo this method as it is not aligned with the rest
indigo_result QueryPWMPorts(indigo_device *device) {
    char response[50];
    char *words[10];
    char* token;

    for (int i = 0; i < nTotalFeatures; i++) {
        if (deviceFeatures[i].type == MODE) {
            char command[50];
            sprintf(command, ">G:%02d#", deviceFeatures[i].port - 1);
            
			if(!pbex_command(device,command,response,sizeof(response)))
			{
				return INDIGO_FAILED;
			}

            int j = 0;
            token = strtok(response, ":");
            while (token != NULL) {
                words[j++] = token;
                token = strtok(NULL, ":");
            }
            deviceFeatures[i].value = atof(words[2]);

            if (deviceFeatures[i].value == 1) {
                deviceFeatures[deviceFeatures[i].port - 1].type = SWH;
                deviceFeatures[deviceFeatures[i].port - 1].maxvalue = 1;
            }
            deviceFeatures[i].state = true;
            snprintf(deviceFeatures[i].name, sizeof(deviceFeatures[i].name), "%s Mode", deviceFeatures[deviceFeatures[i].port - 1].name);
            //tl_LogMessageQueryPWMPorts", "switch %d mode %f", i, deviceFeatures[i].value);
        }

        if (deviceFeatures[i].type == SETTEMP) {
            char command[50];
            sprintf(command, ">H:%02d#", deviceFeatures[i].port - 1);

			if(!pbex_command(device,command,response,sizeof(response)))
			{
				return INDIGO_FAILED;
			}

            int j = 0;
            token = strtok(response, ":");
            while (token != NULL) {
                words[j++] = token;
                token = strtok(NULL, ":");
            }
            deviceFeatures[i].value = atof(words[2]);
            deviceFeatures[i].state = true;
            snprintf(deviceFeatures[i].name, sizeof(deviceFeatures[i].name), "%s Temperature Offset", deviceFeatures[deviceFeatures[i].port - 1].name);
            //tl_LogMessageQueryPWMPorts", "switch %d offset %f", i, deviceFeatures[i].value);
        }
    }
}
Feature *QueryDeviceDescription(indigo_device *device)
{
	char response[128] = {'\n'} ;

	memset(response,'\0',sizeof(response));	
	// response should be of the form:
	// D:BigPowerBox:001:mmmmmmmmppppaatffff
	//
	if (pbex_command(device, GETDESCRIPTION, response, sizeof(response)))
	{
		char words[4][128]; // Array to store split substrings

		char *token = strtok(response, ":"); // Split the string based on colon ':'
		int count = 0;
		while (token != NULL && count < 4 /*sanity check*/)
		{
			strcpy(words[count], token);
			count++;
			token = strtok(NULL, ":");
		}

		if (strcmp(words[0], ">D") != 0)
		{
			INDIGO_DRIVER_ERROR(DRIVER_NAME,"QueryDeviceDescription Invalid response from device: %s",GETDESCRIPTION);
		}
		else
		{
			strcpy(deviceName, words[1]);
			strcpy(hwRevision, words[2]);
			strcpy(BoardSignature, words[3]);
		}
	}
	else
	{
		INDIGO_DRIVER_ERROR(DRIVER_NAME,"QueryDeviceDescription No response from device: [command %s",response);
	}
	// Allocate features
	// Compute number of features n = ports * 2 + 2 + PWM ports * 2 + temp sensors
	//
	strcpy(portsonly, BoardSignature);
	portNum = GetNumPorts(portsonly);
	nTotalFeatures = portNum * 2 + 2 + GetNUMPWMPorts(portsonly) * 2 +
			GetNumFeaturesToCreateForSensors(BoardSignature);
	Feature *features = (Feature *)calloc(nTotalFeatures, sizeof(Feature));
	int switchable = 1;

	int pwm = 1;
	int ao = 1;

	short portindex = 0;
	// translate BoardSignature into an array of Features_t
	// first the electrical ports, the board signature has the types of ports in order followed by
	// the optional temp and humidity sensors. We also need to add the input Amps and Input Volts that
	// do not appear in the signature
	// so in order: port statuses, port currents, input A, input V, Temp, Hunidity
	// first create a new string without temps and humid
	for (int i = 0; i < portNum; i++)
	{
		// create all ports as status = false (off), they will be updated later by QueryDeviceStatus()
		switch (portsonly[i])
		{
		case 's':
			// normal switch port, is RW bool
			features[i].canWrite = true;
			features[i].state = false;
			features[i].type = SWH;
			features[i].port = i + 1;
			features[i].value = 0;
			features[i].minvalue = 0;
			features[i].maxvalue = 1;
			features[i].unit = ' ';
			sprintf(features[i].description, "Switchable Port %d", switchable++);
			sprintf(features[i].name, "port %d", (i + 1));
			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added SWH port at index: %d",portindex++);
			break;
		case 'm':
			// multiplexed switch port, is RW bool
			features[i].canWrite = true;
			features[i].state = false;
			features[i].type = MPX;
			features[i].port = i + 1;
			features[i].value = 0;
			features[i].minvalue = 0;
			features[i].maxvalue = 1;
			features[i].unit = ' ';
			sprintf(features[i].description, "Switchable Port %d", switchable++);
			sprintf(features[i].name, "port %d" + (i + 1));
			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added MPX port at index: %d", portindex++);
			break;
		case 'p':
			// pwm switch port, is RW analog
			features[i].canWrite = true;
			features[i].state = false;
			features[i].type = PWM;
			features[i].port = i + 1;
			features[i].value = 0;
			features[i].minvalue = 0;
			features[i].maxvalue = 255;
			features[i].unit = ' ';
			sprintf(features[i].description, "PWM Port %d", pwm);
			sprintf(features[i].name, "PWM port %d", pwm++);

			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added PWM port at index: %d",portindex++);
			havePWM = true;
			break;
		case 'a':
			// Allways-On port, is RO analog
			// do we really need this?
			features[i].canWrite = false;
			features[i].state = false;
			features[i].type = AON;
			features[i].port = i + 1;
			features[i].value = 0;
			features[i].minvalue = 0;
			features[i].maxvalue = 1;
			features[i].unit = ' ';
			sprintf(features[i].description, "Always-On Port %d", ao++);
			sprintf(features[i].name, "AO port %d" + (i + 1));

			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "QueryDeviceDescription Added AON port at index: %d", portindex++);
			break;
		}
	}
	int index = portNum;
	// now again lets loop to create "ports" for the output current sensors
	for (int i = 0; i < portNum; i++)
	{
		features[index].canWrite = false;
		features[index].state = true;
		features[index].type = CURRENT;
		features[index].port = i + 1;
		features[index].value = 0;
		features[index].minvalue = 0;
		features[index].maxvalue = 50.00;
		features[index].unit = 'A';
		strcpy(features[index].description, "Output Current Sensor");
		sprintf(features[index].name, "port %d Amps", (i + 1));
		index++;
		INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added CURRENT port at index: %d", portindex++);
	}
	// now create "port" for the input current sensor
	{
		features[index].canWrite = false;
		features[index].state = true;
		features[index].type = INPUTA;
		features[index].port = portNum + 1;
		features[index].value = 0;
		features[index].minvalue = 0;
		features[index].maxvalue = 50.00;
		features[index].unit = 'A';
		strcpy(features[index].description, "Input current sensor");
		strcpy(features[index].name, "Input amps");

		INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added INPUT CURRENT port at index: %d",portindex++);
	}
	// now create "port" for the input voltage sensor
	{
		index++;
		features[index].canWrite = false;
		features[index].state = true;
		features[index].type = INPUTV;
		features[index].port = portNum + 2;
		features[index].value = 0;
		features[index].minvalue = 0;
		features[index].maxvalue = 50.00;
		features[index].unit = 'V';
		strcpy(features[index].description, "Input voltage sensor");
		strcpy(features[index].name, "Input volts");

		INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added INPUT VOLT port at index: %d",portindex++);
	}
	// if we have PWM ports lets add the mode and offset selectors
	if (havePWM)
	{
		pwm = 1;

		int firstPWMPortIndex = GetFirstIndexOf(portsonly, 'p', 0);
		while (GetFirstIndexOf(portsonly, 'p', firstPWMPortIndex) != -1)
		{
			index++;
			features[index].canWrite = true;
			features[index].state = false;
			features[index].type = MODE;
			features[index].port = firstPWMPortIndex + 1;
			features[index].value = 0;
			features[index].minvalue = 0;
			features[index].maxvalue = 3;
			features[index].unit = ' ';
			sprintf(features[index].description, "PWM Port %d Mode (0: variable, 1: on/off, 2:Dewheater, 3:temperature PID", pwm);
			sprintf(features[index].name, "PWM Port %d Mode", pwm);

			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added MODE port at index: %d",portindex++);
			index++;
			features[index].canWrite = true;
			features[index].state = false;
			features[index].type = SETTEMP;
			features[index].port = firstPWMPortIndex + 1;
			features[index].value = 0;
			features[index].minvalue = 0;
			features[index].maxvalue = 10;
			features[index].unit = ' ';
			sprintf(features[index].description, "PWM Port %d Temp Offset", pwm);
			sprintf(features[index].name, "PWM Port %d Offset", pwm++);

			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added SETTEMP port at index: %d",portindex++);
			firstPWMPortIndex++;
		}
	}
	// now lets add "ports" for the temp and humidity sensors if they are present
	if (Contains(BoardSignature, "f"))
	{
		index++;
		features[index].canWrite = false;
		features[index].state = true;
		features[index].type = TEMP;
		features[index].port = portNum + 3;
		features[index].value = 0;
		features[index].minvalue = -100.00;
		features[index].maxvalue = 200.00;
		features[index].unit = 'C';
		strcpy(features[index].description, "Environment temperature sensor");
		strcpy(features[index].name, "Environment temperature");

		INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added ENV TEMP port at index: %d", portindex++);
		index++;
		features[index].canWrite = false;
		features[index].state = true;
		features[index].type = HUMID;
		features[index].port = portNum + 4;
		features[index].value = 0;
		features[index].minvalue = 0;
		features[index].maxvalue = 100;
		features[index].unit = '%';
		strcpy(features[index].description, "Environment humidity sensor");
		strcpy(features[index].name, "Environment humidity");

		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "QueryDeviceDescription Added ENV HUMID port at index: %d", portindex++);
		index++;
		features[index].canWrite = false;
		features[index].state = true;
		features[index].type = DEWPOINT;
		features[index].port = portNum + 5;
		features[index].value = 0;
		features[index].minvalue = -100;
		features[index].maxvalue = 200;
		features[index].unit = 'C';
		strcpy(features[index].description, "Environment dewpoint");
		strcpy(features[index].name, "Environment dewpoint");

		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "QueryDeviceDescription Added ENV DEW port at index: %d", portindex++);
	}
	if (Contains(BoardSignature, "t"))
	{
		int port = 1;
		int i = GetFirstIndexOf(BoardSignature, 't', 0);
		while (GetFirstIndexOf(BoardSignature, 't', i++) != -1)
		{
			index++;
			features[index].canWrite = false;
			features[index].state = true;
			features[index].type = TEMP;
			features[index].port = port;
			features[index].value = 0;
			features[index].minvalue = -100.00;
			features[index].maxvalue = 200.00;
			features[index].unit = 'C';
			sprintf(features[index].description, "Temperature Sensor for PWM port %d", port);
			sprintf(features[index].name, "Temperature %d", port++);

			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Added TEMP port at index: %d",portindex++);
		}
	}
	// the number of "switches" we want the client to display in the UI ( relates to MaxSwitches )

	INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceDescription Total number of ports found: %d",nTotalFeatures);

	return features;
}
/// Queries the device for a status string and updates the driver's internal datastructures
void QueryDeviceStatus(indigo_device *device)
{
	// CheckConnected("QueryDeviceStatus");
	// we do not want to query the status if we do not know the device's board signature so populate this first
	if (deviceFeatures == NULL)
	{
		deviceFeatures = QueryDeviceDescription(device);
	}

	char response[500];
	INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus Sending request to device...");
	if (pbex_command(device, GETSTATUS, response, sizeof(response)))
	{
		INDIGO_DRIVER_DEBUG(DRIVER_NAME, "QueryDeviceStatus Status string: %s", response);
		// response should be like:
		// S:0:0:0:0:0:0:0:0:0:0:0:0:8.87:7.19:6.29:5.96:5.89:5.94:5.94:5.94:5.91:5.84:5.82:5.77:0.00:0.00:0.08:3.61:0.00:0.00

		char words[128][10];
		char *token = strtok(response, ":"); // Split the string based on colon ':'
		int count = 0;
		while (token != NULL && count < 128 /*sanity check*/)
		{
			strcpy(words[count], token);
			count++;
			token = strtok(NULL, ":");
		}
		if (strcmp(words[0], ">S") != 0)
		{
			INDIGO_DRIVER_ERROR(DRIVER_NAME,"QueryDeviceStatus Invalid response from device: %s", response);	
		}
		else
		{
			// populate the deviceFeatures List with the status values
			// first iterate through the ports to update the port values (OFF/ON/dutycycle level)
			int index = 1;
			for (int i = 0; i < portNum; i++)
			{
				if (portsonly[i] == 'm' || portsonly[i] == 's' || portsonly[i] == 'a')
				{
					deviceFeatures[i].state = strcmp(words[index], "0") == 0 ? false : true;
					if (deviceFeatures[i].state)
						deviceFeatures[i].value = 255;
					else
						deviceFeatures[i].value = 0;
				}
				if (portsonly[i] == 'p')
				{
					double value = atof(words[index]);
					if (atof(words[index]) == 0.0)
						deviceFeatures[i].state = false;
					else
						deviceFeatures[i].state = true;
					deviceFeatures[i].value = value;
				}
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "QueryDeviceStatus switch %d value %f", i,  deviceFeatures[i].value);
				index++;
			}
			// now iterate through the ports to update the current sensors
			for (int i = 0; i < portNum; i++)
			{
				int j = i + portNum;
				deviceFeatures[j].state = true;
				deviceFeatures[j].value = atof(words[index]);
				INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f", j, deviceFeatures[j].value);
				index++;
			}
			// now do the input ports
			int p = portNum * 2;
			deviceFeatures[p].state = true;
			deviceFeatures[p].value = atof(words[index]);
			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f",p, deviceFeatures[p].value);
			index++;
			p++;
			deviceFeatures[p].state = true;
			deviceFeatures[p].value = atof(words[index]);
			INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f",p, deviceFeatures[p].value);
			index++;
			p++;

			// now skip the PWM port modes and offsets if they exist
			if (havePWM)
			{
				p += (2 * GetNUMPWMPorts(BoardSignature));
				INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus skipped PWM ports");
			}
			// and finaly the temp and humid sensors if they are present in the board signature
			// the board will report 'f' and 't' only if an SHT31 or AHT10 sensor is attached at power-on
			if (Contains(BoardSignature, "f"))
			{
				// temperature
				deviceFeatures[p].state = true;
				deviceFeatures[p].value = atof(words[index++]);
				INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f",p, deviceFeatures[p].value);
				//  humidity
				deviceFeatures[++p].state = true;
				deviceFeatures[p].value = atof(words[index++]);
				INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f",p, deviceFeatures[p].value);
				//  dewpoint
				deviceFeatures[++p].state = true;
				deviceFeatures[p].value = atof(words[index++]);
				INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f",p, deviceFeatures[p].value);
				p++;
			}
			if (Contains(BoardSignature, "t"))
			{
				int i = GetFirstIndexOf(BoardSignature, 't', 0);
				while (GetFirstIndexOf(BoardSignature, 't', i++) != -1)
				{
					deviceFeatures[p].state = true;
					deviceFeatures[p].value = atof(words[index++]);
					INDIGO_DRIVER_DEBUG(DRIVER_NAME,"QueryDeviceStatus switch %d value %f",p, deviceFeatures[p].value);
					p++;
				}
			}
		}
	}
	else
	{
		INDIGO_DRIVER_ERROR( DRIVER_NAME, "QueryDeviceStatus Invalid response from device: %s", response);
	}
}

int indigo_read_line_local(int handle, char *buffer, int length) {
	char c = '\0';
	long total_bytes = 0;
	while (total_bytes < length) {
#if defined(INDIGO_WINDOWS)
		long bytes_read = recv(handle, &c, 1, 0);
		if (bytes_read == -1 && WSAGetLastError() == WSAETIMEDOUT) {
			Sleep(500);
			continue;
		}
#else
		long bytes_read = read(handle, &c, 1);
#endif
		if (bytes_read > 0) {
			if (c == '\r')
				;
			else if (c != '\n')
				buffer[total_bytes++] = c;
			else
				break;
		} else if( bytes_read == 0){
			break;
		} 
		else {
			errno = ECONNRESET;
			INDIGO_TRACE_PROTOCOL(indigo_trace("%d -> // Connection reset", handle));
			return -1;
		}
	}
	buffer[total_bytes] = '\0';
	INDIGO_TRACE_PROTOCOL(indigo_trace("%d -> %s", handle, buffer));
	return (int)total_bytes;
}
// -------------------------------------------------------------------------------- Low level communication routines

static bool pbex_command(indigo_device *device, char *command, char *response, int max)
{	
	tcflush(PRIVATE_DATA->handle, TCIOFLUSH);	
	bool result  = indigo_write(PRIVATE_DATA->handle, command, strlen(command));

	if (response != NULL && result)
	{
		if (indigo_read_line_local(PRIVATE_DATA->handle, response, max) == -1)
		{
			INDIGO_DRIVER_ERROR(DRIVER_NAME,"pbex_command: %s\n",strerror(errno));
			return false;
		}
	}
	
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Command %s -> %s", command, response != NULL ? response : "NULL");

	return true;
}

static void pbex_open(indigo_device *device)
{
	char response[128];
	PRIVATE_DATA->handle = indigo_open_serial(DEVICE_PORT_ITEM->text.value);
	if (PRIVATE_DATA->handle > 0)
	{
		int attempt = 0;
		while (true)
		{
			if (pbex_command(device, PINGCOMMAND, response, 5))
			{ // 5 is number of bytes in >POK#

				if (strncmp(response, ">POK#", 5) == 0)
				{
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "Connected to PBEX %s", DEVICE_PORT_ITEM->text.value);
					PRIVATE_DATA->version = 1;
					break;
				}
			}
			if (attempt++ == 3)
			{

				INDIGO_DRIVER_ERROR(DRIVER_NAME, "PBEX not detected");
				break;
			}

			INDIGO_DRIVER_ERROR(DRIVER_NAME, "PBEX not detected - retrying in 5 seconds...");
			indigo_usleep(ONE_SECOND_DELAY * 5);
		}
	}
}
// -------------------------------------------------------------------------------- INDIGO aux device implementation
static indigo_result aux_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property);

static indigo_result aux_attach(indigo_device *device)
{
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);
	if (indigo_aux_attach(device, DRIVER_NAME, DRIVER_VERSION, INDIGO_INTERFACE_AUX_POWERBOX) == INDIGO_OK)
	{
		INFO_PROPERTY->count = 7;
		strcpy(INFO_DEVICE_MODEL_ITEM->text.value, "Unknown");
		strcpy(INFO_DEVICE_FW_REVISION_ITEM->text.value, "Unknown");
		strcpy(INFO_DEVICE_HW_REVISION_ITEM->text.value,"Unknown");
		// -------------------------------------------------------------------------------- OUTLET_NAMES
		AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY = indigo_init_text_property(NULL, device->name, AUX_OUTLET_NAMES_PROPERTY_NAME, AUX_GROUP, "Outlet names", INDIGO_OK_STATE, INDIGO_RW_PERM, 14);
		if (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY == NULL)
			return INDIGO_FAILED;

		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_1, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_1", "Switchable power outlet 1", "Switchable power outlet 1");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_2, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_2", "Switchable power outlet 2", "Switchable power outlet 2");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_3, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_3", "Switchable power outlet 3", "Switchable power outlet 3");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_4, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_4", "Switchable power outlet 4", "Switchable power outlet 4");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_5, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_5", "Switchable power outlet 5", "Switchable power outlet 5");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_6, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_6", "Switchable power outlet 6", "Switchable power outlet 6");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_7, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_7", "Switchable power outlet 7", "Switchable power outlet 7");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_8, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_8", "Switchable power outlet 8", "Switchable power outlet 8");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_9, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_9", 	"PWM outlet 1", "PWM outlet 1");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_10, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_10", "PWM outlet 2", "PWM outlet 2");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_11, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_11", "PWM outlet 3", "PWM outlet 3");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_12, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_12", "PWM outlet 4", "PWM outlet 4");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_13, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_13", "Always on power outlet 1", "Always on power outlet 1");
		indigo_init_text_item(AUX_SWITCH_POWER_OUTLET_NAME_ITEM_14, "AUX_SWITCH_POWER_OUTLET_NAME_ITEM_14", "Always on power outlet 2", "Always on power outlet 2");	

		AUX_ALWAYS_ON_PORTS_PROPERTY = indigo_init_number_property(NULL,device->name,"AUX_ALWAYS_ON_PORTS_PROPERTY", 
			AUX_GROUP,"Always on power outlets",INDIGO_OK_STATE,INDIGO_RO_PERM,2);

		if (AUX_ALWAYS_ON_PORTS_PROPERTY == NULL)
			return INDIGO_FAILED;
		indigo_init_number_item(AUX_ALWAYS_ON_PORTITEM_1,"AUX_ALWAYS_ON_PORTITEM_1","Always on power outlet 1",255,255,255,255);
		indigo_init_number_item(AUX_ALWAYS_ON_PORTITEM_2,"AUX_ALWAYS_ON_PORTITEM_2","Always on power outlet 2",255,255,255,255);

		// -------------------------------------------------------------------------------- DEVICE_PORT, DEVICE_PORTS
		ADDITIONAL_INSTANCES_PROPERTY->hidden = DEVICE_CONTEXT->base_device != NULL;
		DEVICE_PORT_PROPERTY->hidden = false;
		DEVICE_PORTS_PROPERTY->hidden = false;
#ifdef INDIGO_MACOS
		for (int i = 0; i < DEVICE_PORTS_PROPERTY->count; i++)
		{
			if (strstr(DEVICE_PORTS_PROPERTY->items[i].name, "usbserial"))
			{
				indigo_copy_value(DEVICE_PORT_ITEM->text.value, DEVICE_PORTS_PROPERTY->items[i].name);
				break;
			}
		}
#endif
#ifdef INDIGO_LINUX
		strcpy(DEVICE_PORT_ITEM->text.value, "/dev/ttyPBEX");
#endif
		// --------------------------------------------------------------------------------
		pthread_mutex_init(&PRIVATE_DATA->mutex, NULL);
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		return aux_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static indigo_result aux_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property)
{
	if (IS_CONNECTED)
	{
		if (indigo_property_match(AUX_SWITCH_POWER_OUTLETS_PROPERTY, property))
			indigo_define_property(device, AUX_SWITCH_POWER_OUTLETS_PROPERTY, NULL);

		if (indigo_property_match(AUX_PWM_POWER_OUTLETS_PROPERTY, property)){
		 	indigo_define_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);
		}
		if (indigo_property_match(AUX_PWM_MODES_PROPERTY, property))
			indigo_define_property(device, AUX_PWM_MODES_PROPERTY, NULL);

		if (indigo_property_match(AUX_PWM_TEMP_OFFSETS_PROPERTY, property))
			indigo_define_property(device, AUX_PWM_TEMP_OFFSETS_PROPERTY, NULL);

		if (indigo_property_match(AUX_ALWAYS_ON_PORTS_PROPERTY, property))
			indigo_define_property(device, AUX_ALWAYS_ON_PORTS_PROPERTY, NULL);	
	
		if (indigo_property_match(AUX_WEATHER_PROPERTY, property))
			indigo_define_property(device, AUX_WEATHER_PROPERTY, NULL);		
		
		if (indigo_property_match(AUX_INFO_PROPERTY, property))
			indigo_define_property(device, AUX_INFO_PROPERTY, NULL);

		if (indigo_property_match(AUX_CURRENT_SENSOR_PROPERTY, property))
			indigo_define_property(device, AUX_CURRENT_SENSOR_PROPERTY, NULL);
		
		if (indigo_property_match(AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, property))
			indigo_define_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, NULL);

		if (indigo_property_match(AUX_STATE_PROPERTY, property))
			indigo_define_property(device, AUX_STATE_PROPERTY, NULL);

	}
	if (indigo_property_match(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY, property))
		indigo_define_property(device, AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY, NULL);
	return indigo_aux_enumerate_properties(device, NULL, NULL);
}

static void aux_timer_callback(indigo_device *device)
{
	if (!IS_CONNECTED)
		return;

	pthread_mutex_lock(&PRIVATE_DATA->mutex);
	UpdateDisplayItems(device);
	UpdateStateItems(device);
	indigo_reschedule_timer(device, 30, &PRIVATE_DATA->aux_timer);
	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
}

static void aux_connection_handler(indigo_device *device)
{
	char response[128];
	indigo_lock_master_device(device);
	pthread_mutex_lock(&PRIVATE_DATA->mutex);
	if (CONNECTION_CONNECTED_ITEM->sw.value)
	{
		if (PRIVATE_DATA->count++ == 0)
		{
			pbex_open(device);
		}

		if (PRIVATE_DATA->handle > 0)
		{
			indigo_define_property(device, AUX_ALWAYS_ON_PORTS_PROPERTY, NULL);
			QueryDeviceStatus(device);
			if (deviceFeatures == NULL)
			{
				deviceFeatures = QueryDeviceDescription(device);
			}
			QueryPWMPorts(device);
				
			CreateCurrentSensorPorts(device);
			CreateStateItems(device);
			UpdatePWMModeItems(device);
			ReCreatePWMPorts(device);

			strcpy(INFO_DEVICE_MODEL_ITEM->text.value, deviceName);
			strcpy(INFO_DEVICE_FW_REVISION_ITEM->text.value, "Unknown");
			strcpy(INFO_DEVICE_HW_REVISION_ITEM->text.value,hwRevision);
			indigo_update_property(device, INFO_PROPERTY, NULL);

			indigo_set_timer(device, 0, aux_timer_callback, &PRIVATE_DATA->aux_timer);
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
		else
		{
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "Failed to connect to %s", DEVICE_PORT_ITEM->text.value);
			PRIVATE_DATA->count--;
			CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		}
	}
	else
	{
		indigo_cancel_timer_sync(device, &PRIVATE_DATA->aux_timer);

		indigo_delete_property(device, AUX_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
		indigo_delete_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);
		indigo_delete_property(device, AUX_PWM_MODES_PROPERTY, NULL);
		indigo_delete_property(device, AUX_PWM_TEMP_OFFSETS_PROPERTY, NULL);
		indigo_delete_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
		indigo_delete_property(device, AUX_CURRENT_SENSOR_PROPERTY, NULL);
		indigo_delete_property(device, AUX_WEATHER_PROPERTY, NULL);
		indigo_delete_property(device, AUX_INFO_PROPERTY, NULL);
		indigo_delete_property(device, AUX_STATE_PROPERTY, NULL);
		indigo_delete_property(device, AUX_ALWAYS_ON_PORTS_PROPERTY, NULL);

		strcpy(INFO_DEVICE_MODEL_ITEM->text.value, "Unknown");
		strcpy(INFO_DEVICE_FW_REVISION_ITEM->text.value, "Unknown");
		strcpy(INFO_DEVICE_HW_REVISION_ITEM->text.value,"Unknown");
		indigo_update_property(device, INFO_PROPERTY, NULL);

		if(deviceFeatures){
			free(deviceFeatures);
			deviceFeatures = NULL;
		}
		if (--PRIVATE_DATA->count == 0)
		{
			if (PRIVATE_DATA->handle > 0)
			{
				INDIGO_DRIVER_LOG(DRIVER_NAME, "Disconnected");
				close(PRIVATE_DATA->handle);
				PRIVATE_DATA->handle = 0;
			}
		}
		CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
	}
	indigo_aux_change_property(device, NULL, CONNECTION_PROPERTY);
	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
	indigo_unlock_master_device(device);
}

static void aux_power_outlet_handler(indigo_device *device)
{
	char response[128];
	pthread_mutex_lock(&PRIVATE_DATA->mutex);

	if (deviceFeatures)
	{
		int iNPort = 0;
		int iNSensor = 0;
		for (size_t i = 0; i < portNum; i++)
		{

			if (deviceFeatures[i].type == MPX)
			{
				if ((bool)deviceFeatures[i].value !=
					(AUX_SWITCH_POWER_OUTLETS_PROPERTY->items + iNPort)->sw.value)
				{
					SetSwitchValue(device, i, (AUX_SWITCH_POWER_OUTLETS_PROPERTY->items + iNPort)->sw.value);
				}
				iNPort++;
			}
		}

		AUX_SWITCH_POWER_OUTLETS_PROPERTY->state = INDIGO_OK_STATE;
		indigo_update_property(device, AUX_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
	}

	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
}

static void aux_pwm_configuration_handler(indigo_device *device)
{
	char response[128];
	pthread_mutex_lock(&PRIVATE_DATA->mutex);
	AUX_PWM_MODES_PROPERTY->state = INDIGO_OK_STATE;
	// get the index of PWM
	//
	int index = -1;
	for (int i = 0; (i < nTotalFeatures) && (index < 0); i++)
	{
		if(deviceFeatures[i].type == MODE){ index = i; }
	}
	for(int i = 0; i < AUX_PWM_MODES_PROPERTY->count; i++)
	{
		if(deviceFeatures[index + i].value != (AUX_PWM_MODES_PROPERTY->items + i)->number.value){
			SetSwitchValue(device,index + i,
			(AUX_PWM_MODES_PROPERTY->items + i)->number.value);
		}
		// skip temp offset
		//
		index++;
	}
	ReCreatePWMPorts(device);
	indigo_update_property(device, AUX_PWM_MODES_PROPERTY, NULL);
	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
}

static void aux_temp_offset_handler(indigo_device *device)
{
	pthread_mutex_lock(&PRIVATE_DATA->mutex);
	
	
	if (deviceFeatures)
	{
		int iNPort = 0;
		for (size_t i = 0; i < nTotalFeatures; i++)
		{
			if (deviceFeatures[i].type == SETTEMP)
			{
				if(deviceFeatures[i].value != (AUX_PWM_TEMP_OFFSETS_PROPERTY->items + iNPort)->number.value){
					SetSwitchValue(device, i, (AUX_PWM_TEMP_OFFSETS_PROPERTY->items + iNPort)->number.value);
				}
				iNPort++;
			}
		}
	}
	AUX_PWM_TEMP_OFFSETS_PROPERTY->state = INDIGO_OK_STATE;
	indigo_update_property(device, AUX_PWM_TEMP_OFFSETS_PROPERTY, NULL);
	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
}

static void aux_pwm_switch_power_outlet_handler(indigo_device *device)
{
	pthread_mutex_lock(&PRIVATE_DATA->mutex);
	if (deviceFeatures)
	{
		int iNPort = 0;
		for (size_t i = 0; i < portNum; i++)
		{
			if (deviceFeatures[i].type == SWH)
			{
				if((bool)deviceFeatures[i].value != (AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY->items + iNPort)->sw.value){
					SetSwitchValue(device, i, (AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY->items + iNPort)->sw.value);
				}
				iNPort++;
			}
		}
	}
	AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY->state = INDIGO_OK_STATE;
	indigo_update_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
}
static void aux_pwm_power_outlet_handler(indigo_device *device)
{
	pthread_mutex_lock(&PRIVATE_DATA->mutex);
	if (deviceFeatures)
	{
		int iNPort = 0;
		for (size_t i = 0; i < portNum; i++)
		{
			if (deviceFeatures[i].type == PWM)
			{
				if(deviceFeatures[i].value != (AUX_PWM_POWER_OUTLETS_PROPERTY->items + iNPort)->number.value){
					SetSwitchValue(device, i, (AUX_PWM_POWER_OUTLETS_PROPERTY->items + iNPort)->number.value);
				}
				iNPort++;
			}
		}
	}
	AUX_PWM_POWER_OUTLETS_PROPERTY->state = INDIGO_OK_STATE;
	indigo_update_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);
	pthread_mutex_unlock(&PRIVATE_DATA->mutex);
}

static indigo_result aux_change_property(indigo_device *device, indigo_client *client, indigo_property *property)
{
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	if (indigo_property_match_changeable(CONNECTION_PROPERTY, property))
	{
		// -------------------------------------------------------------------------------- CONNECTION
		if (indigo_ignore_connection_change(device, property))
			return INDIGO_OK;
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		CONNECTION_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, CONNECTION_PROPERTY, NULL);
		indigo_set_timer(device, 0, aux_connection_handler, NULL);
		return INDIGO_OK;
	} 
	else if (indigo_property_match_changeable(AUX_SWITCH_POWER_OUTLETS_PROPERTY, property))
	{
		indigo_property_copy_values(AUX_SWITCH_POWER_OUTLETS_PROPERTY, property, false);
		AUX_SWITCH_POWER_OUTLETS_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AUX_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
		indigo_set_timer(device, 0, aux_power_outlet_handler, NULL);
		return INDIGO_OK;
	}
	else if (indigo_property_match_changeable(AUX_PWM_POWER_OUTLETS_PROPERTY, property))
	{
		indigo_property_copy_values(AUX_PWM_POWER_OUTLETS_PROPERTY, property, false);
		AUX_PWM_POWER_OUTLETS_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);
		indigo_set_timer(device, 0, aux_pwm_power_outlet_handler, NULL);
		return INDIGO_OK;
	}else if (indigo_property_match_changeable(AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, property)){
		indigo_property_copy_values(AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, property, false);
		AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
		indigo_set_timer(device, 0, aux_pwm_switch_power_outlet_handler, NULL);
		return INDIGO_OK;
	}else if (indigo_property_match_changeable(AUX_PWM_MODES_PROPERTY, property)) {
		indigo_property_copy_values(AUX_PWM_MODES_PROPERTY, property, false);
		AUX_PWM_MODES_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AUX_PWM_MODES_PROPERTY, NULL);
		indigo_set_timer(device, 0, aux_pwm_configuration_handler, NULL);
		return INDIGO_OK;
	} 
	else if (indigo_property_match_changeable(AUX_PWM_TEMP_OFFSETS_PROPERTY, property)) {
		indigo_property_copy_values(AUX_PWM_TEMP_OFFSETS_PROPERTY, property, false);
		AUX_PWM_TEMP_OFFSETS_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, AUX_PWM_TEMP_OFFSETS_PROPERTY, NULL);
		indigo_set_timer(device, 0, aux_temp_offset_handler, NULL);
		return INDIGO_OK;
	}else if (indigo_property_match_changeable(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY, property)) {
		indigo_property_copy_values(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY, property, false);

		bool bIsPWMDefined = false;
		bool bIsPWMSwitchDefined = false;

		int nSW = 0;
		int nPWM = 0;
		int nAON = 0;
		for(int i = 0; i < portNum; i++ ){

			if(deviceFeatures[i].type == MPX){
				snprintf((AUX_SWITCH_POWER_OUTLETS_PROPERTY->items + i)->label, 
				INDIGO_NAME_SIZE, "%s", (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value);
			}

			if(deviceFeatures[i].type == PWM){

				bIsPWMDefined = true;
				snprintf((AUX_PWM_POWER_OUTLETS_PROPERTY->items + nPWM)->label, 
				INDIGO_NAME_SIZE, "%s", (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value);
				nPWM++;
			}

			if(deviceFeatures[i].type == SWH){
				bIsPWMSwitchDefined = true;
				snprintf((AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY->items + nSW)->label, INDIGO_NAME_SIZE, 
				"%s", (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value);
				nSW++;
			}

			snprintf((AUX_CURRENT_SENSOR_PROPERTY->items + i)->label, 
				INDIGO_NAME_SIZE, "%s", (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value);
			
			for(int i = 0; i < portNum; i++){
				sprintf((AUX_STATE_PROPERTY->items + i)->label,"%s",
				(AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value);
			}
			if(deviceFeatures[i].type == AON){
				snprintf((AUX_ALWAYS_ON_PORTS_PROPERTY->items + nAON)->label, 
				INDIGO_NAME_SIZE, "%s", (AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->items + i)->text.value);
				nAON++;
			}
		}
		AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY->state = INDIGO_OK_STATE;
		if (IS_CONNECTED) {

			indigo_delete_property(device, AUX_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
			indigo_define_property(device, AUX_SWITCH_POWER_OUTLETS_PROPERTY, NULL);

			if(bIsPWMDefined){
				indigo_delete_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);
				indigo_define_property(device, AUX_PWM_POWER_OUTLETS_PROPERTY, NULL);
			}

			if(bIsPWMSwitchDefined){
				indigo_delete_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
				indigo_define_property(device, AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY, NULL);
			}
			indigo_delete_property(device, AUX_CURRENT_SENSOR_PROPERTY, NULL);
			indigo_define_property(device, AUX_CURRENT_SENSOR_PROPERTY, NULL);
			
			indigo_delete_property(device, AUX_STATE_PROPERTY, NULL);
			indigo_define_property(device, AUX_STATE_PROPERTY, NULL);

			indigo_delete_property(device, AUX_ALWAYS_ON_PORTS_PROPERTY, NULL);
			indigo_define_property(device, AUX_ALWAYS_ON_PORTS_PROPERTY, NULL);

			indigo_update_property(device, AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY, NULL);
		}

		return INDIGO_OK;
	}else if (indigo_property_match_changeable(CONFIG_PROPERTY, property)) {
		if (indigo_switch_match(CONFIG_SAVE_ITEM, property)) {
			indigo_save_property(device, NULL, AUX_SWITCH_POWER_OUTLET_NAMES_PROPERTY);
		}

	}
	return indigo_aux_change_property(device, client, property);
}

static indigo_result aux_detach(indigo_device *device)
{
	assert(device != NULL);
	if (IS_CONNECTED)
	{
		indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		aux_connection_handler(device);
	}

	indigo_release_property( AUX_SWITCH_POWER_OUTLETS_PROPERTY );
	indigo_release_property( AUX_PWM_POWER_OUTLETS_PROPERTY );
	indigo_release_property( AUX_PWM_MODES_PROPERTY );
	indigo_release_property( AUX_PWM_TEMP_OFFSETS_PROPERTY );
	indigo_release_property( AUX_PWM_SWITCH_POWER_OUTLETS_PROPERTY );
	indigo_release_property( AUX_CURRENT_SENSOR_PROPERTY );
	indigo_release_property( AUX_WEATHER_PROPERTY );
	indigo_release_property( AUX_INFO_PROPERTY );
	indigo_release_property( AUX_STATE_PROPERTY );
	indigo_release_property( AUX_ALWAYS_ON_PORTS_PROPERTY );		
	pthread_mutex_destroy(&PRIVATE_DATA->mutex);
	INDIGO_DEVICE_DETACH_LOG(DRIVER_NAME, device->name);
	return indigo_aux_detach(device);
}

indigo_result indigo_aux_pbex(indigo_driver_action action, indigo_driver_info *info)
{
	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;
	static pbex_private_data *private_data = NULL;
	static indigo_device *aux = NULL;

	static indigo_device aux_template = INDIGO_DEVICE_INITIALIZER(
		"Big Power Box Exxxtreme",
		aux_attach,
		aux_enumerate_properties,
		aux_change_property,
		NULL,
		aux_detach);

	SET_DRIVER_INFO(info, "Big Power Box Exxxtreme", __FUNCTION__, DRIVER_VERSION, false, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch (action)
	{
	case INDIGO_DRIVER_INIT:
		last_action = action;
		private_data = indigo_safe_malloc(sizeof(pbex_private_data));
		aux = indigo_safe_malloc_copy(sizeof(indigo_device), &aux_template);
		aux->private_data = private_data;

		indigo_attach_device(aux);
		break;

	case INDIGO_DRIVER_SHUTDOWN:
		VERIFY_NOT_CONNECTED(aux);
		last_action = action;

		if (aux != NULL)
		{
			indigo_detach_device(aux);
			free(aux);
			aux = NULL;
		}
		if (private_data != NULL)
		{
			free(private_data);
			private_data = NULL;
		}
		break;

	case INDIGO_DRIVER_INFO:
		break;
	}

	return INDIGO_OK;
}
