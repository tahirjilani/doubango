/*
* Copyright (C) 2009 Mamadou Diop.
*
* Contact: Mamadou Diop <diopmamadou@yahoo.fr>
*	
* This file is part of Open Source Doubango Framework.
*
* DOUBANGO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*	
* DOUBANGO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*	
* You should have received a copy of the GNU General Public License
* along with DOUBANGO.
*
*/

/**@file tsip_dialog_register.client.c
 * @brief SIP dialog register (Client side).
 *
 * @author Mamadou Diop <diopmamadou(at)yahoo.fr>
 *
 * @date Created: Sat Nov 8 16:54:58 2009 mdiop
 */
#include "tinysip/dialogs/tsip_dialog_register.h"
#include "tinysip/parsers/tsip_parser_uri.h"

#include "tinysip/headers/tsip_header_Path.h"
#include "tinysip/headers/tsip_header_P_Associated_URI.h"
#include "tinysip/headers/tsip_header_Min_Expires.h"
#include "tinysip/headers/tsip_header_Service_Route.h"

#include "tinysip/api/tsip_register.h"

#include "tsk_memory.h"
#include "tsk_debug.h"
#include "tsk_time.h"

#define DEBUG_STATE_MACHINE											1
#define TSIP_DIALOG_REGISTER_TIMER_SCHEDULE(TX)						TSIP_DIALOG_TIMER_SCHEDULE(register, TX)
#define TSIP_DIALOG_REGISTER_SIGNAL_ERROR(self)									\
	TSIP_DIALOG_SYNC_BEGIN(self);												\
	tsip_dialog_registerContext_sm_error(&TSIP_DIALOG_REGISTER(self)->_fsm);	\
	TSIP_DIALOG_SYNC_END(self);
#define TSIP_DIALOG_REGISTER_SIGNAL(self, type, code, phrase, incoming)	\
	tsip_register_event_signal(type, TSIP_DIALOG_GET_STACK(self),/*tsip_operation_get_id(TSIP_DIALOG(self)->operation)*/0, code, phrase, incoming)
#define TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, type, code, phrase) TSIP_DIALOG_REGISTER_SIGNAL(self, type, code, phrase, 1)
#define TSIP_DIALOG_REGISTER_SIGNAL_OUTGOING(self, type, code, phrase) TSIP_DIALOG_REGISTER_SIGNAL(self, type, code, phrase, 0)

int send_register(tsip_dialog_register_t *self);

/**
 * @fn	int tsip_dialog_register_event_callback(const tsip_dialog_register_t *self, tsip_dialog_event_type_t type,
 * 		const tsip_message_t *msg)
 *
 * @brief	Callback function called to alert the dialog for new events from the transaction/transport layers.
 *
 * @author	Mamadou
 * @date	1/4/2010
 *
 * @param [in,out]	self	A reference to the dialog.
 * @param	type		The event type. 
 * @param [in,out]	msg	The incoming SIP/IMS message. 
 *
 * @return	Zero if succeed and non-zero error code otherwise. 
**/
int tsip_dialog_register_event_callback(const tsip_dialog_register_t *self, tsip_dialog_event_type_t type, const tsip_message_t *msg)
{
	TSIP_DIALOG_SYNC_BEGIN(self);

	switch(type)
	{
	case tsip_dialog_msg:
		{
			if(msg && TSIP_MESSAGE_IS_RESPONSE(msg))
			{
				//
				//	RESPONSE
				//
				if(TSIP_RESPONSE_IS_1XX(msg))
				{
					tsip_dialog_registerContext_sm_1xx(&TSIP_DIALOG_REGISTER(self)->_fsm, msg);
				}
				else if(TSIP_RESPONSE_IS_2XX(msg))
				{
					tsip_dialog_registerContext_sm_2xx(&TSIP_DIALOG_REGISTER(self)->_fsm, TSIP_DIALOG_REGISTER(self)->unregistering, msg);
				}
				else if(TSIP_RESPONSE_IS(msg,401) || TSIP_RESPONSE_IS(msg,407) || TSIP_RESPONSE_IS(msg,421) || TSIP_RESPONSE_IS(msg,494))
				{
					tsip_dialog_registerContext_sm_401_407_421_494(&TSIP_DIALOG_REGISTER(self)->_fsm, msg);
				}
				else if(TSIP_RESPONSE_IS(msg,423))
				{
					tsip_dialog_registerContext_sm_423(&TSIP_DIALOG_REGISTER(self)->_fsm, msg);
				}
				else
				{
					// Alert User
					TSIP_DIALOG_REGISTER_SIGNAL_ERROR(self);
					TSK_DEBUG_WARN("Not supported status code: %d", TSIP_RESPONSE_CODE(msg));
				}
			}
			else
			{
				//
				//	REQUEST
				//
			}
			break;
		}

	case tsip_dialog_hang_up:
		{
			tsip_dialog_registerContext_sm_hangup(&TSIP_DIALOG_REGISTER(self)->_fsm);
			break;
		}

	case tsip_dialog_canceled:
		{
			tsip_dialog_registerContext_sm_cancel(&TSIP_DIALOG_REGISTER(self)->_fsm);
			break;
		}

	case tsip_dialog_terminated:
	case tsip_dialog_timedout:
	case tsip_dialog_error:
	case tsip_dialog_transport_error:
		{
			tsip_dialog_registerContext_sm_transportError(&TSIP_DIALOG_REGISTER(self)->_fsm);
			break;
		}
	}

	TSIP_DIALOG_SYNC_END(self);

	return 0;
}

