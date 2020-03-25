/*******************************************************************************
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
 ******************************************************************************/

/*!
 * \addtogroup UpnpSamples
 *
 * @{
 *
 * \name Control Point Sample Module
 *
 * @{
 *
 * \file
 */

#include "cctv_ctrlpt.h"

#include "upnp.h"

/*!
 * Mutex for protecting the global device list in a multi-threaded,
 * asynchronous environment. All functions should lock this mutex before
 * reading or writing the device list. 
 */
ithread_mutex_t DeviceListMutex;

UpnpClient_Handle ctrlpt_handle = -1;

/*! Device type for cctv device. */
const char CCTvDeviceType[] = "urn:schemas-upnp-org:device:cctvdevice:1";

/*! Service names.*/
const char *CCTvServiceName[] = { "Control" };

/*!
   Global arrays for storing variable names and counts for 
   CCTvControl and CCTvPicture services 
 */
const char *CCTvVarName[CCTV_SERVICE_SERVCOUNT][CCTV_MAXVARS] = {
		{ {"Power"},{"Temperature"}}
};
char CCTvVarCount[CCTV_SERVICE_SERVCOUNT] =
    { CCTV_CONTROL_VARCOUNT };

/*!
   Timeout to request during subscriptions 
 */
int default_timeout = 1801;

/*!
   The first node in the global device list, or NULL if empty 
 */
struct CCTvDeviceNode *GlobalDeviceList = NULL;

/********************************************************************************
 * CCTvCtrlPointDeleteNode
 *
 * Description: 
 *       Delete a device node from the global device list.  Note that this
 *       function is NOT thread safe, and should be called from another
 *       function that has already locked the global device list.
 *
 * Parameters:
 *   node -- The device node
 *
 ********************************************************************************/
