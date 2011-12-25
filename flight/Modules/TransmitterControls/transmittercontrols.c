/* -*- Mode: c; c-basic-offset: 2; tab-width: 2; indent-tabs-mode: t -*- */
/**
******************************************************************************
* @addtogroup OpenPilotModules OpenPilot Modules
* @{
* @addtogroup TransmitterControls Copter Control TransmitterControls Estimation
* @brief Acquires sensor data and computes attitude estimate
* Specifically updates the the @ref TransmitterControlsActual "TransmitterControlsActual" and @ref TransmitterControlsRaw "TransmitterControlsRaw" settings objects
* @{
*
* @file				radio.c
* @author			The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
* @brief			Module to handle all comms to the AHRS on a periodic basis.
*
* @see				The GNU Public License (GPL) Version 3
*
******************************************************************************/
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
 * Input objects: None, takes sensor data via pios
 * Output objects: @ref TransmitterControlsRaw @ref TransmitterControlsActual
 *
 * This module computes an attitude estimate from the sensor data
 *
 * The module executes in its own thread.
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

#include "pios.h"
#include "manualcontrolcommand.h"
#include "transmittercontrols.h"
#include "manualcontrolsettings.h"
#include "gcsreceiver.h"

#define RECEIVER_INPUT
//#define ANALOG_INPUT

// Private constants
#define STACK_SIZE_BYTES 540
#define TASK_PRIORITY (tskIDLE_PRIORITY+3)

#define UPDATE_RATE	 2.0f

#define REQ_TIMEOUT_MS 250
#define MAX_RETRIES 2

#define RECEIVER_READ_PERIOD_MS 50

// Global variables
extern uint32_t rssi_pwm_id;
extern struct pios_rcvr_driver pios_pwm_rcvr_driver;

// Private types
struct RouterCommsStruct {
	uint8_t num;
	uint32_t port;
	xQueueHandle txqueue;
	xSemaphoreHandle sem;
	UAVTalkConnection com;
	xTaskHandle txTaskHandle;
	xTaskHandle rxTaskHandle;
	struct RouterCommsStruct *relay_comm;
};
typedef struct RouterCommsStruct RouterComms;
typedef RouterComms *RouterCommsHandle;

// Private variables
static uint32_t txErrors;
static uint32_t txRetries;
static RouterComms comms[2];

// Private functions
static void transmitterTxTask(void *parameters);
static void transmitterRxTask(void *parameters);
static int32_t transmitData1(uint8_t * data, int32_t length);
static int32_t transmitData2(uint8_t * data, int32_t length);


