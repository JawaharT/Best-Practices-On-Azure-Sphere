#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h> 
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <getopt.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include "epoll_timerfd_utilities.h"
#include "i2c.h"
#include "hw/avnet_mt3620_sk.h"
#include "exit_codes.h"

// DevX Libraries
#include "dx_avnet_iot_connect.h"
#include "dx_azure_iot.h"
#include "dx_config.h"
#include "dx_device_twins.h"
#include "dx_utilities.h"
#include "dx_json_serializer.h"

#include <applibs/log.h>
#include <applibs/i2c.h>
#include <applibs/gpio.h>
#include <applibs/wificonfig.h>


// File descriptors - initialized to invalid value
int epollFd = -1;
static int buttonPollTimerFd = -1;
static int buttonAGpioFd = -1;
static int buttonBGpioFd = -1;

// Button state variables, initilize them to button not-pressed (High)
static GPIO_Value_Type buttonAState = GPIO_Value_High;
static GPIO_Value_Type buttonBState = GPIO_Value_High;

// Struct contains sensor info from i2c
sensor_var sensor_info;

// Support functions.
static int InitPeripheralsAndHandlers(void);
static void ClosePeripheralsAndHandlers(void);


// Azure IoT Central Template and Network Credentials
#define IOT_PLUG_AND_PLAY_MODEL_ID "dtmi:customEnvironmentMonitor:BestPracticesMonitorqq;1"
#define NETWORK_INTERFACE "wlan0"

DX_USER_CONFIG dx_config;

// Number of bytes to allocate for the JSON telemetry message for IoT Central
#define JSON_MESSAGE_BYTES 256
static char msgBuffer[JSON_MESSAGE_BYTES] = { 0 };

static DX_MESSAGE_PROPERTY* messageProperties[] = { &(DX_MESSAGE_PROPERTY) { .key = "appid", .value = "env" },
												   &(DX_MESSAGE_PROPERTY){.key = "type", .value = "telemetry"},
												   &(DX_MESSAGE_PROPERTY){.key = "schema", .value = "1"} };

static DX_MESSAGE_CONTENT_PROPERTIES contentProperties = { .contentEncoding = "utf-8", .contentType = "application/json" };


/// <summary>
///     Send Message to IoT Central with sensor outputs
/// </summary>
static void publish_message_handler(void) {

	float temperature = sensor_info.lps22hhTemperature_degC;
	float pressure = sensor_info.lps22hhpressure_hPa;
	float accel_1 = sensor_info.acceleration_mg[0];
	float accel_2 = sensor_info.acceleration_mg[1];
	float accel_3 = sensor_info.acceleration_mg[2];
	float angu_rate_1 = sensor_info.angular_rate_dps[0];
	float angu_rate_2 = sensor_info.angular_rate_dps[1];
	float angu_rate_3 = sensor_info.angular_rate_dps[2];

	static int msgId = 0;

	if (dx_isAzureConnected()) {
		// Serialize telemetry as JSON
		if (!dx_jsonSerialize(msgBuffer, sizeof(msgBuffer), 9,
			DX_JSON_INT, "MsgId", msgId++,
			DX_JSON_FLOAT, "Temperature", temperature,
			DX_JSON_FLOAT, "Pressure", pressure,
			DX_JSON_FLOAT, "AccelerationInX", accel_1,
			DX_JSON_FLOAT, "AccelerationInY", accel_2,
			DX_JSON_FLOAT, "AccelerationInZ", accel_3,
			DX_JSON_FLOAT, "AngularRateInX", angu_rate_1,
			DX_JSON_FLOAT, "AngularRateInY", angu_rate_2,
			DX_JSON_FLOAT, "AngularRateInZ", angu_rate_3)) {
			Log_Debug("JSON Serialization failed: Buffer too small\n");
			dx_terminate(ExitCode_Telemetry_Buffer_Too_Small);
		}

		// Validate sensor data to check within an expected range
		if (!IN_RANGE(temperature, -20, 50) || !IN_RANGE(pressure, 800, 1200)) {
			Log_Debug("ERROR: Invalid sensor data: %s\n", msgBuffer);
		}
		else {
			// Publish telemetry message to IoT Central
			dx_azurePublish(msgBuffer, strlen(msgBuffer), messageProperties, NELEMS(messageProperties), &contentProperties);
		}
	}
	else {
		Log_Debug("\nFailed To Find Azure IoT Central Connection.\n");
		dx_terminate(ExitCode_Azure_IoT_Central_Connection);
	}
}