int
CCTvCtrlPointDeleteNode( struct CCTvDeviceNode *node )
{
	int rc, service, var;

	if (NULL == node) {
		SampleUtil_Print
		    ("ERROR: CCTvCtrlPointDeleteNode: Node is empty\n");
		return CCTV_ERROR;
	}

	for (service = 0; service < CCTV_SERVICE_SERVCOUNT; service++) {
		/*
		   If we have a valid control SID, then unsubscribe 
		 */
		if (strcmp(node->device.CCTvService[service].SID, "") != 0) {
			rc = UpnpUnSubscribe(ctrlpt_handle,
					     node->device.CCTvService[service].
					     SID);
			if (UPNP_E_SUCCESS == rc) {
				SampleUtil_Print
				    ("Unsubscribed from CCTv %s EventURL with SID=%s\n",
				     CCTvServiceName[service],
				     node->device.CCTvService[service].SID);
			} else {
				SampleUtil_Print
				    ("Error unsubscribing to CCTv %s EventURL -- %d\n",
				     CCTvServiceName[service], rc);
			}
		}

		for (var = 0; var < CCTvVarCount[service]; var++) {
			if (node->device.CCTvService[service].VariableStrVal[var]) {
				free(node->device.
				     CCTvService[service].VariableStrVal[var]);
			}
		}
	}

	/*Notify New Device Added */
	SampleUtil_StateUpdate(NULL, NULL, node->device.UDN, DEVICE_REMOVED);
	free(node);
	node = NULL;

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointRemoveDevice
 *
 * Description: 
 *       Remove a device from the global device list.
 *
 * Parameters:
 *   UDN -- The Unique Device Name for the device to remove
 *
 ********************************************************************************/
int CCTvCtrlPointRemoveDevice(const char *UDN)
{
	struct CCTvDeviceNode *curdevnode;
	struct CCTvDeviceNode *prevdevnode;

	ithread_mutex_lock(&DeviceListMutex);

	curdevnode = GlobalDeviceList;
	if (!curdevnode) {
		SampleUtil_Print(
			"WARNING: CCTvCtrlPointRemoveDevice: Device list empty\n");
	} else {
		if (0 == strcmp(curdevnode->device.UDN, UDN)) {
			GlobalDeviceList = curdevnode->next;
			CCTvCtrlPointDeleteNode(curdevnode);
		} else {
			prevdevnode = curdevnode;
			curdevnode = curdevnode->next;
			while (curdevnode) {
				if (strcmp(curdevnode->device.UDN, UDN) == 0) {
					prevdevnode->next = curdevnode->next;
					CCTvCtrlPointDeleteNode(curdevnode);
					break;
				}
				prevdevnode = curdevnode;
				curdevnode = curdevnode->next;
			}
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointRemoveAll
 *
 * Description: 
 *       Remove all devices from the global device list.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int CCTvCtrlPointRemoveAll(void)
{
	struct CCTvDeviceNode *curdevnode, *next;

	ithread_mutex_lock(&DeviceListMutex);

	curdevnode = GlobalDeviceList;
	GlobalDeviceList = NULL;

	while (curdevnode) {
		next = curdevnode->next;
		CCTvCtrlPointDeleteNode(curdevnode);
		curdevnode = next;
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointRefresh
 *
 * Description: 
 *       Clear the current global device list and issue new search
 *	 requests to build it up again from scratch.
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int CCTvCtrlPointRefresh(void)
{
	int rc;

	CCTvCtrlPointRemoveAll();
	/* Search for all devices of type cctvdevice version 1,
	 * waiting for up to 5 seconds for the response */
	rc = UpnpSearchAsync(ctrlpt_handle, 5, CCTvDeviceType, NULL);
	if (UPNP_E_SUCCESS != rc) {
		SampleUtil_Print("Error sending search request%d\n", rc);

		return CCTV_ERROR;
	}

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointGetVar
 *
 * Description: 
 *       Send a GetVar request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   varname -- The name of the variable to request.
 *
 ********************************************************************************/
int CCTvCtrlPointGetVar(int service, int devnum, const char *varname)
{
	struct CCTvDeviceNode *devnode;
	int rc;

	ithread_mutex_lock(&DeviceListMutex);

	rc = CCTvCtrlPointGetDevice(devnum, &devnode);

	if (CCTV_SUCCESS == rc) {
		rc = UpnpGetServiceVarStatusAsync(
			ctrlpt_handle,
			devnode->device.CCTvService[service].ControlURL,
			varname,
			CCTvCtrlPointCallbackEventHandler,
			NULL);
		if (rc != UPNP_E_SUCCESS) {
			SampleUtil_Print(
				"Error in UpnpGetServiceVarStatusAsync -- %d\n",
				rc);
			rc = CCTV_ERROR;
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return rc;
}

int CCTvCtrlPointGetPower(int devnum)
{
	return CCTvCtrlPointGetVar(CCTV_SERVICE_CONTROL, devnum, "Power");
}

/********************************************************************************
 * CCTvCtrlPointSendAction
 *
 * Description: 
 *       Send an Action request to the specified service of a device.
 *
 * Parameters:
 *   service -- The service
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   actionname -- The name of the action.
 *   param_name -- An array of parameter names
 *   param_val -- The corresponding parameter values
 *   param_count -- The number of parameters
 *
 ********************************************************************************/
int CCTvCtrlPointSendAction(
	int service,
	int devnum,
	const char *actionname,
	const char **param_name,
	char **param_val,
	int param_count)
{
	struct CCTvDeviceNode *devnode;
	IXML_Document *actionNode = NULL;
	int rc = CCTV_SUCCESS;
	int param;

	ithread_mutex_lock(&DeviceListMutex);

	rc = CCTvCtrlPointGetDevice(devnum, &devnode);
	if (CCTV_SUCCESS == rc) {
		if (0 == param_count) {
			actionNode =
			    UpnpMakeAction(actionname, CCTvServiceType[service],
					   0, NULL);
		} else {
			for (param = 0; param < param_count; param++) {
				if (UpnpAddToAction
				    (&actionNode, actionname,
				     CCTvServiceType[service], param_name[param],
				     param_val[param]) != UPNP_E_SUCCESS) {
					SampleUtil_Print
					    ("ERROR: CCTvCtrlPointSendAction: Trying to add action param\n");
					/*return -1; // TBD - BAD! leaves mutex locked */
				}
			}
		}

		rc = UpnpSendActionAsync(ctrlpt_handle,
					 devnode->device.
					 CCTvService[service].ControlURL,
					 CCTvServiceType[service], NULL,
					 actionNode,
					 CCTvCtrlPointCallbackEventHandler, NULL);

		if (rc != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in UpnpSendActionAsync -- %d\n",
					 rc);
			rc = CCTV_ERROR;
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);

	if (actionNode)
		ixmlDocument_free(actionNode);

	return rc;
}

/********************************************************************************
 * CCTvCtrlPointSendActionNumericArg
 *
 * Description:Send an action with one argument to a device in the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list, starting with 1)
 *   service -- CCTV_SERVICE_CONTROL or CCTV_SERVICE_PICTURE
 *   actionName -- The device action, i.e., "SetChannel"
 *   paramName -- The name of the parameter that is being passed
 *   paramValue -- Actual value of the parameter being passed
 *
 ********************************************************************************/
int CCTvCtrlPointSendActionNumericArg(int devnum, int service,
	const char *actionName, const char *paramName, int paramValue)
{
	char param_val_a[50];
	char *param_val = param_val_a;

	sprintf(param_val_a, "%d", paramValue);
	return CCTvCtrlPointSendAction(
		service, devnum, actionName, &paramName,
		&param_val, 1);
}

int CCTvCtrlPointSendPowerOn(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "PowerOn", NULL, NULL, 0);
}

int CCTvCtrlPointSendPowerOff(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "PowerOff", NULL, NULL, 0);
}

int CCTvCtrlPointSendReboot(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "Reboot", NULL, NULL, 0);
}
int CCTvCtrlPointSendBottomMountLeft(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "BottomMountLeft",NULL,NULL,0);
}

int CCTvCtrlPointSendBottomMountRight(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "BottomMountRight",NULL,NULL,0);

}
int CCTvCtrlPointSendBottomMountMiddle(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "BottomMountMiddle",NULL,NULL,0);

}
int CCTvCtrlPointSendTopMountUp(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "TopMountUp",NULL,NULL,0);
}
int CCTvCtrlPointSendTopMountDown(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "TopMountDown",NULL,NULL,0);
}
int CCTvCtrlPointSendTopMountMiddle(int devnum)
{
	return CCTvCtrlPointSendAction(
		CCTV_SERVICE_CONTROL, devnum, "TopMountMiddle",NULL,NULL,0);
}
/********************************************************************************
 * CCTvCtrlPointGetDevice
 *
 * Description: 
 *       Given a list number, returns the pointer to the device
 *       node at that position in the global device list.  Note
 *       that this function is not thread safe.  It must be called 
 *       from a function that has locked the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *   devnode -- The output device node pointer
 *
 ********************************************************************************/
int CCTvCtrlPointGetDevice(int devnum, struct CCTvDeviceNode **devnode)
{
	int count = devnum;
	struct CCTvDeviceNode *tmpdevnode = NULL;

	if (count)
		tmpdevnode = GlobalDeviceList;
	while (--count && tmpdevnode) {
		tmpdevnode = tmpdevnode->next;
	}
	if (!tmpdevnode) {
		SampleUtil_Print("Error finding CCTvDevice number -- %d\n",
				 devnum);
		return CCTV_ERROR;
	}
	*devnode = tmpdevnode;

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointPrintList
 *
 * Description: 
 *       Print the universal device names for each device in the global device list
 *
 * Parameters:
 *   None
 *
 ********************************************************************************/
int CCTvCtrlPointPrintList()
{
	struct CCTvDeviceNode *tmpdevnode;
	int i = 0;

	ithread_mutex_lock(&DeviceListMutex);

	SampleUtil_Print("CCTvCtrlPointPrintList:\n");
	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		SampleUtil_Print(" %3d -- %s\n", ++i, tmpdevnode->device.UDN);
		tmpdevnode = tmpdevnode->next;
	}
	SampleUtil_Print("\n");
	ithread_mutex_unlock(&DeviceListMutex);

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointPrintDevice
 *
 * Description: 
 *       Print the identifiers and state table for a device from
 *       the global device list.
 *
 * Parameters:
 *   devnum -- The number of the device (order in the list,
 *             starting with 1)
 *
 ********************************************************************************/
int CCTvCtrlPointPrintDevice(int devnum)
{
	struct CCTvDeviceNode *tmpdevnode;
	int i = 0, service, var;
	char spacer[15];

	if (devnum <= 0) {
		SampleUtil_Print(
			"Error in CCTvCtrlPointPrintDevice: "
			"invalid devnum = %d\n",
			devnum);
		return CCTV_ERROR;
	}

	ithread_mutex_lock(&DeviceListMutex);

	SampleUtil_Print("CCTvCtrlPointPrintDevice:\n");
	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		i++;
		if (i == devnum)
			break;
		tmpdevnode = tmpdevnode->next;
	}
	if (!tmpdevnode) {
		SampleUtil_Print(
			"Error in CCTvCtrlPointPrintDevice: "
			"invalid devnum = %d  --  actual device count = %d\n",
			devnum, i);
	} else {
		SampleUtil_Print(
			"  CCTvDevice -- %d\n"
			"    |                  \n"
			"    +- UDN        = %s\n"
			"    +- DescDocURL     = %s\n"
			"    +- FriendlyName   = %s\n"
			"    +- PresURL        = %s\n"
			"    +- Adver. TimeOut = %d\n",
			devnum,
			tmpdevnode->device.UDN,
			tmpdevnode->device.DescDocURL,
			tmpdevnode->device.FriendlyName,
			tmpdevnode->device.PresURL,
			tmpdevnode->device.AdvrTimeOut);
		for (service = 0; service < CCTV_SERVICE_SERVCOUNT; service++) {
			if (service < CCTV_SERVICE_SERVCOUNT - 1)
				sprintf(spacer, "    |    ");
			else
				sprintf(spacer, "         ");
			SampleUtil_Print(
				"    |                  \n"
				"    +- CCTv %s Service\n"
				"%s+- ServiceId       = %s\n"
				"%s+- ServiceType     = %s\n"
				"%s+- EventURL        = %s\n"
				"%s+- ControlURL      = %s\n"
				"%s+- SID             = %s\n"
				"%s+- ServiceStateTable\n",
				CCTvServiceName[service],
				spacer,
				tmpdevnode->device.CCTvService[service].ServiceId,
				spacer,
				tmpdevnode->device.CCTvService[service].ServiceType,
				spacer,
				tmpdevnode->device.CCTvService[service].EventURL,
				spacer,
				tmpdevnode->device.CCTvService[service].ControlURL,
				spacer,
				tmpdevnode->device.CCTvService[service].SID,
				spacer);
			for (var = 0; var < CCTvVarCount[service]; var++) {
				SampleUtil_Print(
					"%s     +- %-10s = %s\n",
					spacer,
					CCTvVarName[service][var],
					tmpdevnode->device.CCTvService[service].VariableStrVal[var]);
			}
		}
	}
	SampleUtil_Print("\n");
	ithread_mutex_unlock(&DeviceListMutex);

	return CCTV_SUCCESS;
}

/********************************************************************************
 * CCTvCtrlPointAddDevice
 *
 * Description: 
 *       If the device is not already included in the global device list,
 *       add it.  Otherwise, update its advertisement expiration timeout.
 *
 * Parameters:
 *   DescDoc -- The description document for the device
 *   location -- The location of the description document URL
 *   expires -- The expiration time for this advertisement
 *
 ********************************************************************************/
void CCTvCtrlPointAddDevice(
	IXML_Document *DescDoc,
	const char *location,
	int expires)
{
	char *deviceType = NULL;
	char *friendlyName = NULL;
	char *presURL = NULL;
	char *baseURL = NULL;
	char *relURL = NULL;
	char *UDN = NULL;
	char *serviceId[CCTV_SERVICE_SERVCOUNT] = { NULL, NULL };
	char *eventURL[CCTV_SERVICE_SERVCOUNT] = { NULL, NULL };
	char *controlURL[CCTV_SERVICE_SERVCOUNT] = { NULL, NULL };
	Upnp_SID eventSID[CCTV_SERVICE_SERVCOUNT];
	int TimeOut[CCTV_SERVICE_SERVCOUNT] = {
		default_timeout,
		default_timeout
	};
	struct CCTvDeviceNode *deviceNode;
	struct CCTvDeviceNode *tmpdevnode;
	int ret = 1;
	int found = 0;
	int service;
	int var;

	ithread_mutex_lock(&DeviceListMutex);

	/* Read key elements from description document */
	UDN = SampleUtil_GetFirstDocumentItem(DescDoc, "UDN");
	deviceType = SampleUtil_GetFirstDocumentItem(DescDoc, "deviceType");
	friendlyName = SampleUtil_GetFirstDocumentItem(DescDoc, "friendlyName");
	baseURL = SampleUtil_GetFirstDocumentItem(DescDoc, "URLBase");
	relURL = SampleUtil_GetFirstDocumentItem(DescDoc, "presentationURL");

	ret = UpnpResolveURL2((baseURL ? baseURL : location), relURL, &presURL);

/*	if (UPNP_E_SUCCESS != ret)
		SampleUtil_Print("Error generating presURL from %s + %s\n",
				 baseURL, relURL);*/

	if (strcmp(deviceType, CCTvDeviceType) == 0) {

		/* Check if this device is already in the list */
		tmpdevnode = GlobalDeviceList;
		while (tmpdevnode) {
			if (strcmp(tmpdevnode->device.UDN, UDN) == 0) {
				found = 1;
				break;
			}
			tmpdevnode = tmpdevnode->next;
		}

		if (found) {
			/* The device is already there, so just update  */
			/* the advertisement timeout field */
			tmpdevnode->device.AdvrTimeOut = expires;
		} else {
			
			SampleUtil_Print("=========Found CCTv device=========\n");
			for (service = 0; service < CCTV_SERVICE_SERVCOUNT;
			     service++) {
				if (SampleUtil_FindAndParseService
				    (DescDoc, location, CCTvServiceType[service],
				     &serviceId[service], &eventURL[service],
				     &controlURL[service])) {
					SampleUtil_Print
					    ("Subscribing to EventURL %s...\n",
					     eventURL[service]);
					ret =
					    UpnpSubscribe(ctrlpt_handle,
							  eventURL[service],
							  &TimeOut[service],
							  eventSID[service]);
					if (ret == UPNP_E_SUCCESS) {
						SampleUtil_Print
						    ("Subscribed to EventURL with SID=%s\n",
						     eventSID[service]);
					} else {
						SampleUtil_Print
						    ("Error Subscribing to EventURL -- %d\n",
						     ret);
						strcpy(eventSID[service], "");
					}
				} else {
					SampleUtil_Print
					    ("Error: Could not find Service: %s\n",
					     CCTvServiceType[service]);
				}
			}
			/* Create a new device node */
			deviceNode =
			    (struct CCTvDeviceNode *)
			    malloc(sizeof(struct CCTvDeviceNode));
			strcpy(deviceNode->device.UDN, UDN);
			strcpy(deviceNode->device.DescDocURL, location);
			strcpy(deviceNode->device.FriendlyName, friendlyName);
			strcpy(deviceNode->device.PresURL, presURL);
			deviceNode->device.AdvrTimeOut = expires;
			for (service = 0; service < CCTV_SERVICE_SERVCOUNT;
			     service++) {
				if (serviceId[service] == NULL) {
					/* not found */
					continue;
				}
				strcpy(deviceNode->device.CCTvService[service].
				       ServiceId, serviceId[service]);
				strcpy(deviceNode->device.CCTvService[service].
				       ServiceType, CCTvServiceType[service]);
				strcpy(deviceNode->device.CCTvService[service].
				       ControlURL, controlURL[service]);
				strcpy(deviceNode->device.CCTvService[service].
				       EventURL, eventURL[service]);
				strcpy(deviceNode->device.CCTvService[service].
				       SID, eventSID[service]);
				for (var = 0; var < CCTvVarCount[service]; var++) {
					deviceNode->device.
					    CCTvService[service].VariableStrVal
					    [var] =
					    (char *)malloc(CCTV_MAX_VAL_LEN);
					strcpy(deviceNode->device.
					       CCTvService[service].VariableStrVal
					       [var], "");
				}
			}
			deviceNode->next = NULL;
			/* Insert the new device node in the list */
			if ((tmpdevnode = GlobalDeviceList)) {
				while (tmpdevnode) {
					if (tmpdevnode->next) {
						tmpdevnode = tmpdevnode->next;
					} else {
						tmpdevnode->next = deviceNode;
						break;
					}
				}
			} else {
				GlobalDeviceList = deviceNode;
			}
			/*Notify New Device Added */
			SampleUtil_StateUpdate(NULL, NULL,
					       deviceNode->device.UDN,
					       DEVICE_ADDED);
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);

	if (deviceType)
		free(deviceType);
	if (friendlyName)
		free(friendlyName);
	if (UDN)
		free(UDN);
	if (baseURL)
		free(baseURL);
	if (relURL)
		free(relURL);
	if (presURL)
		free(presURL);
	for (service = 0; service < CCTV_SERVICE_SERVCOUNT; service++) {
		if (serviceId[service])
			free(serviceId[service]);
		if (controlURL[service])
			free(controlURL[service]);
		if (eventURL[service])
			free(eventURL[service]);
	}
}

void CCTvStateUpdate(char *UDN, int Service, IXML_Document *ChangedVariables,
		   char **State)
{
	IXML_NodeList *properties;
	IXML_NodeList *variables;
	IXML_Element *property;
	IXML_Element *variable;
	long unsigned int length;
	long unsigned int length1;
	long unsigned int i;
	int j;
	char *tmpstate = NULL;

	SampleUtil_Print("CCTv State Update (service %d):\n", Service);
	/* Find all of the e:property tags in the document */
	properties = ixmlDocument_getElementsByTagName(ChangedVariables,
		"e:property");
	if (properties) {
		length = ixmlNodeList_length(properties);
		for (i = 0; i < length; i++) {
			/* Loop through each property change found */
			property = (IXML_Element *)ixmlNodeList_item(
				properties, i);
			/* For each variable name in the state table,
			 * check if this is a corresponding property change */
			for (j = 0; j < CCTvVarCount[Service]; j++) {
				variables = ixmlElement_getElementsByTagName(
					property, CCTvVarName[Service][j]);
				/* If a match is found, extract 
				 * the value, and update the state table */
				if (variables) {
					length1 = ixmlNodeList_length(variables);
					if (length1) {
						variable = (IXML_Element *)
							ixmlNodeList_item(variables, 0);
						tmpstate =
						    SampleUtil_GetElementValue(variable);
						if (tmpstate) {
							strcpy(State[j], tmpstate);
							SampleUtil_Print(
								" Variable Name: %s New Value:'%s'\n",
								CCTvVarName[Service][j], State[j]);
						}
						if (tmpstate)
							free(tmpstate);
						tmpstate = NULL;
					}
					ixmlNodeList_free(variables);
					variables = NULL;
				}
			}
		}
		ixmlNodeList_free(properties);
	}
	return;
	UDN = UDN;
}

/********************************************************************************
 * CCTvCtrlPointHandleEvent
 *
 * Description: 
 *       Handle a UPnP event that was received.  Process the event and update
 *       the appropriate service state table.
 *
 * Parameters:
 *   sid -- The subscription id for the event
 *   eventkey -- The eventkey number for the event
 *   changes -- The DOM document representing the changes
 *
 ********************************************************************************/
void CCTvCtrlPointHandleEvent(
	const char *sid,
	int evntkey,
	IXML_Document *changes)
{
	struct CCTvDeviceNode *tmpdevnode;
	int service;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		for (service = 0; service < CCTV_SERVICE_SERVCOUNT; ++service) {
			if (strcmp(tmpdevnode->device.CCTvService[service].SID, sid) ==  0) {
				SampleUtil_Print("Received CCTv %s Event: %d for SID %s\n",
					CCTvServiceName[service],
					evntkey,
					sid);
				CCTvStateUpdate(
					tmpdevnode->device.UDN,
					service,
					changes,
					(char **)&tmpdevnode->device.CCTvService[service].VariableStrVal);
				break;
			}
		}
		tmpdevnode = tmpdevnode->next;
	}

	ithread_mutex_unlock(&DeviceListMutex);
}

/********************************************************************************
 * CCTvCtrlPointHandleSubscribeUpdate
 *
 * Description: 
 *       Handle a UPnP subscription update that was received.  Find the 
 *       service the update belongs to, and update its subscription
 *       timeout.
 *
 * Parameters:
 *   eventURL -- The event URL for the subscription
 *   sid -- The subscription id for the subscription
 *   timeout  -- The new timeout for the subscription
 *
 ********************************************************************************/
void CCTvCtrlPointHandleSubscribeUpdate(
	const char *eventURL,
	const Upnp_SID sid,
	int timeout)
{
	struct CCTvDeviceNode *tmpdevnode;
	int service;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		for (service = 0; service < CCTV_SERVICE_SERVCOUNT; service++) {
			if (strcmp
			    (tmpdevnode->device.CCTvService[service].EventURL,
			     eventURL) == 0) {
				SampleUtil_Print
				    ("Received CCTv %s Event Renewal for eventURL %s\n",
				     CCTvServiceName[service], eventURL);
				strcpy(tmpdevnode->device.CCTvService[service].
				       SID, sid);
				break;
			}
		}

		tmpdevnode = tmpdevnode->next;
	}

	ithread_mutex_unlock(&DeviceListMutex);

	return;
	timeout = timeout;
}

