/**
 ******************************************************************************
 * @addtogroup OpenPilotModules OpenPilot Modules
 * @{
 * @addtogroup CameraStab Camera Stabilization Module
 * @brief Camera stabilization module
 * Updates accessory outputs with values appropriate for camera stabilization
 * @{
 *
 * @file       camerastab.c
 * @author     The OpenPilot Team, http://www.openpilot.org Copyright (C) 2010.
 * @brief      Stabilize camera against the roll pitch and yaw of aircraft
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
 * Output object: Accessory
 *
 * This module will periodically calculate the output values for stabilizing the camera
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

#include "accessorydesired.h"
#include "attitudeactual.h"
#include "camerastabsettings.h"
#include "cameradesired.h"
#include "modulesettings.h"
// New - Feed Forward
#include "mixersettings.h"

//
// Configuration
//
#define SAMPLE_PERIOD_MS		10
#define LOAD_DELAY			7000

// Private types
enum {ROLL,PITCH,YAW,MAX_AXES};

// Private variables
static struct CameraStab_data {
	portTickType lastSysTime;
	uint8_t AttitudeFilter;
	float inputs[CAMERASTABSETTINGS_INPUT_NUMELEM];
	float attitude_filtered[MAX_AXES];
	float FFlastAttitude[MAX_AXES];
	float FFlastFilteredAttitude[MAX_AXES];
	float FFfilterAccumulator[MAX_AXES];	
} *csd;

// Private functions
static void attitudeUpdated(UAVObjEvent* ev);
static float bound(float val, float limit);
static void applyFF(uint8_t index, float dT, float *attitude, CameraStabSettingsData* cameraStab);

/**
 * Initialise the module, called on startup
 * \returns 0 on success or -1 if initialisation failed
 */
int32_t CameraStabInitialize(void)
{
	bool module_enabled = false;

#ifdef MODULE_CameraStab_BUILTIN
	module_enabled = true;
#else
	uint8_t module_state[MODULESETTINGS_STATE_NUMELEM];
	ModuleSettingsStateGet(module_state);
	if (module_state[MODULESETTINGS_STATE_CAMERASTAB] == MODULESETTINGS_STATE_ENABLED) {
		module_enabled = true;
	} else {
		module_enabled = false;
	}
#endif

	if (module_enabled) {

		// allocate and initialize the static data storage only if module is enabled
		csd = (struct CameraStab_data *) pvPortMalloc(sizeof(struct CameraStab_data));
		if (!csd)
			return -1;

		// make sure that all inputs[] are zeroed
		memset(csd, 0, sizeof(struct CameraStab_data));
		csd->lastSysTime = xTaskGetTickCount();

		AttitudeActualInitialize();
		CameraStabSettingsInitialize();
		CameraDesiredInitialize();

		UAVObjEvent ev = {
			.obj = AttitudeActualHandle(),
			.instId = 0,
			.event = 0,
		};
		EventPeriodicCallbackCreate(&ev, attitudeUpdated, SAMPLE_PERIOD_MS / portTICK_RATE_MS);

		return 0;
	}

	return -1;
}

/* stub: module has no module thread */
int32_t CameraStabStart(void)
{
	return 0;
}

MODULE_INITCALL(CameraStabInitialize, CameraStabStart)