/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t TransmitterControlsStart(void)
{

	PIOS_WDG_RegisterFlag(PIOS_WDG_ATTITUDE);

	// Start the Tx and Rx tasks
	xTaskCreate(transmitterTxTask, (signed char *)"TransmitterTx1", STACK_SIZE_BYTES/4, (void*)comms, TASK_PRIORITY, &(comms[0].txTaskHandle));
	//TaskMonitorAdd(TASKINFO_RUNNING_TRANSMITTERTX, txTaskHandle);
	xTaskCreate(transmitterRxTask, (signed char *)"TransmitterRx1", STACK_SIZE_BYTES/4, (void*)comms, TASK_PRIORITY, &(comms[0].rxTaskHandle));
	//TaskMonitorAdd(TASKINFO_RUNNING_TRANSMITTERRX, rxTaskHandle);
	xTaskCreate(transmitterTxTask, (signed char *)"TransmitterTx2", STACK_SIZE_BYTES/4, (void*)(comms + 1), TASK_PRIORITY, &(comms[1].txTaskHandle));
	//TaskMonitorAdd(TASKINFO_RUNNING_TRANSMITTERTX, txTaskHandle);
	xTaskCreate(transmitterRxTask, (signed char *)"TransmitterRx2", STACK_SIZE_BYTES/4, (void*)(comms + 1), TASK_PRIORITY, &(comms[1].rxTaskHandle));
	//TaskMonitorAdd(TASKINFO_RUNNING_TRANSMITTERRX, rxTaskHandle);

	return 0;
}

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t TransmitterControlsInitialize(void) {

	// Initialize the GCSReceiver object.
	GCSReceiverInitialize();

	// Set the comm number
	comms[0].num = 0;
	comms[1].num = 1;

	// Initialize the telemetry ports
	comms[0].port = PIOS_COM_TELEM_GCS;
	comms[1].port = PIOS_COM_TELEM_OUT;

	// Create the semaphores
	comms[0].sem = xSemaphoreCreateRecursiveMutex();
	comms[1].sem = xSemaphoreCreateRecursiveMutex();

	// Point each comm to it's relay comm
	comms[0].relay_comm = comms + 1;
	comms[1].relay_comm = comms;

	// Create object queues
	comms[0].txqueue = xQueueCreate(TELEM_QUEUE_SIZE, sizeof(UAVObjEvent));
	comms[1].txqueue = xQueueCreate(TELEM_QUEUE_SIZE, sizeof(UAVObjEvent));

	// Initialise UAVTalk
	comms[0].com = UAVTalkInitializeMultiBuffer(&transmitData1, 256, 3);
	comms[1].com = UAVTalkInitializeMultiBuffer(&transmitData2, 256, 3);

	//TransmitterControlsSettingsConnectCallback(&settingsUpdatedCb);

	// Create periodic event that will be used to send transmitter state.
	UAVObjEvent ev;
	memset(&ev, 0, sizeof(UAVObjEvent));
	EventPeriodicQueueCreate(&ev, comms[1].txqueue, RECEIVER_READ_PERIOD_MS);

	PIOS_ADC_Config((PIOS_ADC_RATE / 1000.0f) * UPDATE_RATE);

	return 0;
}

MODULE_INITCALL(TransmitterControlsInitialize, TransmitterControlsStart)

#ifdef ANALOG_INPUT
/**
 * Read the primary and trim ADC channels for a control stick and scale it appropriately
 */
static uint16_t read_stick(uint8_t primary_pin, uint8_t trim_pin) {
	return PIOS_ADC_PinGet(primary_pin) + PIOS_ADC_PinGet(trim_pin) / 10;
}

/**
 * Read a switch and scale the value appropriately.
 */
static uint16_t read_switch(uint8_t val) {
	return val ? 1900 : 1000;
}

/**
 * Read a potentiometer and scale the value appropriately.
 */
static uint16_t read_potentiometer(uint8_t poten_pin) {
	return PIOS_ADC_PinGet(poten_pin);
}
#endif

/**
 * Processes queue events
 */