void CCTvCtrlPointHandleGetVar(
	const char *controlURL,
	const char *varName,
	const DOMString varValue)
{

	struct CCTvDeviceNode *tmpdevnode;
	int service;

	ithread_mutex_lock(&DeviceListMutex);

	tmpdevnode = GlobalDeviceList;
	while (tmpdevnode) {
		for (service = 0; service < CCTV_SERVICE_SERVCOUNT; service++) {
			if (strcmp
			    (tmpdevnode->device.CCTvService[service].ControlURL,
			     controlURL) == 0) {
				SampleUtil_StateUpdate(varName, varValue,
						       tmpdevnode->device.UDN,
						       GET_VAR_COMPLETE);
				break;
			}
		}
		tmpdevnode = tmpdevnode->next;
	}

	ithread_mutex_unlock(&DeviceListMutex);
}

/********************************************************************************
 * CCTvCtrlPointCallbackEventHandler
 *
 * Description: 
 *       The callback handler registered with the SDK while registering
 *       the control point.  Detects the type of callback, and passes the 
 *       request on to the appropriate function.
 *
 * Parameters:
 *   EventType -- The type of callback event
 *   Event -- Data structure containing event data
 *   Cookie -- Optional data specified during callback registration
 *
 ********************************************************************************/
int CCTvCtrlPointCallbackEventHandler(Upnp_EventType EventType, const void *Event, void *Cookie)
{
	int errCode = 0;

//	SampleUtil_PrintEvent(EventType, Event);
	switch ( EventType ) {
	/* SSDP Stuff */
	case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
	case UPNP_DISCOVERY_SEARCH_RESULT: {
		const UpnpDiscovery *d_event = (UpnpDiscovery *)Event;
		IXML_Document *DescDoc = NULL;
		const char *location = NULL;
		int errCode = UpnpDiscovery_get_ErrCode(d_event);

		if (errCode != UPNP_E_SUCCESS) {
			SampleUtil_Print(
				"Error in Discovery Callback -- %d\n", errCode);
		}

		location = UpnpString_get_String(UpnpDiscovery_get_Location(d_event));
		errCode = UpnpDownloadXmlDoc(location, &DescDoc);
		if (errCode != UPNP_E_SUCCESS) {
			SampleUtil_Print(
				"Error obtaining device description from %s -- error = %d\n",
				location, errCode);
		} else {
			CCTvCtrlPointAddDevice(
				DescDoc, location, UpnpDiscovery_get_Expires(d_event));
		}
		if (DescDoc) {
			ixmlDocument_free(DescDoc);
		}
		//CCTvCtrlPointPrintList();
		break;
	}
	case UPNP_DISCOVERY_SEARCH_TIMEOUT:
		/* Nothing to do here... */
		break;
	case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE: {
		UpnpDiscovery *d_event = (UpnpDiscovery *)Event;
		int errCode = UpnpDiscovery_get_ErrCode(d_event);
		const char *deviceId = UpnpString_get_String(
		UpnpDiscovery_get_DeviceID(d_event));

		if (errCode != UPNP_E_SUCCESS) {
			SampleUtil_Print(
				"Error in Discovery ByeBye Callback -- %d\n", errCode);
		}
		SampleUtil_Print("Received ByeBye for Device: %s\n", deviceId);
		CCTvCtrlPointRemoveDevice(deviceId);
		SampleUtil_Print("After byebye:\n");
		CCTvCtrlPointPrintList();
		break;
	}
	/* SOAP Stuff */
	case UPNP_CONTROL_ACTION_COMPLETE: {
		UpnpActionComplete *a_event = (UpnpActionComplete *)Event;
		int errCode = UpnpActionComplete_get_ErrCode(a_event);
		if (errCode != UPNP_E_SUCCESS) {
			SampleUtil_Print("Error in  Action Complete Callback -- %d\n",
				errCode);
		}
		/* No need for any processing here, just print out results.
		 * Service state table updates are handled by events. */
		break;
	}
	case UPNP_CONTROL_GET_VAR_COMPLETE: {
		UpnpStateVarComplete *sv_event = (UpnpStateVarComplete *)Event;
		int errCode = UpnpStateVarComplete_get_ErrCode(sv_event);
		if (errCode != UPNP_E_SUCCESS) {
			SampleUtil_Print(
				"Error in Get Var Complete Callback -- %d\n", errCode);
		} else {
			CCTvCtrlPointHandleGetVar(
				UpnpString_get_String(UpnpStateVarComplete_get_CtrlUrl(sv_event)),
				UpnpString_get_String(UpnpStateVarComplete_get_StateVarName(sv_event)),
				UpnpStateVarComplete_get_CurrentVal(sv_event));
		}
		break;
	}
	/* GENA Stuff */
	case UPNP_EVENT_RECEIVED: {
		UpnpEvent *e_event = (UpnpEvent *)Event;
		CCTvCtrlPointHandleEvent(
			UpnpEvent_get_SID_cstr(e_event),
			UpnpEvent_get_EventKey(e_event),
			UpnpEvent_get_ChangedVariables(e_event));
		break;
	}
	case UPNP_EVENT_SUBSCRIBE_COMPLETE:
	case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
	case UPNP_EVENT_RENEWAL_COMPLETE: {
		UpnpEventSubscribe *es_event = (UpnpEventSubscribe *)Event;

		errCode = UpnpEventSubscribe_get_ErrCode(es_event);
		if (errCode != UPNP_E_SUCCESS) {
			SampleUtil_Print(
				"Error in Event Subscribe Callback -- %d\n", errCode);
		} else {
			CCTvCtrlPointHandleSubscribeUpdate(
				UpnpString_get_String(UpnpEventSubscribe_get_PublisherUrl(es_event)),
				UpnpString_get_String(UpnpEventSubscribe_get_SID(es_event)),
				UpnpEventSubscribe_get_TimeOut(es_event));
		}
		break;
	}
	case UPNP_EVENT_AUTORENEWAL_FAILED:
	case UPNP_EVENT_SUBSCRIPTION_EXPIRED: {
		UpnpEventSubscribe *es_event = (UpnpEventSubscribe *)Event;
		int TimeOut = default_timeout;
		Upnp_SID newSID;

		errCode = UpnpSubscribe(
			ctrlpt_handle,
			UpnpString_get_String(UpnpEventSubscribe_get_PublisherUrl(es_event)),
			&TimeOut,
			newSID);
		if (errCode == UPNP_E_SUCCESS) {
			SampleUtil_Print("Subscribed to EventURL with SID=%s\n", newSID);
			CCTvCtrlPointHandleSubscribeUpdate(
				UpnpString_get_String(UpnpEventSubscribe_get_PublisherUrl(es_event)),
				newSID,
				TimeOut);
		} else {
			SampleUtil_Print("Error Subscribing to EventURL -- %d\n", errCode);
		}
		break;
	}
	/* ignore these cases, since this is not a device */
	case UPNP_EVENT_SUBSCRIPTION_REQUEST:
	case UPNP_CONTROL_GET_VAR_REQUEST:
	case UPNP_CONTROL_ACTION_REQUEST:
		break;
	}

	return 0;
	Cookie = Cookie;
}

