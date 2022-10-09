/*
Nintendo Switch Fightstick - Proof-of-Concept

Based on the LUFA library's Low-Level Joystick Demo
	(C) Dean Camera
Based on the HORI's Pokken Tournament Pro Pad design
	(C) HORI

This project implements a modified version of HORI's Pokken Tournament Pro Pad
USB descriptors to allow for the creation of custom controllers for the
Nintendo Switch. This also works to a limited degree on the PS3.

Since System Update v3.0.0, the Nintendo Switch recognizes the Pokken
Tournament Pro Pad as a Pro Controller. Physical design limitations prevent
the Pokken Controller from functioning at the same level as the Pro
Controller. However, by default most of the descriptors are there, with the
exception of Home and Capture. Descriptor modification allows us to unlock
these buttons for our use.
*/

/** \file
 *
 *  Main source file for automated tableturf battles. This file contains the main tasks of
 *  the demo and is responsible for the initial application hardware configuration.
 */

#include "Joystick.h"

typedef enum {
	UP,
	DOWN,
	LEFT,
	RIGHT,
	A,
	B,
	NOTHING
} Buttons_t;

typedef struct {
	Buttons_t button;
	uint16_t duration;
} command;

static const command step[] = {
	{ NOTHING,  1 },
	{ B,   5 },
	{ NOTHING,  1 },
	{ DOWN,   5 }, 
	{ NOTHING,  1 },
	{ DOWN,   5 },
	{ NOTHING,  1 },
	{ A,   5 },
	{ NOTHING,  1 },
	{ A,   5 }
};


// Main entry point.
int main(void)
{
	// We'll start by performing hardware and peripheral setup.
	SetupHardware();
	// We'll then enable global interrupts for our use.
	GlobalInterruptEnable();
	// Once that's done, we'll enter an infinite loop.
	for (;;)
	{
		// We need to run our task to process and deliver data for our IN and OUT endpoints.
		HID_Task();
		// We also need to run the main USB management task.
		USB_USBTask();
	}
}

// Configures hardware and peripherals, such as the USB peripherals.
void SetupHardware(void)
{
	// We need to disable watchdog if enabled by bootloader/fuses.
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	// We need to disable clock division before initializing the USB hardware.
	clock_prescale_set(clock_div_1);

	// We can then initialize our hardware and peripherals, including the USB stack.

#ifdef ALERT_WHEN_DONE
	// Both PORTD and PORTB will be used for the optional LED flashing and buzzer.
#warning LED and Buzzer functionality enabled. All pins on both PORTB and PORTD will toggle when printing is done.
	DDRD = 0xFF; //Teensy uses PORTD
	PORTD = 0x0;
	//We'll just flash all pins on both ports since the UNO R3
	DDRB = 0xFF; //uses PORTB. Micro can use either or, but both give us 2 LEDs
	PORTB = 0x0; //The ATmega328P on the UNO will be resetting, so unplug it?
#endif

	// The USB stack should be initialized last.
	USB_Init();
}

// Fired to indicate that the device is enumerating.
void EVENT_USB_Device_Connect(void)
{
	// We can indicate that we're enumerating here (via status LEDs, sound, etc.).
}

// Fired to indicate that the device is no longer connected to a host.
void EVENT_USB_Device_Disconnect(void)
{
	// We can indicate that our device is not ready (via status LEDs, sound, etc.).
}

// Fired when the host set the current configuration of the USB device after enumeration.
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	// We setup the HID report endpoints.
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_OUT_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_IN_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);

	// We can read ConfigSuccess to indicate a success or failure at this point.
}

// Process control requests sent to the device from the USB host.
void EVENT_USB_Device_ControlRequest(void)
{
	// We can handle two control requests: a GetReport and a SetReport.

	// Not used here, it looks like we don't receive control request from the Switch.
}