static void processObjEvent(UAVObjEvent * ev, RouterComms *comm)
{
	static uint32_t cntr = 0;
	UAVObjMetadata metadata;
	int32_t retries;
	int32_t success;

	PIOS_WDG_UpdateFlag(PIOS_WDG_ATTITUDE);

	if (ev->obj == 0) {
		GCSReceiverData rcvr;
		char buf[50];
		int i;
		bool debug = false;

		if(cntr == 20) {
			debug = true;
			cntr = 0;
		}
		++cntr;

#ifdef RECEIVER_INPUT
		if(debug)
			PIOS_COM_SendString(PIOS_COM_DEBUG, "Rcvr: ");

		// Read the receiver channels.
		for (i = 0; i < GCSRECEIVER_CHANNEL_NUMELEM; ++i) {
			extern uint32_t pios_rcvr_group_map[];
			uint32_t val = PIOS_RCVR_Read(pios_rcvr_group_map[MANUALCONTROLSETTINGS_CHANNELGROUPS_PPM],	i+1);
			if(debug) {
				sprintf(buf, "%x %x  ", (unsigned int)rcvr.Channel[i], (unsigned int)val);
				PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
				xPortGetFreeHeapSize();
			}
			rcvr.Channel[i] = val;
		}
		if(debug)
			PIOS_COM_SendString(PIOS_COM_DEBUG, "\n\r");

#else

		if(debug) {
			static int32_t prev_adc[PIOS_ADC_NUM_CHANNELS];
			PIOS_COM_SendString(PIOS_COM_DEBUG, "ADC: ");
			for(i = 0; i < PIOS_ADC_NUM_CHANNELS; ++i) {
				int32_t cur_adc = PIOS_ADC_PinGet(i);
				int32_t diff = (int32_t)abs(prev_adc[i] - cur_adc);
				//sprintf(buf, "%x ", (unsigned int)PIOS_ADC_PinGet(i));
				if(diff > 20) {
					sprintf(buf, "%x ", (unsigned int)PIOS_ADC_PinGet(i));
					PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
				} else
					PIOS_COM_SendString(PIOS_COM_DEBUG, "--- ");
				prev_adc[i] = cur_adc;
			}
			PIOS_COM_SendString(PIOS_COM_DEBUG, "  Switches: ");
			sprintf(buf, "%x ", (unsigned int)GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8));
			PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
			sprintf(buf, "%x ", (unsigned int)GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7));
			PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
			sprintf(buf, "%x ", (unsigned int)GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14));
			PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
			sprintf(buf, "%x ", (unsigned int)GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13));
			PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
			sprintf(buf, "%x ", (unsigned int)GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15));
			PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
			PIOS_COM_SendString(PIOS_COM_DEBUG, "  RSSI: ");
			sprintf(buf, "%x ", (unsigned int)pios_pwm_rcvr_driver.read(rssi_pwm_id, 0));
			PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
		}

		// Calculate Roll
		rcvr.Channel[0] = read_stick(1, 2);
		// Calculate Pitch
		rcvr.Channel[1] = read_stick(3, 4);
		// Calculate Throttle
		rcvr.Channel[2] = read_stick(7, 8);
		// Calculate Yaw
		rcvr.Channel[3] = read_stick(6, 5);
		// Read switch 1
		rcvr.Channel[4] = read_switch(GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_8));
		// Read switch 2
		rcvr.Channel[5] = read_switch(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_7));
		// Read switch 3
		rcvr.Channel[6] = read_switch(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_14));
		// Read switch 4
		rcvr.Channel[7] = read_switch(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13));
		// Read switch 5
		rcvr.Channel[8] = read_switch(GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_15));
		// Read the potentiometer.
		rcvr.Channel[9] = read_potentiometer(9);

		if(debug) {
			PIOS_COM_SendString(PIOS_COM_DEBUG, "  Rcvr: ");
			for(i = 0; i < 10; ++i) {
				sprintf(buf, "%d ", (unsigned int)rcvr.Channel[i]);
				PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
			}
			PIOS_COM_SendString(PIOS_COM_DEBUG, "\n\r");
		}

