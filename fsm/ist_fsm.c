/*
  The oSIP library implements the Session Initiation Protocol (SIP -rfc2543-)
  Copyright (C) 2001  Aymeric MOIZARD jack@atosc.org
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
  
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <osip/port.h>
#include <osip/osip.h>

#include "fsm.h"

static statemachine_t *ist_fsm;

statemachine_t*
ist_get_fsm()
{
  return ist_fsm;
}

void ist_unload_fsm()
{
  transition_t *transition;
  statemachine_t *statemachine = ist_get_fsm();
  while (!list_eol(statemachine->transitions,0))
    {
      transition = (transition_t *)list_get(statemachine->transitions,0);
      list_remove(statemachine->transitions,0);
      sfree(transition);
    }
  sfree(statemachine->transitions);
  sfree(statemachine);
}


void
ist_load_fsm()
{
  transition_t *transition;

  ist_fsm       = (statemachine_t *) smalloc(sizeof(statemachine_t));
  ist_fsm->transitions = (list_t *)  smalloc(sizeof(list_t));
  list_init(ist_fsm->transitions);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_PRE_PROCEEDING;
  transition->type   = RCV_REQINVITE;
  transition->method = (void(*)(void *,void *))&ist_rcv_invite;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_PROCEEDING;
  transition->type   = RCV_REQINVITE;
  transition->method = (void(*)(void *,void *))&ist_rcv_invite;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_COMPLETED;
  transition->type   = RCV_REQINVITE;
  transition->method = (void(*)(void *,void *))&ist_rcv_invite;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_COMPLETED;
  transition->type   = TIMEOUT_G;
  transition->method = (void(*)(void *,void *))&ist_timeout_g_event;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_COMPLETED;
  transition->type   = TIMEOUT_H;
  transition->method = (void(*)(void *,void *))&ist_timeout_h_event;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_PROCEEDING;
  transition->type   = SND_STATUS_1XX;
  transition->method = (void(*)(void *,void *))&ist_snd_1xx;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_PROCEEDING;
  transition->type   = SND_STATUS_2XX;
  transition->method = (void(*)(void *,void *))&ist_snd_2xx;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_PROCEEDING;
  transition->type   = SND_STATUS_3456XX;
  transition->method = (void(*)(void *,void *))&ist_snd_3456xx;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_COMPLETED;
  transition->type   = RCV_REQACK;
  transition->method = (void(*)(void *,void *))&ist_rcv_ack;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_CONFIRMED;
  transition->type   = RCV_REQACK;
  transition->method = (void(*)(void *,void *))&ist_rcv_ack;
  list_add(ist_fsm->transitions,transition,-1);

  transition         = (transition_t *) smalloc(sizeof(transition_t));
  transition->state  = IST_CONFIRMED;
  transition->type   = TIMEOUT_I;
  transition->method = (void(*)(void *,void *))&ist_timeout_i_event;
  list_add(ist_fsm->transitions,transition,-1);

}

sip_t *
ist_create_resp_100(transaction_t *ist, sip_t *request)
{
  int i;
  sip_t *resp_100;
  i = msg_init(&resp_100);
  if (i!=0) return NULL;

  /* follow instructions from 8.2.6 */
  i = from_clone(request->from, &(resp_100->from));
  if (i!=0) goto icr_error;
  /* 17.2.1 says: should NOT add a tag */
  i = to_clone(request->to, &(resp_100->to));  /* DOES NOT include any tag! */
  if (i!=0) goto icr_error;
  i = call_id_clone(request->call_id, &(resp_100->call_id));
  if (i!=0) goto icr_error;
  i = cseq_clone(request->cseq, &(resp_100->cseq));
  if (i!=0) goto icr_error;
  
  /* Via headers are copied from request */
  {
    int pos=0;
    via_t *via;
    via_t *orig_via;
    while (!list_eol(ist->orig_request->vias, pos))
      {
	orig_via = (via_t*)list_get(ist->orig_request->vias, pos);
	via_clone(orig_via, &via);
	list_add(resp_100->vias, via, -1);
	pos++;
      }
  }

  /* TODO: */
  /* MUST copy the "Timestamp" header here (+ add a delay if necessary!)        */
  /* a delay should not be necessary for 100 as it is sent in less than one sec */


  return resp_100;
 icr_error:
  msg_free(resp_100);
  sfree(resp_100);
  return NULL;
}

