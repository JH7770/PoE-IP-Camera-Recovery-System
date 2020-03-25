#ifndef UPNP_CCTV_DEVICE_H
#define UPNP_CCTV_DEVICE_H

/**************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 **************************************************************************/

/*!
 * \addtogroup UpnpSamples
 *
 * @{
 *
 * \name Device Sample API
 *
 * @{
 *
 * \file
 */

#include <stdio.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "sample_util.h"

#include "ithread.h"
#include "upnp.h"

#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <linux/watchdog.h>

#include <pthread.h>

/*! Power constants */
#define POWER_ON 1
#define POWER_OFF 0


/*! Number of services. */
#define CCTV_SERVICE_SERVCOUNT  1

/*! Index of control service */
#define CCTV_SERVICE_CONTROL    0


/*! Number of control variables */
#define CCTV_CONTROL_VARCOUNT   2

/*! Index of power variable */
#define CCTV_CONTROL_POWER      0
#define CCTV_CONTROL_TEMP	1


/*! Temperature constants */
#define MAX_TEMP 100
#define MIN_TEMP 1
/*! Max value length */
#define CCTV_MAX_VAL_LEN 5

/*! Max actions */
#define CCTV_MAXACTIONS 12

/*! This should be the maximum VARCOUNT from above */
#define CCTV_MAXVARS 5 

/*!
 * \brief Prototype for all actions. For each action that a service 
 * implements, there is a corresponding function with this prototype.
 *
 * Pointers to these functions, along with action names, are stored
 * in the service table. When an action request comes in the action
 * name is matched, and the appropriate function is called.
 * Each function returns UPNP_E_SUCCESS, on success, and a nonzero 
 * error code on failure.
 */


int fd;


typedef int (*upnp_action)(
	/*! [in] Document of action request. */
	IXML_Document *request,
	/*! [out] Action result. */
	IXML_Document **out,
	/*! [out] Error string in case action was unsuccessful. */
	const char **errorString);

/*! Structure for storing CCTv Service identifiers and state table. */
struct CCTvService {
	/*! Universally Unique Device Name. */
	char UDN[NAME_SIZE];
	/*! . */
	char ServiceId[NAME_SIZE];
	/*! . */
	char ServiceType[NAME_SIZE];
	/*! . */
	const char *VariableName[CCTV_MAXVARS]; 
	/*! . */
	char *VariableStrVal[CCTV_MAXVARS];
	/*! . */
	const char *ActionNames[CCTV_MAXACTIONS];
	/*! . */
	upnp_action actions[CCTV_MAXACTIONS];
	/*! . */
	int VariableCount;
};

/*! Array of service structures */
extern struct CCTvService cctv_service_table[];

/*! Device handle returned from sdk */
extern UpnpDevice_Handle device_handle;

/*! Mutex for protecting the global state table data
 * in a multi-threaded, asynchronous environment.
 * All functions should lock this mutex before reading
 * or writing the state table data. */
extern ithread_mutex_t CCTVDevMutex;

/*!
 * \brief Initializes the action table for the specified service.
 *
 * Note that knowledge of the service description is assumed.
 * Action names are hardcoded.
 */
int SetActionTable(
	/*! [in] one of CCTV_SERVICE_CONTROL or, CCTV_SERVICE_PICTURE. */
	int serviceType,
	/*! [in,out] service containing action table to set. */
	struct CCTvService *out);

/*!
 * \brief Initialize the device state table for this CCTvDevice, pulling
 * identifier info from the description Document.
 *
 * Note that knowledge of the service description is assumed.
 * State table variables and default values are currently hardcoded in
 * this file rather than being read from service description documents.
 */
int CCTvDeviceStateTableInit(
	/*! [in] The description document URL. */
	char *DescDocURL);

/*!
 * \brief Called during a subscription request callback.
 *
 * If the subscription request is for this device and either its
 * control service or picture service, then accept it.
 */
int CCTvDeviceHandleSubscriptionRequest(
	/*! [in] The subscription request event structure. */
	const UpnpSubscriptionRequest *sr_event);

/*!
 * \brief Called during a get variable request callback.
 *
 * If the request is for this device and either its control service or
 * picture service, then respond with the variable value.
 */
int CCTvDeviceHandleGetVarRequest(
	/*! [in,out] The control get variable request event structure. */
	UpnpStateVarRequest *cgv_event);

/*!
 * \brief Called during an action request callback.
 *
 * If the request is for this device and either its control service
 * or picture service, then perform the action and respond.
 */