/**
 * @fn	int tsip_dialog_register_timer_callback(const tsip_dialog_register_t* self,
 * 		tsk_timer_id_t timer_id)
 *
 * @brief	Timer manager callback.
 *
 * @author	Mamadou
 * @date	1/5/2010
 *
 * @param [in,out]	self	The owner of the signaled timer. 
 * @param	timer_id		The identifier of the signaled timer.
 *
 * @return	Zero if succeed and non-zero error code otherwise.  
**/
int tsip_dialog_register_timer_callback(const tsip_dialog_register_t* self, tsk_timer_id_t timer_id)
{
	int ret = -1;

	if(self)
	{
		TSIP_DIALOG_SYNC_BEGIN(self);

		if(timer_id == self->timerrefresh.id)
		{
			tsip_dialog_registerContext_sm_refresh(&TSIP_DIALOG_REGISTER(self)->_fsm);
			ret = 0;
		}

		TSIP_DIALOG_SYNC_END(self);
	}
	return ret;
}

/**
 * @fn	void tsip_dialog_register_init(tsip_dialog_register_t *self)
 *
 * @brief	Initializes the dialog.
 *
 * @author	Mamadou
 * @date	1/4/2010
 *
 * @param [in,out]	self	The dialog to initialize. 
**/
void tsip_dialog_register_init(tsip_dialog_register_t *self)
{
	/* Initialize the state machine.
	*/
	tsip_dialog_registerContext_Init(&self->_fsm, self);

	TSIP_DIALOG(self)->callback = TSIP_DIALOG_EVENT_CALLBACK(tsip_dialog_register_event_callback);
	
	TSIP_DIALOG(self)->uri_local = tsk_object_ref((void*)TSIP_DIALOG_GET_STACK(self)->public_identity);
	TSIP_DIALOG(self)->uri_remote = tsk_object_ref((void*)TSIP_DIALOG_GET_STACK(self)->public_identity);
	TSIP_DIALOG(self)->uri_remote_target = tsk_object_ref((void*)TSIP_DIALOG_GET_STACK(self)->realm);

	self->timerrefresh.id = TSK_INVALID_TIMER_ID;
	self->timerrefresh.timeout = TSIP_DIALOG(self)->expires;

#if defined(_DEBUG) || defined(DEBUG)
	 setDebugFlag(&(self->_fsm), DEBUG_STATE_MACHINE);
#endif
}

/**
 * @fn	int tsip_dialog_register_start(tsip_dialog_register_t *self)
 *
 * @brief	Starts the dialog state machine.
 *
 * @author	Mamadou
 * @date	1/4/2010
 *
 * @param [in,out]	self	The dialog to start. 
 *
 * @return	Zero if succeed and non-zero error code otherwise. 
**/
int tsip_dialog_register_start(tsip_dialog_register_t *self)
{
	int ret = -1;
	if(self && !TSIP_DIALOG(self)->running)
	{
		/* Set state machine state to started */
		setState(&self->_fsm, &tsip_dialog_register_Started);

		/* Send request */
		tsip_dialog_registerContext_sm_send(&self->_fsm);
		ret = 0;
	}
	return ret;
}