void
ist_rcv_invite(transaction_t *ist, sipevent_t *evt)
{
  int i;
  osip_t *osip = (osip_t*)ist->config;

  if (ist->state==IST_PRE_PROCEEDING) /* announce new INVITE */
    osip->cb_ist_invite_received(ist, evt->sip);
  else /* IST_PROCEEDING or IST_COMPLETED */
    {
      /* delete retransmission */
      msg_free(evt->sip);
      sfree(evt->sip);

      if (osip->cb_ist_invite_received2!=NULL)
	osip->cb_ist_invite_received2(ist, ist->orig_request);
      if (ist->last_response!=NULL) /* retransmit last response */
	{
	  via_t *via;
	  via = (via_t*)list_get(ist->last_response->vias, 0);
	  {
	    int port;
	    if (via->port==NULL) port=5060;
	    else port = atoi(via->port); /* we should use strtol to handle errors */
	    i = osip->cb_send_message(ist, ist->last_response, via->host,
				      port, ist->out_socket);
	  }
	  if (i!=0)
	    {
	      osip->cb_ist_transport_error(ist, i);
	      transaction_set_state(ist, IST_TERMINATED);
	      osip->cb_ist_kill_transaction(ist);
	      /* MUST BE DELETED NOW */
	      return;
	    }
	  else
	    osip->cb_ist_1xx_sent(ist, ist->last_response);	    
	}
      /* we are already in the proper state */
      return;
    }
  /* This is not to be used by the TRANSACTION LAYER??
     if (ist->auto_send_100==0)
     {
     sip_t *resp_100;
     resp_100 = create_resp_100(ist, evt->sip);
     if (resp_100!=NULL)
     {
     via_t *via;
     ist->last_response=resp_100;
     via = (via_t*)list_get(resp_100->vias, 0);
     {
     int port;
     if (via->port==NULL) port=5060;
     else port = atoi(via->port);
     i = osip->cb_send_message(ist, resp_100, via->host,
     port, ist->out_socket);
     }
     if (i!=0)
     {
     osip->cb_ist_transport_error(ist, i);
     transaction_set_state(ist, IST_TERMINATED);
     osip->cb_ist_kill_transaction(ist);
     return;
     }
     }
     else {
     transaction_set_state(ist, IST_TERMINATED);
     osip->cb_ist_kill_transaction(ist);
     return;
     }
     }
  */
  /* we come here only if it was the first INVITE received */
  transaction_set_state(ist, IST_PROCEEDING);
}

void
ist_timeout_g_event(transaction_t *ist, sipevent_t *evt)
{
  via_t *via;
  osip_t *osip = (osip_t*)ist->config;
  time_t now = time(NULL);
  int i;

  ist->ist_context->timer_g_length = ist->ist_context->timer_g_length*2;
  if (ist->ist_context->timer_g_length>4000)
    ist->ist_context->timer_g_length = 4000;
  ist->ist_context->timer_g_start = now;

  /* retransmit RESPONSE */
  via = (via_t*)list_get(ist->last_response->vias, 0);
  {
    int port;
    if (via->port==NULL) port=5060;
    else port = atoi(via->port); /* we should use strtol to handle errors */
    i = osip->cb_send_message(ist, ist->last_response, via->host,
			      port, ist->out_socket);
  }
  if (i!=0)
    {
      osip->cb_ist_transport_error(ist, i);
      transaction_set_state(ist, IST_TERMINATED);
      osip->cb_ist_kill_transaction(ist);
      /* TODO: MUST BE DELETED NOW */
      return;
    }
  if (osip->cb_ist_3456xx_sent2!=NULL)
    osip->cb_ist_3456xx_sent2(ist, ist->last_response);
}

void
ist_timeout_h_event(transaction_t *ist, sipevent_t *evt)
{
  osip_t *osip = (osip_t*)ist->config;

  ist->ist_context->timer_h_length = -1;
  ist->ist_context->timer_h_start = -1;

  transaction_set_state(ist, IST_TERMINATED);
  osip->cb_ist_kill_transaction(ist);  
}

void
ist_timeout_i_event(transaction_t *ist, sipevent_t *evt)
{
  osip_t *osip = (osip_t*)ist->config;

  ist->ist_context->timer_i_length = -1;
  ist->ist_context->timer_i_start = -1;

  transaction_set_state(ist, IST_TERMINATED);
  osip->cb_ist_kill_transaction(ist);  
}

