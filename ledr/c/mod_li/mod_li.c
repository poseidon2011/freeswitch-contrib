/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2009, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 * Leon de Rooij <leon@toyos.nl>
 *
 *
 * mod_li.c -- Legal Intercept
 *
 * THIS MODULE IS STILL UNDER CONSTRUCTION !!!!!!!!!!!!!!!!!!!
 *
 * Enable Legal Intercept, copy all frame data, prepend it with
 * LI_ID, SEQ, TIMESTAMP, DIRECTION, CIN (can I use call-uuid for this?) - perhaps ETSI232 compliant would be nice ?
 * and send it over UDP to remote_addr:remote_port
 *
 */
#include <switch.h>
#include <netdb.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_li_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_li_shutdown);
SWITCH_MODULE_DEFINITION(mod_li, mod_li_load, mod_li_shutdown, NULL);

SWITCH_STANDARD_APP(li_start_function);

static struct {
	switch_memory_pool_t *pool;
	char *local_addr;
	int local_port;
	char *remote_addr;
	int remote_port;
	switch_socket_t *socket;
} li_globals;

typedef struct {
	/*! Internal FreeSWITCH session. */
	switch_core_session_t *session;
	char *li_id; /* legal intercept id */
} li_session_helper_t;

/* config item validations */
static switch_xml_config_string_options_t config_opt_valid_addr = { NULL, 0, ".+" };
static switch_xml_config_int_options_t config_opt_valid_port = { SWITCH_TRUE, 0, SWITCH_TRUE, 65535 };

/* config items */
static switch_xml_config_item_t instructions[] = {
	SWITCH_CONFIG_ITEM("local_addr", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &li_globals.local_addr,
		"localhost", &config_opt_valid_addr, NULL, NULL),
	SWITCH_CONFIG_ITEM("local_port", SWITCH_CONFIG_INT, CONFIG_RELOADABLE, &li_globals.local_port,
		(void*)10741, &config_opt_valid_port, NULL, NULL),
	SWITCH_CONFIG_ITEM("remote_addr", SWITCH_CONFIG_STRING, CONFIG_RELOADABLE, &li_globals.remote_addr,
		"localhost", &config_opt_valid_addr, NULL, NULL),
	SWITCH_CONFIG_ITEM("remote_port", SWITCH_CONFIG_INT, CONFIG_RELOADABLE, &li_globals.remote_port,
		(void*)10741, &config_opt_valid_port, NULL, NULL),
  SWITCH_CONFIG_ITEM_END()
};

static switch_status_t do_config(switch_bool_t reload)
{
	switch_socket_t *socket = NULL;
	switch_sockaddr_t *local_sockaddr;
	switch_sockaddr_t *remote_sockaddr;

	if (switch_xml_config_parse_module_settings("li.conf", reload, instructions) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Could not open li.conf\n");
		return SWITCH_STATUS_FALSE;
	}

	/* for now, only init port on startup, fix reload later !! */
	if (!reload) {

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Trying to create a udp socket !!\n");

		if (switch_sockaddr_info_get(&local_sockaddr, li_globals.local_addr, SWITCH_UNSPEC, li_globals.local_port, 0, li_globals.pool) != SWITCH_STATUS_SUCCESS || !local_sockaddr) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Local Address Error!\n");
			return SWITCH_STATUS_FALSE;
		}

		if (switch_socket_create(&socket, switch_sockaddr_get_family(local_sockaddr), SOCK_DGRAM, 0, li_globals.pool) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Socket Error!\n");
			return SWITCH_STATUS_FALSE;
		}

		if (switch_socket_opt_set(socket, SWITCH_SO_REUSEADDR, 1) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Socket Error!\n");
			return SWITCH_STATUS_FALSE;
		}

		if (switch_socket_bind(socket, local_sockaddr) != SWITCH_STATUS_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Bind Error!\n");
			return SWITCH_STATUS_FALSE;
		}

		/* HAVE A LOOK AT switch_rtp.c LINE 746 and 796 FOR WIN32 COMPATIBILITY ! */

		li_globals.socket = socket;

		/* TEST SENDING A PACKET */
		if (switch_sockaddr_info_get(&remote_sockaddr, li_globals.remote_addr, SWITCH_UNSPEC, li_globals.remote_port, 0, li_globals.pool) != SWITCH_STATUS_SUCCESS || !remote_sockaddr) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Remote Address Error!\n");
			return SWITCH_STATUS_FALSE;
		}

		switch_size_t len = sizeof("X");
		switch_socket_sendto(li_globals.socket, remote_sockaddr, 0, "X", &len);

	}

	return SWITCH_STATUS_SUCCESS;
}