//--------------------------------------------------------
//				== STATE MACHINE BEGIN ==
//--------------------------------------------------------

/* Started -> (send) -> Trying
*/
void tsip_dialog_register_Started_2_Trying_X_send(tsip_dialog_register_t *self)
{
	send_register(self);
}

/* Trying -> (1xx) -> Trying
*/
void tsip_dialog_register_Trying_2_Trying_X_1xx(tsip_dialog_register_t *self, const tsip_message_t* msg)
{
	tsip_dialog_update(TSIP_DIALOG(self), msg);
}

/* Trying -> (2xx) -> Connected
*/
//#include "tsk_thread.h"
void tsip_dialog_register_Trying_2_Connected_X_2xx(tsip_dialog_register_t *self, const tsip_message_t* msg)
{
	/* Alert the user. */
	TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, tsip_register_ok, 
		TSIP_RESPONSE_CODE(msg), TSIP_RESPONSE_PHRASE(msg));

	/*	- Set P-associated-uriS
	*	- Update service-routes
	*	- Update Pats
	*/
	if(TSIP_DIALOG(self)->state != tsip_established) /* Must be called before "tsip_dialog_update" update the state. */
	{
		size_t index;
		const tsip_header_Path_t *hdr_Path;
		const tsip_header_Service_Route_t *hdr_Service_Route;
		const tsip_header_P_Associated_URI_t *hdr_P_Associated_URI_t;
		tsip_uri_t *uri;

		/* To avoid memory leaks ==> delete all concerned objects (it worth nothing) */
		TSK_OBJECT_SAFE_FREE(TSIP_DIALOG_GET_STACK(self)->associated_uris);
		TSK_OBJECT_SAFE_FREE(TSIP_DIALOG_GET_STACK(self)->service_routes);
		TSK_OBJECT_SAFE_FREE(TSIP_DIALOG_GET_STACK(self)->paths);

		/* Associated URIs */
		for(index = 0; (hdr_P_Associated_URI_t = (const tsip_header_P_Associated_URI_t*)tsip_message_get_headerAt(msg, tsip_htype_P_Associated_URI, index)); index++){
			if(!TSIP_DIALOG_GET_STACK(self)->associated_uris){
				TSIP_DIALOG_GET_STACK(self)->associated_uris = TSK_LIST_CREATE();
			}
			uri = tsk_object_ref(hdr_P_Associated_URI_t->uri);
			tsk_list_push_back_data(TSIP_DIALOG_GET_STACK(self)->associated_uris, (void**)&uri);
		}

		/* Service-Route */
		for(index = 0; (hdr_Service_Route = (const tsip_header_Service_Route_t*)tsip_message_get_headerAt(msg, tsip_htype_Service_Route, index)); index++){
			if(!TSIP_DIALOG_GET_STACK(self)->service_routes){
				TSIP_DIALOG_GET_STACK(self)->service_routes = TSK_LIST_CREATE();
			}
			uri = tsk_object_ref(hdr_Service_Route->uri);
			tsk_list_push_back_data(TSIP_DIALOG_GET_STACK(self)->service_routes, (void**)&uri);
		}

		/* Paths */
		for(index = 0; (hdr_Path = (const tsip_header_Path_t*)tsip_message_get_headerAt(msg, tsip_htype_Path, index)); index++){
			if(TSIP_DIALOG_GET_STACK(self)->paths == 0){
				TSIP_DIALOG_GET_STACK(self)->paths = TSK_LIST_CREATE();
			}
			uri = tsk_object_ref(hdr_Path->uri);
			tsk_list_push_back_data(TSIP_DIALOG_GET_STACK(self)->paths, (void**)&uri);
		}
	}

	/* Update the dialog state. */
	tsip_dialog_update(TSIP_DIALOG(self), msg);

	/* Request timeout for dialog refresh (re-registration). */
	self->timerrefresh.timeout = tsip_dialog_get_newdelay(TSIP_DIALOG(self), msg);
	TSIP_DIALOG_REGISTER_TIMER_SCHEDULE(refresh);
}