void
ist_snd_1xx(transaction_t *ist, sipevent_t *evt)
{
  int i;
  via_t *via;
  osip_t *osip = (osip_t*)ist->config;

  if (ist->last_response!=NULL)
    {
      msg_free(ist->last_response);
      sfree(ist->last_response);
    }
  ist->last_response = evt->sip;

  via = (via_t*)list_get(ist->last_response->vias, 0);
  {
    int port;
    if (via->port==NULL) port=5060;
    else port = atoi(via->port); /* we should use strtol to handle errors */
    i = osip->cb_send_message(ist, ist->last_response, via->host,
			      port, ist->out_socket);
  }
  if (i!=0)
    {
      osip->cb_ist_transport_error(ist, i);
      transaction_set_state(ist, IST_TERMINATED);
      osip->cb_ist_kill_transaction(ist);
      /* MUST BE DELETED NOW */
      return;
    }
  else
    osip->cb_ist_1xx_sent(ist, ist->last_response);
  
  /* we are already in the proper state */
  return;
}

void
ist_snd_2xx(transaction_t *ist, sipevent_t *evt)
{
  int i;
  via_t *via;
  osip_t *osip = (osip_t*)ist->config;

  if (ist->last_response!=NULL)
    {
      msg_free(ist->last_response);
      sfree(ist->last_response);
    }
  ist->last_response = evt->sip;

  via = (via_t*)list_get(ist->last_response->vias, 0);
  {
    int port;
    if (via->port==NULL) port=5060;
    else port = atoi(via->port); /* we should use strtol to handle errors */
    i = osip->cb_send_message(ist, ist->last_response, via->host,
			      port, ist->out_socket);
  }
  if (i!=0)
    {
      osip->cb_ist_transport_error(ist, i);
      transaction_set_state(ist, IST_TERMINATED);
      osip->cb_ist_kill_transaction(ist);
      /* MUST BE DELETED NOW */
      return;
    }
  else
    osip->cb_ist_2xx_sent(ist, ist->last_response);

  transaction_set_state(ist, IST_TERMINATED);
  osip->cb_ist_kill_transaction(ist);
  return;
}

void
ist_snd_3456xx(transaction_t *ist, sipevent_t *evt)
{
  int i;
  via_t *via;
  osip_t *osip = (osip_t*)ist->config;

  if (ist->last_response!=NULL)
    {
      msg_free(ist->last_response);
      sfree(ist->last_response);
    }
  ist->last_response = evt->sip;

  via = (via_t*)list_get(ist->last_response->vias, 0);
  {
    int port;
    if (via->port==NULL) port=5060;
    else port = atoi(via->port); /* we should use strtol to handle errors */
    i = osip->cb_send_message(ist, ist->last_response, via->host,
			      port, ist->out_socket);
  }
  if (i!=0)
    {
      osip->cb_ist_transport_error(ist, i);
      transaction_set_state(ist, IST_TERMINATED);
      osip->cb_ist_kill_transaction(ist);
      /* MUST BE DELETED NOW */
      return;
    }
  else
    {
      if (MSG_IS_STATUS_3XX(ist->last_response))
	osip->cb_ist_3xx_sent(ist, ist->last_response);
      else if (MSG_IS_STATUS_4XX(ist->last_response))
	osip->cb_ist_4xx_sent(ist, ist->last_response);
      else if (MSG_IS_STATUS_5XX(ist->last_response))
	osip->cb_ist_5xx_sent(ist, ist->last_response);
      else
	osip->cb_ist_6xx_sent(ist, ist->last_response);
    }

  ist->ist_context->timer_g_start = time(NULL);
  transaction_set_state(ist, IST_COMPLETED);
  return;
}

void
ist_rcv_ack(transaction_t *ist, sipevent_t *evt)
{
  osip_t *osip = (osip_t*)ist->config;

  if (ist->ack!=NULL)
    {
      msg_free(ist->ack);
      sfree(ist->ack);
    }

  ist->ack = evt->sip;

  if (ist->state==IST_COMPLETED)
    osip->cb_ist_ack_received(ist, ist->ack);
  else /* IST_CONFIRMED */
    { /* ack retransmission */
      if (osip->cb_ist_ack_received2!=NULL)
	osip->cb_ist_ack_received2(ist, ist->ack);
    }
  /* set the timer to 0 for reliable, and T4 for unreliable (already set) */
  ist->ist_context->timer_i_start = time(NULL); /* not started */
  transaction_set_state(ist, IST_CONFIRMED);
}