void CCTvCtrlPointVerifyTimeouts(int incr)
{
	struct CCTvDeviceNode *prevdevnode;
	struct CCTvDeviceNode *curdevnode;
	int ret;

	ithread_mutex_lock(&DeviceListMutex);

	prevdevnode = NULL;
	curdevnode = GlobalDeviceList;
	while (curdevnode) {
		curdevnode->device.AdvrTimeOut -= incr;
		/*SampleUtil_Print("Advertisement Timeout: %d\n", curdevnode->device.AdvrTimeOut); */
		if (curdevnode->device.AdvrTimeOut <= 0) {
			/* This advertisement has expired, so we should remove the device
			 * from the list */
			if (GlobalDeviceList == curdevnode)
				GlobalDeviceList = curdevnode->next;
			else
				prevdevnode->next = curdevnode->next;
			CCTvCtrlPointDeleteNode(curdevnode);
			if (prevdevnode)
				curdevnode = prevdevnode->next;
			else
				curdevnode = GlobalDeviceList;
		} else {
			if (curdevnode->device.AdvrTimeOut < 2 * incr) {
				/* This advertisement is about to expire, so
				 * send out a search request for this device
				 * UDN to try to renew */
				ret = UpnpSearchAsync(ctrlpt_handle, incr,
						      curdevnode->device.UDN,
						      NULL);
				if (ret != UPNP_E_SUCCESS)
					SampleUtil_Print
					    ("Error sending search request for Device UDN: %s -- err = %d\n",
					     curdevnode->device.UDN, ret);
			}
			prevdevnode = curdevnode;
			curdevnode = curdevnode->next;
		}
	}

	ithread_mutex_unlock(&DeviceListMutex);
}