/* Trying -> (2xx) -> Terminated
*/
void tsip_dialog_register_Trying_2_Terminated_X_2xx(tsip_dialog_register_t *self, const tsip_message_t* msg)
{
	/* Alert the user. */
	TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, tsip_unregister_ok, TSIP_RESPONSE_CODE(msg), TSIP_RESPONSE_PHRASE(msg));
}

/*	Trying --> (401/407/421/494) --> Trying
*/
void tsip_dialog_register_Trying_2_Trying_X_401_407_421_494(tsip_dialog_register_t *self, const tsip_message_t* msg)
{
	if(tsip_dialog_update(TSIP_DIALOG(self), msg))
	{
		TSIP_DIALOG_REGISTER_SIGNAL_ERROR(self);

		/* Alert the user. */
		TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, self->unregistering ? tsip_unregistering : tsip_registering,
			TSIP_RESPONSE_CODE(msg), TSIP_RESPONSE_PHRASE(msg));
		
		goto bail;
	}
	
	send_register(self);

bail:;
}

/*	Trying -> (423) -> Trying
*/
void tsip_dialog_register_Trying_2_Trying_X_423(tsip_dialog_register_t *self, const tsip_message_t* msg)
{
	tsip_header_Min_Expires_t *hdr;

	/*
	RFC 3261 - 10.2.8 Error Responses

	If a UA receives a 423 (Interval Too Brief) response, it MAY retry
	the registration after making the expiration interval of all contact
	addresses in the REGISTER request equal to or greater than the
	expiration interval within the Min-Expires header field of the 423
	(Interval Too Brief) response.
	*/
	hdr = (tsip_header_Min_Expires_t*)tsip_message_get_header(msg, tsip_htype_Min_Expires);
	if(hdr)
	{
		TSIP_DIALOG(self)->expires = hdr->value;

		send_register(self);
	}
	else
	{
		TSIP_DIALOG_REGISTER_SIGNAL_ERROR(self);

		TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, self->unregistering ? tsip_unregistering : tsip_registering,
			TSIP_RESPONSE_CODE(msg), TSIP_RESPONSE_PHRASE(msg));
	}
}

/* Trying -> (300-699) -> Terminated
*/
void tsip_dialog_register_Trying_2_Terminated_X_300_to_699(tsip_dialog_register_t *self, const tsip_message_t* msg)
{
	/* Alert the user. */
	TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, tsip_register_nok, TSIP_RESPONSE_CODE(msg), TSIP_RESPONSE_PHRASE(msg));
}

/* Trying -> (cancel) -> Terminated
*/
void tsip_dialog_register_Trying_2_Terminated_X_cancel(tsip_dialog_register_t *self)
{
	/* Alert the user. */
	TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, tsip_register_cancelled, 701, "Registration cancelled");
}

/* Connected -> (Unregister) -> Trying
*/
void tsip_dialog_register_Connected_2_Trying_X_unregister(tsip_dialog_register_t *self)
{
}

/* Connected -> (refresh) -> Trying
*/
void tsip_dialog_register_Connected_2_Trying_X_refresh(tsip_dialog_register_t *self)
{
	send_register(self);
}

/* Any -> (hangup) -> Trying
*/
void tsip_dialog_register_Any_2_Trying_X_hangup(tsip_dialog_register_t *self)
{
	self->unregistering = 1;
	send_register(self);
}

/* Any -> (transport error) -> Terminated
*/
void tsip_dialog_register_Any_2_Terminated_X_transportError(tsip_dialog_register_t *self)
{
	TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, tsip_register_transporterr, 702, "Transport error.");
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//				== STATE MACHINE END ==
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++