static void attitudeUpdated(UAVObjEvent* ev)
{
	if (ev->obj != AttitudeActualHandle())
		return;

	AccessoryDesiredData accessory;

	CameraStabSettingsData cameraStab;
	CameraStabSettingsGet(&cameraStab);

	// Check how long since last update, time delta between calls in ms
	portTickType thisSysTime = xTaskGetTickCount();
	float dT = (thisSysTime > csd->lastSysTime) ?
			(thisSysTime - csd->lastSysTime) / portTICK_RATE_MS :
			(float)SAMPLE_PERIOD_MS / 1000.0f;
	csd->lastSysTime = thisSysTime;


	float attitude;
	float output;

	// Read any input channels and apply LPF
	for (uint8_t i = 0; i < MAX_AXES; i++) {
		switch (i) {
		case ROLL:
			AttitudeActualRollGet(&attitude);
			break;
		case PITCH:
			AttitudeActualPitchGet(&attitude);
			break;
		case YAW:
			AttitudeActualYawGet(&attitude);
			break;
		}

		float rt = (float)cameraStab.AttitudeFilter;
		csd->attitude_filtered[i] = (rt / (rt + dT)) * csd->attitude_filtered[i] + (dT / (rt + dT)) * attitude;
		attitude = csd->attitude_filtered[i];


		if (cameraStab.Input[i] != CAMERASTABSETTINGS_INPUT_NONE) {
			if (AccessoryDesiredInstGet(cameraStab.Input[i] - CAMERASTABSETTINGS_INPUT_ACCESSORY0, &accessory) == 0) {
				float input_rate;
				switch (cameraStab.StabilizationMode[i]) {
				case CAMERASTABSETTINGS_STABILIZATIONMODE_ATTITUDE:
					csd->inputs[i] = accessory.AccessoryVal * cameraStab.InputRange[i];
					break;
				case CAMERASTABSETTINGS_STABILIZATIONMODE_AXISLOCK:
					input_rate = accessory.AccessoryVal * cameraStab.InputRate[i];
					if (fabs(input_rate) > cameraStab.MaxAxisLockRate)
						csd->inputs[i] = bound(csd->inputs[i] + input_rate * dT / 1000.0f, cameraStab.InputRange[i]);
					break;
				default:
					PIOS_Assert(0);
				}
			}
		}

		// Add Servo FeedForward
		applyFF(i, dT, &attitude, &cameraStab);

		// Set output channels
		output = bound((attitude + csd->inputs[i]) / cameraStab.OutputRange[i], 1.0f);
		if (thisSysTime / portTICK_RATE_MS > LOAD_DELAY) {
			switch (i) {
			case ROLL:
				CameraDesiredRollSet(&output);
				break;
			case PITCH:
				CameraDesiredPitchSet(&output);
				break;
			case YAW:
				CameraDesiredYawSet(&output);
				break;
			}
		}
	}
}

float bound(float val, float limit)
{
	return (val > limit) ? limit :
		(val < -limit) ? -limit :
		val;
}

static void applyFF(uint8_t index, float dT, float *attitude, CameraStabSettingsData* cameraStab)
{
	MixerSettingsData mixerSettings;
	MixerSettingsGet (&mixerSettings);
	float k = 1;
	if (index == ROLL) {
		float pattitude;
		AttitudeActualPitchGet(&pattitude);
		k = (cameraStab->OutputRange[PITCH] - fabs(pattitude)) / cameraStab->OutputRange[PITCH];
	}
	float accumulator = csd->FFfilterAccumulator[index];
	float period = dT / 1000.0f;
	accumulator += (*attitude - csd->FFlastAttitude[index]) * cameraStab->FeedForward[index] * k;
	csd->FFlastAttitude[index] = *attitude;
	*attitude += accumulator;

	if(period !=0)
	{
		if(accumulator > 0)
		{
			float filter = mixerSettings.AccelTime / period;
			if(filter <1)
			{
				filter = 1;
			}
			accumulator -= accumulator / filter;
		}else
		{
			float filter = mixerSettings.DecelTime / period;
			if(filter <1)
			{
				filter = 1;
			}
			accumulator -= accumulator / filter;
		}
	}
	csd->FFfilterAccumulator[index] = accumulator;
	*attitude += accumulator;

	//acceleration and decceleration limit
	float delta = *attitude - csd->FFlastFilteredAttitude[index];
	float maxDelta = mixerSettings.MaxAccel * period;
	if(fabs(delta) > maxDelta) //we are accelerating too hard
	{
		*attitude = csd->FFlastFilteredAttitude[index] + (delta > 0 ? maxDelta : - maxDelta);
	}
	csd->FFlastFilteredAttitude[index] = *attitude;
}
/**
  * @}
  */

/**
 * @}
 */