/*!
 * \brief Function that runs in its own thread and monitors advertisement
 * and subscription timeouts for devices in the global device list.
 */
static int CCTvCtrlPointTimerLoopRun = 1;
void *CCTvCtrlPointTimerLoop(void *args)
{
	/* how often to verify the timeouts, in seconds */
	int incr = 30;

	while (CCTvCtrlPointTimerLoopRun) {
		isleep((unsigned int)incr);
		CCTvCtrlPointVerifyTimeouts(incr);
	}

	return NULL;
	args = args;
}

/*!
 * \brief Call this function to initialize the UPnP library and start the CCTV
 * Control Point.  This function creates a timer thread and provides a
 * callback handler to process any UPnP events that are received.
 *
 * \return CCTV_SUCCESS if everything went well, else CCTV_ERROR.
 */
int CCTvCtrlPointStart(print_string printFunctionPtr, state_update updateFunctionPtr, int combo)
{
	ithread_t timer_thread;
	int rc;
	unsigned short port = 0;
	char *ip_address = NULL;

	SampleUtil_Initialize(printFunctionPtr);
	SampleUtil_RegisterUpdateFunction(updateFunctionPtr);

	ithread_mutex_init(&DeviceListMutex, 0);

	SampleUtil_Print("Initializing UPnP Sdk with\n"
			 "\tipaddress = %s port = %u\n",
			 ip_address ? ip_address : "{NULL}", port);

	rc = UpnpInit2(ip_address, port);
	if (rc != UPNP_E_SUCCESS) {
		SampleUtil_Print("WinCEStart: UpnpInit2() Error: %d\n", rc);
		if (!combo) {
			UpnpFinish();

			return CCTV_ERROR;
		}
	}
	if (!ip_address) {
		ip_address = UpnpGetServerIpAddress();
	}
	if (!port) {
		port = UpnpGetServerPort();
	}

	SampleUtil_Print("UPnP Initialized\n"
			 "\tipaddress = %s port = %u\n",
			 ip_address ? ip_address : "{NULL}", port);
	SampleUtil_Print("Registering Control Point\n");
	rc = UpnpRegisterClient(CCTvCtrlPointCallbackEventHandler,
				&ctrlpt_handle, &ctrlpt_handle);
	if (rc != UPNP_E_SUCCESS) {
		SampleUtil_Print("Error registering CP: %d\n", rc);
		UpnpFinish();

		return CCTV_ERROR;
	}

	SampleUtil_Print("Control Point Registered\n");

	CCTvCtrlPointRefresh();

	/* start a timer thread */
	ithread_create(&timer_thread, NULL, CCTvCtrlPointTimerLoop, NULL);
	ithread_detach(timer_thread);

	return CCTV_SUCCESS;
}

