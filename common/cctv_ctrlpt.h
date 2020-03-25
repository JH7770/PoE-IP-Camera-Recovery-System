#ifndef UPNP_CCTV_CTRLPT_H
#define UPNP_CCTV_CTRLPT_H

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
 * \name Contro Point Sample API
 *
 * @{
 *
 * \file
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "sample_util.h"

#include "upnp.h"
#include "UpnpString.h"
#include "upnptools.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>

#define CCTV_SERVICE_SERVCOUNT	1
#define CCTV_SERVICE_CONTROL	0

#define CCTV_CONTROL_VARCOUNT	2
#define CCTV_CONTROL_POWER	0
#define CCTV_CONTROL_TEMP

#define CCTV_MAX_VAL_LEN		5

#define CCTV_SUCCESS		0
#define CCTV_ERROR		(-1)
#define CCTV_WARNING		1

/* This should be the maximum VARCOUNT from above */
#define CCTV_MAXVARS		CCTV_CONTROL_VARCOUNT

extern const char *CCTvServiceName[];
extern const char *CCTvVarName[CCTV_SERVICE_SERVCOUNT][CCTV_MAXVARS];
extern char CCTvVarCount[];

struct cctv_service {
    char ServiceId[NAME_SIZE];
    char ServiceType[NAME_SIZE];
    char *VariableStrVal[CCTV_MAXVARS];
    char EventURL[NAME_SIZE];
    char ControlURL[NAME_SIZE];
    char SID[NAME_SIZE];
};

extern struct CCTvDeviceNode *GlobalDeviceList;

struct CCTvDevice {
    char UDN[250];
    char DescDocURL[250];
    char FriendlyName[250];
    char PresURL[250];
    int  AdvrTimeOut;
    struct cctv_service CCTvService[CCTV_SERVICE_SERVCOUNT];
};

struct CCTvDeviceNode {
    struct CCTvDevice device;
    struct CCTvDeviceNode *next;
};

extern ithread_mutex_t DeviceListMutex;

extern UpnpClient_Handle ctrlpt_handle;

void	CCTvCtrlPointPrintHelp(void);
int		CCTvCtrlPointDeleteNode(struct CCTvDeviceNode *);
int		CCTvCtrlPointRemoveDevice(const char *);
int		CCTvCtrlPointRemoveAll(void);
int		CCTvCtrlPointRefresh(void);

int		CCTvCtrlPointSendAction(int, int, const char *, const char **, char **, int);
int		CCTvCtrlPointSendActionNumericArg(int devnum, int service, const char *actionName, const char *paramName, int paramValue);
int		CCTvCtrlPointSendPowerOn(int devnum);
int		CCTvCtrlPointSendPowerOff(int devnum);
int		CCTvCtrlPointSendReboot(int devnum);
int		CCTvCtrlPointSendBottomMountLeft(int devnum);
int		CCTvCtrlPointSendBottomMountRight(int devnum);
int		CCTvCtrlPointSendBottomMountMiddle(int devnum);
int		CCTvCtrlPointSendTopMountUp(int devnum);
int		CCTvCtrlPointSendTopMountDown(int devnum);
int		CCTvCtrlPointSendTopMountMiddle(int devnum);


int		CCTvCtrlPointGetVar(int, int, const char *);
int		CCTvCtrlPointGetPower(int devnum);

int		CCTvCtrlPointGetDevice(int, struct CCTvDeviceNode **);
int		CCTvCtrlPointPrintList(void);
int		CCTvCtrlPointPrintDevice(int);
void	CCTvCtrlPointAddDevice(IXML_Document *, const char *, int); 
void    CCTvCtrlPointHandleGetVar(const char *, const char *, const DOMString);

/*!
 * \brief Update a CCTv state table. Called when an event is received.
 *
 * Note: this function is NOT thread save. It must be called from another
 * function that has locked the global device list.
 **/
void CCTvStateUpdate(
	/*! [in] The UDN of the parent device. */
	char *UDN,
	/*! [in] The service state table to update. */
	int Service,
	/*! [out] DOM document representing the XML received with the event. */
	IXML_Document *ChangedVariables,
	/*! [out] pointer to the state table for the CCTv  service to update. */
	char **State);

void	CCTvCtrlPointHandleEvent(const char *, int, IXML_Document *); 
void	CCTvCtrlPointHandleSubscribeUpdate(const char *, const Upnp_SID, int); 
int		CCTvCtrlPointCallbackEventHandler(Upnp_EventType, const void *, void *);

/*!
 * \brief Checks the advertisement each device in the global device list.
 *
 * If an advertisement expires, the device is removed from the list.
 *
 * If an advertisement is about to expire, a search request is sent for that
 * device.
 */
void CCTvCtrlPointVerifyTimeouts(
	/*! [in] The increment to subtract from the timeouts each time the
	 * function is called. */
	int incr);

void	CCTvCtrlPointPrintCommands(void);
void*	CCTvCtrlPointCommandLoop(void *);
int		CCTvCtrlPointStart(print_string printFunctionPtr, state_update updateFunctionPtr, int combo);
int		CCTvCtrlPointStop(void);
int		CCTvCtrlPointProcessCommand(char *cmdline);

/*!
 * \brief Print help info for this application.
 */
void CCTvCtrlPointPrintShortHelp(void);

/*!
 * \brief Print long help info for this application.
 */
void CCTvCtrlPointPrintLongHelp(void);

/*!
 * \briefPrint the list of valid command line commands to the user
 */
void CCTvCtrlPointPrintCommands(void);

/*!
 * \brief Function that receives commands from the user at the command prompt
 * during the lifetime of the device, and calls the appropriate
 * functions for those commands.
 */
void *CCTvCtrlPointCommandLoop(void *args);

/*!
 * \brief
 */
int CCTvCtrlPointProcessCommand(char *cmdline);

#ifdef __cplusplus
};
#endif


/*! @} Device Sample */

/*! @} UpnpSamples */

#endif /* UPNP_CCTV_CTRLPT_H */