// Process and deliver data from IN and OUT endpoints.
void HID_Task(void)
{
	// If the device isn't connected and properly configured, we can't do anything here.
	if (USB_DeviceState != DEVICE_STATE_Configured)
		return;

	// We'll start with the OUT endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_OUT_EPADDR);
	// We'll check to see if we received something on the OUT endpoint.
	if (Endpoint_IsOUTReceived())
	{
		// If we did, and the packet has data, we'll react to it.
		if (Endpoint_IsReadWriteAllowed())
		{
			// We'll create a place to store our data received from the host.
			USB_JoystickReport_Output_t JoystickOutputData;
			// We'll then take in that data, setting it up in our storage.
			Endpoint_Read_Stream_LE(&JoystickOutputData, sizeof(JoystickOutputData), NULL);
			// At this point, we can react to this data.

			// However, since we're not doing anything with this data, we abandon it.
		}
		// Regardless of whether we reacted to the data, we acknowledge an OUT packet on this endpoint.
		Endpoint_ClearOUT();
	}

	// We'll then move on to the IN endpoint.
	Endpoint_SelectEndpoint(JOYSTICK_IN_EPADDR);
	// We first check to see if the host is ready to accept data.
	if (Endpoint_IsINReady())
	{
		// We'll create an empty report.
		USB_JoystickReport_Input_t JoystickInputData;
		// We'll then populate this report with what we want to send to the host.
		GetNextReport(&JoystickInputData);
		// Once populated, we can output this data to the host. We do this by first writing the data to the control stream.
		Endpoint_Write_Stream_LE(&JoystickInputData, sizeof(JoystickInputData), NULL);
		// We then send an IN packet on this endpoint.
		Endpoint_ClearIN();
	}
}

typedef enum {
	SYNC_CONTROLLER,
	BREATHE,
	PROCESS
} State_t;
State_t state = SYNC_CONTROLLER;

#define ECHOES 2
#define TURNS 12
int echoes = 0;
USB_JoystickReport_Input_t last_report;

int command_count = 0;
int report_count = 0;
int xpos = 0;
int ypos = 0;
int bufindex = 0;
int duration_count = 0;
int portsval = 0;
int turn_count = 0;

#define max(a, b) (a > b ? a : b)
#define ms_2_count(ms) (ms / ECHOES / (max(POLLING_MS, 8) / 8 * 8))

void processCommand(USB_JoystickReport_Input_t *const ReportData, command cmd)
{
	switch (cmd.button)
	{
		case UP:
			ReportData->HAT = HAT_TOP;				
			break;

		case LEFT:
			ReportData->HAT = HAT_LEFT;				
			break;

		case DOWN:
			ReportData->HAT = HAT_BOTTOM;				
			break;

		case RIGHT:
			ReportData->HAT = HAT_RIGHT;				
			break;

		case A:
			ReportData->Button |= SWITCH_A;
			break;
			
		case B:
			ReportData->Button |= SWITCH_B;
			break;

		default:
			ReportData->LX = STICK_CENTER;
			ReportData->LY = STICK_CENTER;
			ReportData->RX = STICK_CENTER;
			ReportData->RY = STICK_CENTER;
			ReportData->HAT = HAT_CENTER;
			break;
	}

	duration_count++;

	if (duration_count > cmd.duration)
	{
		bufindex++;
		duration_count = 0;				
	}
}	


// Prepare the next report for the host
void GetNextReport(USB_JoystickReport_Input_t *const ReportData)
{

	// Prepare an empty report
	memset(ReportData, 0, sizeof(USB_JoystickReport_Input_t));
	ReportData->LX = STICK_CENTER;
	ReportData->LY = STICK_CENTER;
	ReportData->RX = STICK_CENTER;
	ReportData->RY = STICK_CENTER;
	ReportData->HAT = HAT_CENTER;

	// Repeat ECHOES times the last report
	if (echoes > 0)
	{
		memcpy(ReportData, &last_report, sizeof(USB_JoystickReport_Input_t));
		echoes--;
		return;
	}

	// States and moves management
	switch (state)
	{
		case SYNC_CONTROLLER:
			if (command_count > ms_2_count(2000))
			{
				command_count = 0;
				state = BREATHE;
			}
			else
			{
				if (command_count == ms_2_count(500) || command_count == ms_2_count(1000))
					ReportData->Button |= SWITCH_L;
				else if (command_count == ms_2_count(1500) || command_count == ms_2_count(2000))
					ReportData->Button |= SWITCH_A;
				command_count++;
			}
			break;
		case BREATHE:
			state = PROCESS;
			break;
		case PROCESS:
			processCommand(ReportData, step[bufindex]);
			if (bufindex > (int)( sizeof(step) / sizeof(step[0])) - 1)
			{
				bufindex = 0;
				duration_count = 0;

				state = BREATHE;

				ReportData->LX = STICK_CENTER;
				ReportData->LY = STICK_CENTER;
				ReportData->RX = STICK_CENTER;
				ReportData->RY = STICK_CENTER;
				ReportData->HAT = HAT_CENTER;
			}
			break;

	}

	// Prepare to echo this report
	memcpy(&last_report, ReportData, sizeof(USB_JoystickReport_Input_t));
	echoes = ECHOES;
}