int CCTvCtrlPointStop(void)
{
	CCTvCtrlPointTimerLoopRun = 0;
	CCTvCtrlPointRemoveAll();
	UpnpUnRegisterClient( ctrlpt_handle );
	UpnpFinish();
	SampleUtil_Finish();

	return CCTV_SUCCESS;
}

void CCTvCtrlPointPrintShortHelp(void)
{
	SampleUtil_Print(
		"Commands:\n"
		"  Help\n"
		"  HelpFull\n"
		"  ListDev\n"
		"  Refresh\n"
		"  PrintDev          <devnum>\n"
		"  PowerOn           <devnum>\n"
		"  PowerOff          <devnum>\n"
		"  Reboot            <devnum>\n"
		"  BottomMountLeft   <devnum>\n"
		"  BottomMountRight  <devnum>\n"
		"  BottomMountMiddle <devnum>\n"
		"  TopMountUp        <devnum>\n"
		"  TopMountDown      <devnum>\n"
		"  TopMountMiddle    <devnum>\n"
		"  CtrlAction        <devnum> <action>\n"
		"  PictAction        <devnum> <action>\n"
		"  CtrlGetVar        <devnum> <varname>\n"
		"  PictGetVar        <devnum> <action>\n"
		"  Exit\n");
}

void CCTvCtrlPointPrintLongHelp(void)
{
	SampleUtil_Print(
		"\n"
		"******************************\n"
		"* CCTV Control Point Help Info *\n"
		"******************************\n"
		"\n"
		"This sample control point application automatically searches\n"
		"for and subscribes to the services of television device emulator\n"
		"devices, described in the cctvdevicedesc.xml description document.\n"
		"It also registers itself as a cctv device.\n"
		"\n"
		"Commands:\n"
		"  Help\n"
		"       Print this help info.\n"
		"  ListDev\n"
		"       Print the current list of CCTV Device Emulators that this\n"
		"         control point is aware of.  Each device is preceded by a\n"
		"         device number which corresponds to the devnum argument of\n"
		"         commands listed below.\n"
		"  Refresh\n"
		"       Delete all of the devices from the device list and issue new\n"
		"         search request to rebuild the list from scratch.\n"
		"  PrintDev       <devnum>\n"
		"       Print the state table for the device <devnum>.\n"
		"         e.g., 'PrintDev 1' prints the state table for the first\n"
		"         device in the device list.\n"
		"  PowerOn        <devnum>\n"
		"       Sends the PowerOn action to the Control Service of\n"
		"         device <devnum>.\n"
		"  PowerOff       <devnum>\n"
		"       Sends the PowerOff action to the Control Service of\n"
		"         device <devnum>.\n"
		"  CtrlAction     <devnum> <action>\n"
		"       Sends an action request specified by the string <action>\n"
		"         to the Control Service of device <devnum>.  This command\n"
		"         only works for actions that have no arguments.\n"
		"         (e.g., \"CtrlAction 1 IncreaseChannel\")\n"
		"  PictAction     <devnum> <action>\n"
		"       Sends an action request specified by the string <action>\n"
		"         to the Picture Service of device <devnum>.  This command\n"
		"         only works for actions that have no arguments.\n"
		"         (e.g., \"PictAction 1 DecreaseContrast\")\n"
		"  CtrlGetVar     <devnum> <varname>\n"
		"       Requests the value of a variable specified by the string <varname>\n"
		"         from the Control Service of device <devnum>.\n"
		"         (e.g., \"CtrlGetVar 1 Volume\")\n"
		"  PictGetVar     <devnum> <action>\n"
		"       Requests the value of a variable specified by the string <varname>\n"
		"         from the Picture Service of device <devnum>.\n"
		"         (e.g., \"PictGetVar 1 Tint\")\n"
		"  Exit\n"
		"       Exits the control point application.\n");
}