#endif

		// Set the GCSReceiverData object.
		{
			UAVObjMetadata metadata;
			UAVObjGetMetadata(GCSReceiverHandle(), &metadata);
			metadata.access = ACCESS_READWRITE;
			UAVObjSetMetadata(GCSReceiverHandle(), &metadata);
		}
		GCSReceiverSet(&rcvr);

		// Send update (with retries)
		retries = 0;
		success = -1;
		while (retries < MAX_RETRIES && success == -1) {
			success = UAVTalkSendObject(comm->com, GCSReceiverHandle(), 0, 0, REQ_TIMEOUT_MS);
			++retries;
		}
		// Update stats
		txRetries += (retries - 1);

	} else {

		// Get object metadata
		//UAVObjGetMetadata(ev->obj, &metadata);
		// Act on event
		retries = 0;
		success = -1;
		switch (ev->event)
		{
		case EV_UPDATED:
		case EV_UPDATED_MANUAL:
			// Send update (with retries)
			while (retries < MAX_RETRIES && success == -1) {
				success = UAVTalkSendObject(comm->com, ev->obj, ev->instId, metadata.telemetryAcked, REQ_TIMEOUT_MS);	// call blocks until ack is received or timeout
				++retries;
			}
			// Update stats
			txRetries += (retries - 1);
			if (success == -1) {
				++txErrors;
			}
			break;
		case EV_UPDATE_REQ:
			// Request object update from GCS (with retries)
			while (retries < MAX_RETRIES && success == -1) {
				success = UAVTalkSendObjectRequest(comm->com, ev->obj, ev->instId, REQ_TIMEOUT_MS);	// call blocks until update is received or timeout
				++retries;
			}
			// Update stats
			txRetries += (retries - 1);
			if (success == -1) {
				++txErrors;
			}
			break;
		case EV_TRANSMIT_REQ:
			// Send packet (with retries)
			while (retries < MAX_RETRIES && success == -1) {
				success = UAVTalkSendPacket(comm->com, ev->obj);
				*((char*)ev->obj) = 0;
				++retries;
			}
			// Update stats
			txRetries += (retries - 1);
			if (success == -1) {
				++txErrors;
			}
			break;
		default:
			break;
		}
	}
}

/**
 * Telemetry transmit task, regular priority
 */
static void transmitterTxTask(void *parameters)
{
	RouterComms *comm = (RouterComms*)parameters;
	UAVObjEvent ev;

	// Loop forever
	while (1) {
		// Wait for queue message
		if (xQueueReceive(comm->txqueue, &ev, portMAX_DELAY) == pdTRUE) {
			// Process event
			processObjEvent(&ev, comm);
		}
	}
}

/**
 * Transmitter receive task. Processes queue events and periodic updates.
 */
static void transmitterRxTask(void *parameters)
{
	RouterComms *comm = (RouterComms*)parameters;
	uint32_t inputPort = comm->port;

	// Task loop
	while (1) {
		if (inputPort) {
			uint8_t serial_data[1];
			uint16_t bytes_to_process;

			// Block until data are available
			bytes_to_process = PIOS_COM_ReceiveBuffer(inputPort, serial_data, sizeof(serial_data), 500);
			if (bytes_to_process > 0) {
				for (uint8_t i = 0; i < bytes_to_process; i++) {
					UAVTalkProcessInputStream(&(comm->com), serial_data[i]);
					UAVTalkRxState state = UAVTalkProcessInputStream(comm->com, serial_data[i]);
					if(state == UAVTALK_STATE_COMPLETE) {
						//char buf[16];
						//sprintf(buf, "Rcvd from %d\n\r", (unsigned int)comm->num);
						//PIOS_COM_SendString(PIOS_COM_DEBUG, buf);
						// Send an event to the other connection to transmit the packet.
						UAVObjEvent ev;
						ev.obj = UAVTalkGetPacket(comm->com);
						ev.instId = 0;
						ev.event = EV_TRANSMIT_REQ;
						// will not block if queue is full
						if(xQueueSend(comm->relay_comm->txqueue, &ev, 0) != pdTRUE)
							*((char*)ev.obj) = 0;
					}
				}
			}
		} else {
			vTaskDelay(5);
		}
	}
}

/**
 * Transmit data buffer to the modem or USB port.
 * \param[in] data Data buffer to send
 * \param[in] length Length of buffer
 * \return 0 Success
 */
static int32_t transmitData(RouterComms *comm, uint8_t * data, int32_t length)
{
	uint32_t outputPort = comm->port;

	if (outputPort) {
		return PIOS_COM_SendBufferNonBlocking(outputPort, data, length);
	} else {
		return -1;
	}
	return 0;
}
static int32_t transmitData1(uint8_t * data, int32_t length)
{
	return transmitData(comms, data, length);
}
static int32_t transmitData2(uint8_t * data, int32_t length)
{
	return transmitData(comms + 1, data, length);
}

/**
 * @}
 * @}
 */