/* called when SWITCH_EVENT_RELOADXML is sent to this module */
static void reload_event_handler(switch_event_t *event)
{
  do_config(SWITCH_TRUE);
}

/* necessary for hooking to SWITCH_EVENT_RELOADXML */
static switch_event_node_t *NODE = NULL;

static switch_bool_t li_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
//	li_session_helper_t *li_session_helper = (li_session_helper_t *) user_data;
	switch_size_t len;

	switch_sockaddr_t *remote_sockaddr;

//	switch_buffer_t *buffer = (switch_buffer_t *) user_data;
	uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
	switch_frame_t frame = { 0 };

	frame.data = data;
	frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> INIT\n");
		break;

	case SWITCH_ABC_TYPE_READ_PING:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> PING\n");
		break;

	case SWITCH_ABC_TYPE_CLOSE:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> CLOSE\n");
		break;

	case SWITCH_ABC_TYPE_READ:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> READ\n");

		if (switch_sockaddr_info_get(&remote_sockaddr, li_globals.remote_addr, SWITCH_UNSPEC, li_globals.remote_port, 0, li_globals.pool) != SWITCH_STATUS_SUCCESS || !remote_sockaddr) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Remote Address Error!\n");
			return SWITCH_STATUS_FALSE;
		}

		while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "HUK SEQ[%u]!\n", (unsigned)frame.seq);
			len = (switch_size_t) frame.datalen / 2;
			if (len) switch_socket_sendto(li_globals.socket, remote_sockaddr, 0, frame.data, &len);
		}
switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "TILDE!\n");

		break;

	case SWITCH_ABC_TYPE_WRITE:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> WRITE\n");
		break;

	case SWITCH_ABC_TYPE_READ_REPLACE:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> READ_REPLACE\n");
		break;

	case SWITCH_ABC_TYPE_WRITE_REPLACE:
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "--> WRITE_REPLACE\n");
		break;
	}

	return SWITCH_TRUE;
}

SWITCH_STANDARD_APP(li_start_function)
{
    switch_media_bug_t *bug;
    switch_status_t status;
    switch_channel_t *channel;
    li_session_helper_t *li_session_helper;
		const char *p;

    if (session == NULL)
        return;

    channel = switch_core_session_get_channel(session);

    /* Is this channel already set? */
    bug = (switch_media_bug_t *) switch_channel_get_private(channel, "_li_");
    /* If yes */
    if (bug != NULL) {
        /* If we have a stop remove audio bug */
        if (strcasecmp(data, "stop") == 0) {
            switch_channel_set_private(channel, "_li_", NULL);
            switch_core_media_bug_remove(session, &bug);
            return;
        }

        /* We have already started */
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "Cannot run 2 at once on the same channel!\n");

        return;
    }

		/* create a new li_session_helper */
    li_session_helper = (li_session_helper_t *) switch_core_session_alloc(session, sizeof(li_session_helper_t));

		/* make session available from li_session_helper */
    li_session_helper->session = session;

		/* get LI_ID and store in li_session_helper */
		if ((p = switch_channel_get_variable(channel, "LI_ID")) && switch_true(p)) {
			li_session_helper->li_id = switch_core_session_strdup(session, p);
		} else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "No LI_ID was set!\n");
			return;
		}

		/* add the bug */
    status = switch_core_media_bug_add(session, li_callback, li_session_helper, 0, SMBF_BOTH, &bug);

    if (status != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Failure hooking to stream\n");
        return;
    }

    switch_channel_set_private(channel, "_li_", bug);
}

SWITCH_MODULE_LOAD_FUNCTION(mod_li_load)
{
    switch_application_interface_t *app_interface;

		/* initialize li_globals struct */
		memset(&li_globals, 0, sizeof(li_globals));

		/* make the pool available from li_globals */
		li_globals.pool = pool;

		/* do config */
		do_config(SWITCH_FALSE);

    /* connect my internal structure to the blank pointer passed to me */
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);

		/* make the li application available to the system */
    SWITCH_ADD_APP(app_interface, "li", "Legal Intercept", "Legal Intercept", li_start_function, "<start>", SAF_NONE);

		/* subscribe to reloadxml event, and hook it to reload_event_handler */
		if ((switch_event_bind_removable(modname, SWITCH_EVENT_RELOADXML, NULL, reload_event_handler, NULL, &NODE) != SWITCH_STATUS_SUCCESS)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind event!\n");
			return SWITCH_STATUS_TERM;
		}

		/* say it */
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Legal Intercept enabled\n");

    /* indicate that the module should continue to be loaded */
    return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_li_shutdown)
{
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Legal Intercept disabled\n");

/* TODO DESTROY THE SOCKET HERE ! */

    return SWITCH_STATUS_SUCCESS;
}


/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4:
 */