/// <summary>
///     Handle button timer event: if the button is pressed
/// </summary>
static void ButtonTimerEventHandler(EventData* eventData) {

	bool buttonAPressed = false;
	bool buttonBPressed = false;

	if (ConsumeTimerFdEvent(buttonPollTimerFd) != 0) {
		dx_terminate(ExitCode_Button_Timer_Poll);
		return;
	}

	// Check for button A press
	GPIO_Value_Type newButtonAState;
	int result = GPIO_GetValue(buttonAGpioFd, &newButtonAState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read button A GPIO.");
		dx_terminate(ExitCode_Not_Read_Button_A_GPIO);
		return;
	}

	// If A button has just been pressed
	// The button has GPIO_Value_Low when pressed and GPIO_Value_High when released
	if (newButtonAState != buttonAState) {
		if (newButtonAState == GPIO_Value_Low) {
			Log_Debug("\nButton A pressed!\n");
			buttonAPressed = true;
		}
		else {
			Log_Debug("Button A released!\n\n");
		}

		// Update the static variable to use next time we enter this routine
		buttonAState = newButtonAState;
	}

	// Check for button B press
	GPIO_Value_Type newButtonBState;
	result = GPIO_GetValue(buttonBGpioFd, &newButtonBState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read button GPIO\n");
		dx_terminate(ExitCode_Not_Read_Button_B_GPIO);
		return;
	}

	// If B button has just been pressed/released
	// The button has GPIO_Value_Low when pressed and GPIO_Value_High when released
	if (newButtonBState != buttonBState) {
		if (newButtonBState == GPIO_Value_Low) {
			Log_Debug("Button B pressed!\n");
			buttonBPressed = true;
		}
		else {
			Log_Debug("Button B released!\n\n");
		}

		// Update the static variable to use next time we enter this routine
		buttonBState = newButtonBState;
	}

	if (buttonAPressed || buttonBPressed) {
		if (buttonAPressed) {
			sensor_info = initI2c();

			//Send raw data up to IoT Central
			publish_message_handler();
		}

		if (buttonBPressed) {
			Log_Debug("Application Exiting.\n");
			dx_terminate(DX_ExitCode_Success);
			return;
		}
		buttonAPressed = !buttonAPressed;
		buttonBPressed = !buttonBPressed;
		return;
	}
}

// event handler data structures. Only the event handler field needs to be populated.
static EventData buttonEventData = { .eventHandler = &ButtonTimerEventHandler };

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitPeripheralsAndHandlers(void) {
	dx_azureConnect(&dx_config, NETWORK_INTERFACE, IOT_PLUG_AND_PLAY_MODEL_ID);

	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		dx_terminate(ExitCode_PollFd_Problem);
	}

	// Open button A GPIO as input
	Log_Debug("\nOpening Button A as input.\n");
	buttonAGpioFd = GPIO_OpenAsInput(AVNET_MT3620_SK_USER_BUTTON_A);
	if (buttonAGpioFd < 0) {
		Log_Debug("ERROR: Could not open button A GPIO.\n");
		dx_terminate(ExitCode_Not_Read_Button_A_GPIO);
	}

	// Open button B GPIO as input
	Log_Debug("Opening Button B as input.\n");
	buttonBGpioFd = GPIO_OpenAsInput(AVNET_MT3620_SK_USER_BUTTON_B);
	if (buttonBGpioFd < 0) {
		Log_Debug("ERROR: Could not open button B GPIO.\n");
		dx_terminate(ExitCode_Not_Read_Button_B_GPIO);
	}

	// Set up a timer to poll the buttons (in seconds)
	Log_Debug("Poll opened for buttons. Press A for Sensor Readings. Press B to End Application.\n\n");

	struct timespec buttonPressCheckPeriod = { 0, 1 };
	buttonPollTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &buttonPressCheckPeriod, &buttonEventData, EPOLLIN);
	if (buttonPollTimerFd < 0) {
		dx_terminate(ExitCode_ButtonTimerFd_Problem);
	}

	return 0;
}


/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void) {
	Log_Debug("Closing file descriptors.\n");

	closeI2c();
	CloseFdAndPrintError(epollFd, "Epoll");
	CloseFdAndPrintError(buttonPollTimerFd, "buttonPoll");
	CloseFdAndPrintError(buttonAGpioFd, "buttonA");
	CloseFdAndPrintError(buttonBGpioFd, "buttonB");
}

/// <summary>
///     Main entry point for this application.
/// </summary>
int main(int argc, char* argv[]) {
	Log_Debug("Application Started.\n");

	dx_registerTerminationHandler();

	if (!dx_configParseCmdLineArguments(argc, argv, &dx_config)) {
		dx_terminate(DX_ExitCode_Success);
	}

	// Create and open peripherals for button poll timer, button GPIO and epoll
	if (InitPeripheralsAndHandlers() != 0) {
		dx_terminate(DX_ExitCode_Success);
	}

	// Use epoll to wait for events and trigger handlers, until an error
	while (!dx_isTerminationRequired()) {
		int result = EventLoop_Run(dx_timerGetEventLoop(), -1, true);
		if ((WaitForEventAndCallHandler(epollFd) != 0) || (result == -1)) {
			dx_terminate(DX_ExitCode_Main_EventLoopFail);
			ClosePeripheralsAndHandlers();
		}
	}
	return 0;
}