/*! Tags for valid commands issued at the command prompt. */
enum cmdloop_cctvcmds {
	PRTHELP = 0,
	PRTFULLHELP,
	POWON,
	POWOFF,
	REBOOT,
	BOTMNTLEFT,
	BOTMNTRIGHT,
	BOTMNTMID,
	TOPMNTLEFT,
	TOPMNTRIGHT,
	TOPMNTMID,
	CTRLACTION,
	CTRLGETVAR,
	PRTDEV,
	LSTDEV,
	REFRESH,
	EXITCMD
};

/*! Data structure for parsing commands from the command line. */
struct cmdloop_commands {
	/* the string  */
	const char *str;
	/* the command */
	int cmdnum;
	/* the number of arguments */
	int numargs;
	/* the args */
	const char *args;
} cmdloop_commands;

/*! Mappings between command text names, command tag,
 * and required command arguments for command line
 * commands */
static struct cmdloop_commands cmdloop_cmdlist[] = {
	{"Help",          	PRTHELP,     1, ""},
	{"HelpFull",      	PRTFULLHELP, 1, ""},
	{"ListDev",       	LSTDEV,      1, ""},
	{"Refresh",       	REFRESH,     1, ""},
	{"PrintDev",      	PRTDEV,      2, "<devnum>"},
	{"PowerOn",       	POWON,       2, "<devnum>"},
	{"PowerOff",  		POWOFF,      2, "<devnum>"},
	{"Reboot",		REBOOT,      2, "<devnum?"},
	{"BottomMountLeft",     BOTMNTLEFT,  2, "<devnum>"},
	{"BottomMountRight",    BOTMNTRIGHT, 2, "<devnum>"},
	{"BottomMountMiddle",   BOTMNTMID,   2, "<devnum>"},
	{"TopMountUp",          TOPMNTLEFT,  2, "<devnum>"},
	{"TopMountDown",        TOPMNTRIGHT, 2, "<devnum>"},
	{"TopMountMiddle",      TOPMNTMID,   2, "<devnum>"},
	{"CtrlAction",    CTRLACTION,  2, "<devnum> <action (string)>"},
	{"CtrlGetVar",    CTRLGETVAR,  2, "<devnum> <varname (string)>"},
	{"Exit", EXITCMD, 1, ""}
};