/**
 * @fn	int send_register(tsip_dialog_register_t *self)
 *
 * @brief	Sends a REGISTER request. 
 *
 * @author	Mamadou
 * @date	1/4/2010
 *
 * @param [in,out]	self	The caller.
 *
 * @return	Zero if succeed and non-zero error code otherwise. 
**/
int send_register(tsip_dialog_register_t *self)
{
	tsip_request_t *request;
	int ret = -1;
	
	if(self->unregistering)
	{
		TSIP_DIALOG(self)->expires = 0;
	}
	
	request = tsip_dialog_request_new(TSIP_DIALOG(self), "REGISTER");

	if(request)
	{
		/* == RCS phase 2
		*/
		if(TSIP_DIALOG_GET_STACK(self)->enable_gsmarcs)
		{
			TSIP_HEADER_ADD_PARAM(request->Contact, "+g.oma.sip-im.large-message", 0);
			TSIP_HEADER_ADD_PARAM(request->Contact, "audio", 0);
			TSIP_HEADER_ADD_PARAM(request->Contact, "video", 0);
			TSIP_HEADER_ADD_PARAM(request->Contact, "+g.3gpp.cs-voice", 0);
			TSIP_HEADER_ADD_PARAM(request->Contact, "+g.3gpp.icsi-ref", TSIP_ICSI_QUOTED_MMTEL_PSVOICE);
		}

		/* mobility */
		if(TSIP_DIALOG_GET_STACK(self)->mobility)
		{
			TSIP_HEADER_ADD_PARAM(request->Contact, "mobility", TSIP_DIALOG_GET_STACK(self)->mobility);
		}

		/* deviceID - FIXME: find reference. */
		if(TSIP_DIALOG_GET_STACK(self)->device_id)
		{
			TSIP_HEADER_ADD_PARAM(request->Contact, "+deviceID", TSIP_DIALOG_GET_STACK(self)->device_id);
		}

		/* GSMA Image Sharing */
		if(TSIP_DIALOG_GET_STACK(self)->enable_gsmais)
		{
			TSIP_HEADER_ADD_PARAM(request->Contact, "+g.3gpp.app_ref", TSIP_IARI_QUOTED_GSMAIS);
		}

		/* 3GPP TS 24.341 subclause 5.3.2.2 */
		if(TSIP_DIALOG_GET_STACK(self)->enable_3gppsms)
		{
			TSIP_HEADER_ADD_PARAM(request->Contact, "+g.3gpp.smsip", 0);
		}		
		
		ret = tsip_dialog_request_send(TSIP_DIALOG(self), request);
		TSK_OBJECT_SAFE_FREE(request);
	}

	return ret;
}


/**
 * @fn	void tsip_dialog_register_OnTerminated(tsip_dialog_register_t *self)
 *
 * @brief	Callback function called by the state machine manager to signal that the final state has been reached.
 *
 * @author	Mamadou
 * @date	1/5/2010
 *
 * @param [in,out]	self	The state machine owner.
**/
void tsip_dialog_register_OnTerminated(tsip_dialog_register_t *self)
{
	TSK_DEBUG_INFO("=== Dialog terminated ===");

	/* Cancel all timers */
	DIALOG_TIMER_CANCEL(refresh);

	/* Alert user */
	TSIP_DIALOG_REGISTER_SIGNAL_INCOMING(self, tsip_register_terminated, 700, "Dialog terminated.");

	/* Destroy asynchronously */
	DIALOG_REMOVE_SCHEDULE();
}














//========================================================
//	SIP dialog REGISTER object definition
//
static void* tsip_dialog_register_create(void * self, va_list * app)
{
	tsip_dialog_register_t *dialog = self;
	if(dialog)
	{
		tsip_stack_handle_t *stack = va_arg(*app, tsip_stack_handle_t *);
		tsip_operation_handle_t *operation = va_arg(*app, tsip_operation_handle_t *);

		/* Initialize base class */
		tsip_dialog_init(TSIP_DIALOG(self), tsip_dialog_register, stack, 0, operation);

		/* Initialize the class itself */
		tsip_dialog_register_init(self);
	}
	return self;
}

static void* tsip_dialog_register_destroy(void * self)
{ 
	tsip_dialog_register_t *dialog = self;
	if(dialog)
	{
		/* DeInitialize base class */
		tsip_dialog_deinit(TSIP_DIALOG(self));
	}
	return self;
}

static int tsip_dialog_register_cmp(const void *dialog1, const void *dialog2)
{
	return tsip_dialog_cmp(dialog1, dialog2);
}

static const tsk_object_def_t tsip_dialog_register_def_s = 
{
	sizeof(tsip_dialog_register_t),
	tsip_dialog_register_create, 
	tsip_dialog_register_destroy,
	tsip_dialog_register_cmp, 
};
const void *tsip_dialog_register_def_t = &tsip_dialog_register_def_s;
