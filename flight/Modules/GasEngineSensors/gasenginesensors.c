/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup ET_EGT_Sensor EagleTree EGT Sensor Module
 * @brief Read ET EGT temperature sensors @ref ETEGTSensor "ETEGTSensor UAV Object"
 * @{
 *
 * @file       et_egt_sensor.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Reads dual thermocouple temperature sensors via EagleTree EGT expander
 *
 * @see        The GNU Public License (GPL) Version 3
 *
 *****************************************************************************/
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/**
 * Output object: GasEngine
 *
 * This module will periodically update the value of the GasEngine UAVobject.
 *
 */

#include "openpilot.h"
#include "gasenginesensors.h"
#include "mcp3424.h"
#include "mcp9804.h"
#include "gasenginedata.h"	// UAVobject that will be updated by the module
#include "gasenginedatasettings.h" // UAVobject used to modify module settings

// Private constants
#define STACK_SIZE_BYTES 350
#define TASK_PRIORITY (tskIDLE_PRIORITY+1)
#define UPDATE_PERIOD 500

#define MCP9804_I2C_ADDRESS 0x1F //Cold junction temperature sensor
#define GASENGINE_I2C_ADDRESS 0x68 //Four channel ADC sensor MCP3424

#define IGNITIONBATTVOLTAGE_CHANNEL 1
#define IGNITIONBATTCURRENT_CHANNEL 2
#define CYLINDERHEADTEMP_CHANNEL    3
#define EXHAUSTGASTEMP_CHANNEL      4

static double_t DegCPerVolt = 24813.8957; //volts per celcius for K-type thermocouple
// Private types

// Private variables
static xTaskHandle taskHandle;

// Private functions
static void GasEngineSensorsTask(void *parameters);

/**
* Start the module, called on startup
*/
int32_t GasEngineSensorsStart()
{
	// Start main task
	xTaskCreate(GasEngineSensorsTask, (signed char *)"GasEngineSensors", STACK_SIZE_BYTES/4, NULL, TASK_PRIORITY, &taskHandle);
	TaskMonitorAdd(TASKINFO_RUNNING_GASENGINESENSORS, taskHandle);
	return 0;
}

/**
* Initialise the module, called on startup
*/
int32_t GasEngineSensorsInitialize()
{
	GasEngineDataInitialize(); //Initialise the UAVObject used for transferring sensor readings to GCS
	GasEngineDataSettingsInitialize(); //Initialise the UAVObject used for changing sensor settings

	return 0;
}

MODULE_INITCALL(GasEngineSensorsInitialize, GasEngineSensorsStart)

/**
 * Module thread, should not return.
 * Channel1 = ignitionBatteryVoltage
 * Channel2 = ignitionBatteryAmps
 * Channel3 = cylinderHeadTemperature
 * Channel4 = exhaustGasTemperature
 */
static void GasEngineSensorsTask(void *parameters)
{
	uint8_t buf[8] = {0};
	buf[0] = 5;

	uint16_t I2CAddress = 0x00;
	uint8_t gain, resolution = 0;
	double_t analogValue = 0.0;
	float_t mAh = 0.0;

	//Assume Attopilot voltage and current sensor is being used.
	// Specifically, the full scale voltage is 51.8V = 3.3V
	// Full scale current is 90A = 3.3V
	double_t attoPilotVscale = 1 / 0.06369; //From data sheet
	double_t attoPilotIscale = 1 / 0.0366; //From data sheet

	portTickType lastSysTime;

	GasEngineDataData d1; //UAVObject data structure
	GasEngineDataSettingsData s1; //UAVObject settings data

	double_t coldTemp = 0;

	portTickType energyTimeTickCount = 0; //delta time for calculating battery energy consumption

	// Main task loop
	lastSysTime = xTaskGetTickCount();

	while(1) {

		/*****************
		 * Read cold junction temp from separate MCP9804 IC via I2C
		 ****************/

		bool b2 = MCP9804_ReadColdJunctionTemp(&coldTemp, MCP9804_I2C_ADDRESS);

		if(b2)
			d1.ColdJunction = coldTemp;
		else
			d1.ColdJunction = -99;


		//get any updated settings
		GasEngineDataSettingsGet(&s1);
		I2CAddress = GASENGINE_I2C_ADDRESS; //gasEngineSettingsData.I2CAddress;

		/******************
		 * Read channel 1
		 *******************/
		gain = MCP3424_GetGain(s1.Channel1Gain);
		resolution = MCP3424_GetResolution(s1.Channel1Resolution);

		if (MCP3424_GetAnalogValue(I2CAddress, IGNITIONBATTVOLTAGE_CHANNEL, buf, resolution, gain, &analogValue))
		{
			d1.BatteryVoltage = analogValue * attoPilotVscale;
		}
		else
			d1.BatteryVoltage = -99;

		/***************
		 * Read channel 2
		 ***************/
		gain = MCP3424_GetGain(s1.Channel2Gain);
		resolution = MCP3424_GetResolution(s1.Channel2Resolution);

		if (MCP3424_GetAnalogValue(I2CAddress, IGNITIONBATTCURRENT_CHANNEL, buf, resolution, gain, &analogValue))
		{
			d1.BatteryAmps = analogValue * attoPilotIscale;
		}
		else
			d1.BatteryAmps = -99;

		/******************
		 * Read channel 3
		 ******************/
		gain = MCP3424_GetGain(s1.Channel3Gain);
		resolution = MCP3424_GetResolution(s1.Channel3Resolution);

		if (MCP3424_GetAnalogValue(I2CAddress, CYLINDERHEADTEMP_CHANNEL, buf, resolution, gain, &analogValue))
		{
			d1.CylinderHeadTemp = (analogValue * DegCPerVolt) + coldTemp;  //Assume K-type thermocouple
		}
		else
		{
			d1.CylinderHeadTemp = -99;
		}

		/*******************
		 * Read channel 4
		 ********************/
		gain = MCP3424_GetGain(s1.Channel4Gain);
		resolution = MCP3424_GetResolution(s1.Channel4Resolution);

		//Read thermocouple connected to channel 1 of MCP3424 IC via I2C
		if (MCP3424_GetAnalogValue(I2CAddress, EXHAUSTGASTEMP_CHANNEL, buf, resolution, gain, &analogValue))
		{
			d1.ExhaustGasTemp = (analogValue * DegCPerVolt) + coldTemp;  //Assume K-type thermocouple
		}
		else
			d1.ExhaustGasTemp = -99;

		/*************************
		 * Calculate mAh consumed
		 *************************/

		portTickType x = xTaskGetTickCount();
		portTickType deltaT = (x - energyTimeTickCount) * portTICK_RATE_MS; //Conversion to milliseconds
		energyTimeTickCount = x;

		//float_t mAh = 0.0;
		//GasEngineIgnitionBattery_mAhGet(&mAh); //This allows some other module or the GCS to reset it to zero

		/*
		 * Update UAVObject data
		 */
		d1.IgnitionBattery_mAh = mAh + (deltaT * d1.BatteryAmps / 3600.0); //Conversion to mAh

		GasEngineDataSet(&d1);

		// Delay until it is time to read the next sample
		vTaskDelayUntil(&lastSysTime, UPDATE_PERIOD / portTICK_RATE_MS);
	}
}


/**
  * @}
 * @}
 */