int CCTvDeviceHandleActionRequest(
	/*! [in,out] The control action request event structure. */
	UpnpActionRequest *ca_event);

/*!
 * \brief The callback handler registered with the SDK while registering
 * root device.
 *
 * Dispatches the request to the appropriate procedure
 * based on the value of EventType. The four requests handled by the 
 * device are: 
 *	\li 1) Event Subscription requests.  
 *	\li 2) Get Variable requests. 
 *	\li 3) Action requests.
 */
int CCTvDeviceCallbackEventHandler(
	/*! [in] The type of callback event. */
	Upnp_EventType,
	/*! [in] Data structure containing event data. */
	const void *Event,
	/*! [in] Optional data specified during callback registration. */
	void *Cookie);

/*!
 * \brief Update the CCTvDevice service state table, and notify all subscribed
 * control points of the updated state.
 *
 * Note that since this function blocks on the mutex CCTVDevMutex,
 * to avoid a hang this function should not be called within any other
 * function that currently has this mutex locked.
 */
int CCTvDeviceSetServiceTableVar(
	/*! [in] The service number (CCTV_SERVICE_CONTROL or CCTV_SERVICE_PICTURE). */
	unsigned int service,
	/*! [in] The variable number (CCTV_CONTROL_POWER, CCTV_CONTROL_CHANNEL,
	 * CCTV_CONTROL_VOLUME, CCTV_PICTURE_COLOR, CCTV_PICTURE_TINT,
	 * CCTV_PICTURE_CONTRAST, or CCTV_PICTURE_BRIGHTNESS). */
	int variable,
	/*! [in] The string representation of the new value. */
	char *value);

/* Control Service Actions */

/*!
 * \brief Turn the power on.
 */
int CCTvDevicePowerOn(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

/*!
 * \brief Turn the power off.
 */
int CCTvDevicePowerOff(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

int CCTvDeviceReboot(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

int CCTvDeviceBottomMountLeft(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

int CCTvDeviceBottomMountRight(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

int CCTvDeviceBottomMountMiddle(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);


int CCTvDeviceTopMountUp(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

int CCTvDeviceTopMountDown(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);

int CCTvDeviceTopMountMiddle(
	/*! [in] Document of action request. */
	IXML_Document *in,
	/*! [in] Action result. */
	IXML_Document **out,
	/*! [out] ErrorString in case action was unsuccessful. */
	const char **errorString);
/*!
 * \brief Change the channel, update the CCTvDevice control service
 * state table, and notify all subscribed control points of the
 * updated state.
 */
int CCTvDeviceStart(
	/*! [in] ip address to initialize the sdk (may be NULL)
	 * if null, then the first non null loopback address is used. */
	char *ip_address,
	/*! [in] port number to initialize the sdk (may be 0)
	 * if zero, then a random number is used. */
	unsigned short port,
	/*! [in] name of description document.
	 * may be NULL. Default is cctvdevicedesc.xml. */
	const char *desc_doc_name,
	/*! [in] path of web directory.
	 * may be NULL. Default is ./web (for Linux) or ../cctvdevice/web. */
	const char *web_dir_path,
	/*! [in] print function to use. */
	print_string pfun,
	/*! [in] Non-zero if called from the combo application. */
	int combo);

/*!
 * \brief Stops the device. Uninitializes the sdk.
 */
int CCTvDeviceStop(void);

/*!
 * \brief Function that receives commands from the user at the command prompt
 * during the lifetime of the device, and calls the appropriate
 * functions for those commands. Only one command, exit, is currently
 * defined.
 */
void *CCTvDeviceCommandLoop(void *args);

/*!
 * \brief Main entry point for cctv device application.
 *
 * Initializes and registers with the sdk.
 * Initializes the state stables of the service.
 * Starts the command loop.
 *
 * Accepts the following optional arguments:
 *	\li \c -ip ipaddress
 *	\li \c -port port
 *	\li \c -desc desc_doc_name
 *	\li \c -webdir web_dir_path
 *	\li \c -help
 */
int device_main(int argc, char *argv[]);

void exit_intr(int sig);
void init_watchdog(void);
void get_watchdog_timer(void);
void set_watchdog_timer(void);
void expire_watchdog_timer(int time);
void* watchdog_thread(void* unused);




#ifdef __cplusplus
}
#endif

/*! @} Control Point Sample API */

/*! @} UpnpSamples */

#endif /* UPNP_CCTV_DEVICE_H */
