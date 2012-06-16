/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup BatteryModule Battery Module
 * @brief Measures battery voltage and current
 * Updates the FlightBatteryState object
 * @{
 *
 * @file       battery.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Module to read the battery Voltage and Current periodically and set alarms appropriately.
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
 * Output object: FlightBatteryState
 *
 * This module will periodically generate information on the battery state.
 *
 * UAVObjects are automatically generated by the UAVObjectGenerator from
 * the object definition XML file.
 *
 * Modules have no API, all communication to other modules is done through UAVObjects.
 * However modules may use the API exposed by shared libraries.
 * See the OpenPilot wiki for more details.
 * http://www.openpilot.org/OpenPilot_Application_Architecture
 *
 */

#include "openpilot.h"

#include "flightbatterystate.h"
#include "flightbatterysettings.h"
#include "hwsettings.h"

//
// Configuration
//
#define SAMPLE_PERIOD_MS		500
#define BATTERY_BOARD_VOLTAGE_WARNING 4.5
#define BATTERY_BOARD_VOLTAGE_CRITICAL 3.5
#define BATTERY_BOARD_VOLTAGE_ERROR 1.0
// Private types

// Private variables
static bool batteryEnabled = false;

// Private functions
static void onTimer(UAVObjEvent* ev);

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t BatteryInitialize(void)
{

#ifdef MODULE_BATTERY_BUILTIN
	batteryEnabled = true;
#else
	HwSettingsInitialize();
	uint8_t optionalModules[HWSETTINGS_OPTIONALMODULES_NUMELEM];

	HwSettingsOptionalModulesGet(optionalModules);

	if (optionalModules[HWSETTINGS_OPTIONALMODULES_BATTERY] == HWSETTINGS_OPTIONALMODULES_ENABLED)
		batteryEnabled = true;
	else
		batteryEnabled = false;
#endif

	if (batteryEnabled) {
		FlightBatteryStateInitialize();
		FlightBatterySettingsInitialize();
	
		static UAVObjEvent ev;

		memset(&ev,0,sizeof(UAVObjEvent));
		EventPeriodicCallbackCreate(&ev, onTimer, SAMPLE_PERIOD_MS / portTICK_RATE_MS);
	}

	return 0;
}

MODULE_INITCALL(BatteryInitialize, 0)
#define HAS_SENSOR(x) batterySettings.SensorType[x]==FLIGHTBATTERYSETTINGS_SENSORTYPE_ENABLED 
static void onTimer(UAVObjEvent* ev)
{
	static FlightBatteryStateData flightBatteryData;
	static bool BoardPowerWarning= false;
	// prevent that the initial ramp up of the power supply rail is identified as a power failure.
	static bool BoardPowerOk = false;
	FlightBatterySettingsData batterySettings;

	FlightBatterySettingsGet(&batterySettings);

	static float dT = SAMPLE_PERIOD_MS / 1000.0;
	float energyRemaining;

	if(HAS_SENSOR(FLIGHTBATTERYSETTINGS_SENSORTYPE_BOARDVOLTAGE) )
		flightBatteryData.BoardSupplyVoltage=((float)PIOS_ADC_PinGet(4)) * PIOS_ADC_VOLTAGE_SCALE * 6.1;
	else
		flightBatteryData.BoardSupplyVoltage = -1;

	//calculate the battery parameters
	if(HAS_SENSOR(FLIGHTBATTERYSETTINGS_SENSORTYPE_BATTERYVOLTAGE) )
		flightBatteryData.Voltage = ((float)PIOS_ADC_PinGet(0)) * PIOS_ADC_VOLTAGE_SCALE * batterySettings.SensorCalibrations[FLIGHTBATTERYSETTINGS_SENSORCALIBRATIONS_VOLTAGEFACTOR]; //in Volts
	else 
		flightBatteryData.Voltage = -1;

	
	if(HAS_SENSOR(FLIGHTBATTERYSETTINGS_SENSORTYPE_BATTERYCURRENT))
	{
		flightBatteryData.Current = ((float)PIOS_ADC_PinGet(1)) * PIOS_ADC_VOLTAGE_SCALE * batterySettings.SensorCalibrations[FLIGHTBATTERYSETTINGS_SENSORCALIBRATIONS_CURRENTFACTOR]; //in Amps
		flightBatteryData.ConsumedEnergy += (flightBatteryData.Current * 1000.0f * dT / 3600.0f) ;//in mAh
		
		if (flightBatteryData.Current > flightBatteryData.PeakCurrent)
			flightBatteryData.PeakCurrent = flightBatteryData.Current; //in Amps
		
		flightBatteryData.AvgCurrent=(flightBatteryData.AvgCurrent*0.8)+(flightBatteryData.Current*0.2); //in Amps

		//sanity checks
		if (flightBatteryData.AvgCurrent<0)
			flightBatteryData.AvgCurrent=0.0;
		if (flightBatteryData.PeakCurrent<0)
			flightBatteryData.PeakCurrent=0.0;
		if (flightBatteryData.ConsumedEnergy<0)
			flightBatteryData.ConsumedEnergy=0.0;

		energyRemaining = batterySettings.Capacity - flightBatteryData.ConsumedEnergy; // in mAh
		flightBatteryData.EstimatedFlightTime = ((energyRemaining / (flightBatteryData.AvgCurrent*1000.0))*3600.0);//in Sec
	}
	else 
		if(flightBatteryData.Current != -1)
		{
			flightBatteryData.Current = -1;
			flightBatteryData.EstimatedFlightTime = 0;
			flightBatteryData.AvgCurrent = 0;
			flightBatteryData.ConsumedEnergy = 0;
		}

		//Check for battery inputs disconnection (don't think this really works. Do we need pull down on inputs?).
	if (flightBatteryData.Voltage == 0 || 
		 flightBatteryData.Current == 0 )
	{
		AlarmsSet(SYSTEMALARMS_ALARM_BATTERY, SYSTEMALARMS_ALARM_ERROR);
		AlarmsSet(SYSTEMALARMS_ALARM_FLIGHTTIME, SYSTEMALARMS_ALARM_ERROR);
	}
	else
	{
		if(HAS_SENSOR(FLIGHTBATTERYSETTINGS_SENSORTYPE_BATTERYCURRENT))
		{
			if (flightBatteryData.EstimatedFlightTime < 30) 
				AlarmsSet(SYSTEMALARMS_ALARM_FLIGHTTIME, SYSTEMALARMS_ALARM_CRITICAL);
			else 
				if (flightBatteryData.EstimatedFlightTime < 60) 
					AlarmsSet(SYSTEMALARMS_ALARM_FLIGHTTIME, SYSTEMALARMS_ALARM_WARNING);
				else 
					AlarmsClear(SYSTEMALARMS_ALARM_FLIGHTTIME);
		}
		
		// FIXME: should make the battery voltage detection dependent on battery type.
		if(HAS_SENSOR(FLIGHTBATTERYSETTINGS_SENSORTYPE_BATTERYVOLTAGE)){
			if (flightBatteryData.Voltage < batterySettings.VoltageThresholds[FLIGHTBATTERYSETTINGS_VOLTAGETHRESHOLDS_ALARM] )
				AlarmsSet(SYSTEMALARMS_ALARM_BATTERY, SYSTEMALARMS_ALARM_CRITICAL);
			else 
				if (flightBatteryData.Voltage < batterySettings.VoltageThresholds[FLIGHTBATTERYSETTINGS_VOLTAGETHRESHOLDS_WARNING])
					AlarmsSet(SYSTEMALARMS_ALARM_BATTERY, SYSTEMALARMS_ALARM_WARNING);
				else 
					AlarmsClear(SYSTEMALARMS_ALARM_BATTERY);
		}
	}
	
	if(HAS_SENSOR(FLIGHTBATTERYSETTINGS_SENSORTYPE_BOARDVOLTAGE) )
	{
		// power ia disconnected from the board (it is powered by usb)
		if(flightBatteryData.BoardSupplyVoltage!= -1 && flightBatteryData.BoardSupplyVoltage < BATTERY_BOARD_VOLTAGE_ERROR)
		{
			AlarmsSet(SYSTEMALARMS_ALARM_POWER, SYSTEMALARMS_ALARM_ERROR);
			BoardPowerWarning=false;
			BoardPowerOk = false;
		}
		else 
		{
			if(BoardPowerOk && flightBatteryData.BoardSupplyVoltage < BATTERY_BOARD_VOLTAGE_CRITICAL)
			{
				AlarmsSet(SYSTEMALARMS_ALARM_POWER, SYSTEMALARMS_ALARM_CRITICAL);
				BoardPowerWarning=true;
			}
			else if (BoardPowerOk && flightBatteryData.BoardSupplyVoltage < BATTERY_BOARD_VOLTAGE_WARNING)
			{
				AlarmsSet(SYSTEMALARMS_ALARM_POWER, SYSTEMALARMS_ALARM_WARNING);
				BoardPowerWarning=true;
			}
			else 
			{
				// if there was any previous warning/critical condition, notify the problem leaving the warning
				if(BoardPowerWarning)
					AlarmsSet(SYSTEMALARMS_ALARM_POWER, SYSTEMALARMS_ALARM_WARNING);
				else
					AlarmsClear(SYSTEMALARMS_ALARM_POWER);
				BoardPowerOk |= flightBatteryData.BoardSupplyVoltage > BATTERY_BOARD_VOLTAGE_WARNING;
			}
		}		
	}		
	FlightBatteryStateSet(&flightBatteryData);
}

/**
  * @}
  */

/**
 * @}
 */