void CCTvCtrlPointPrintCommands(void)
{
	int i;
	int numofcmds = (sizeof cmdloop_cmdlist) / sizeof (cmdloop_commands);

	SampleUtil_Print("Valid Commands:\n");
	for (i = 0; i < numofcmds; ++i) {
		SampleUtil_Print("  %-14s %s\n",
			cmdloop_cmdlist[i].str, cmdloop_cmdlist[i].args);
	}
	SampleUtil_Print("\n");
}

void *CCTvCtrlPointCommandLoop(void *args)
{
	char cmdline[100];

	while (1) {
		SampleUtil_Print("\n>> ");
		char *s = fgets(cmdline, 100, stdin);
		if (!s)
			break;
		CCTvCtrlPointProcessCommand(cmdline);
	}

	return NULL;
	args = args;
}

int CCTvCtrlPointProcessCommand(char *cmdline)
{
	char cmd[100];
	char strarg[100];
	int arg_val_err = -99999;
	int arg1 = arg_val_err;
	int arg2 = arg_val_err;
	int cmdnum = -1;
	int numofcmds = (sizeof cmdloop_cmdlist) / sizeof (cmdloop_commands);
	int cmdfound = 0;
	int i;
	int rc;
	int invalidargs = 0;
	int validargs;

	validargs = sscanf(cmdline, "%s %d %d", cmd, &arg1, &arg2);
	for (i = 0; i < numofcmds; ++i) {
		if (strcasecmp(cmd, cmdloop_cmdlist[i].str ) == 0) {
			cmdnum = cmdloop_cmdlist[i].cmdnum;
			cmdfound++;
			if (validargs != cmdloop_cmdlist[i].numargs)
				invalidargs++;
			break;
		}
	}
	if (!cmdfound) {
		SampleUtil_Print("Command not found; try 'Help'\n");
		return CCTV_SUCCESS;
	}
	if (invalidargs) {
		SampleUtil_Print("Invalid arguments; try 'Help'\n");
		return CCTV_SUCCESS;
	}
	switch (cmdnum) {
	case PRTHELP:
		CCTvCtrlPointPrintShortHelp();
		break;
	case PRTFULLHELP:
		CCTvCtrlPointPrintLongHelp();
		break;
	case POWON:
		CCTvCtrlPointSendPowerOn(arg1);
		break;
	case POWOFF:
		CCTvCtrlPointSendPowerOff(arg1);
		break;
	case REBOOT:
		CCTvCtrlPointSendReboot(arg1);
		break;
	case BOTMNTLEFT:
		CCTvCtrlPointSendBottomMountLeft(arg1);
		break;
	case BOTMNTRIGHT:
		CCTvCtrlPointSendBottomMountRight(arg1);
		break;
	case BOTMNTMID:
		CCTvCtrlPointSendBottomMountMiddle(arg1);
		break;
	case TOPMNTLEFT:
		CCTvCtrlPointSendTopMountUp(arg1);
		break;
	case TOPMNTRIGHT:
		CCTvCtrlPointSendTopMountDown(arg1);
		break;
	case TOPMNTMID:
		CCTvCtrlPointSendTopMountMiddle(arg1);
		break;
	case CTRLACTION:
		/* re-parse commandline since second arg is string. */
		validargs = sscanf(cmdline, "%s %d %s", cmd, &arg1, strarg);
		if (validargs == 3)
			CCTvCtrlPointSendAction(CCTV_SERVICE_CONTROL, arg1, strarg,
				NULL, NULL, 0);
		else
			invalidargs++;
		break;
	case CTRLGETVAR:
		/* re-parse commandline since second arg is string. */
		validargs = sscanf(cmdline, "%s %d %s", cmd, &arg1, strarg);
		if (validargs == 3)
			CCTvCtrlPointGetVar(CCTV_SERVICE_CONTROL, arg1, strarg);
		else
			invalidargs++;
		break;
	case PRTDEV:
		CCTvCtrlPointPrintDevice(arg1);
		break;
	case LSTDEV:
		CCTvCtrlPointPrintList();
		break;
	case REFRESH:
		CCTvCtrlPointRefresh();
		break;
	case EXITCMD:
		rc = CCTvCtrlPointStop();
		exit(rc);
		break;
	default:
		SampleUtil_Print("Command not implemented; see 'Help'\n");
		break;
	}
	if(invalidargs)
		SampleUtil_Print("Invalid args in command; see 'Help'\n");

	return CCTV_SUCCESS;
}

/*! @} Control Point Sample Module */

/*! @} UpnpSamples */
