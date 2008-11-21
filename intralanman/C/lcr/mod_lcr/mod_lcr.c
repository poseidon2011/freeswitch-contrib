/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005/2006, Anthony Minessale II <anthmct@yahoo.com>
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
 * Anthony Minessale II <anthmct@yahoo.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 * Raymond Chandler <intralanman@gmail.com>
 *
 * mod_lcr.c -- Least Cost Routing Module
 *
 */

#include <switch.h>
#include <switch_odbc.h>


#define LCR_SYNTAX "lcr <digits>"

/* SQL Query places */
#define LCR_DIGITS_PLACE 0
#define LCR_CARRIER_PLACE 1
#define LCR_RATE_PLACE 2
#define LCR_GATEWAY_PLACE 3
#define LCR_IP_ADDRESS_PLACE 4
#define LCR_PORT_PLACE 5
#define LCR_LSTRIP_PLACE 6
#define LCR_TSTRIP_PLACE 7
#define LCR_PREFIX_PLACE 8
#define LCR_SUFFIX_PLACE 9


#define LCR_DIALSTRING_PLACE 3
#define LCR_HEADERS_COUNT 4
char headers[LCR_HEADERS_COUNT][32] = {
	"Digit Match",
	"Carrier",
	"Rate",
	"Dialstring",
};

struct odbc_obj {
	switch_odbc_handle_t *handle;
	SQLHSTMT stmt;
	SQLCHAR *colbuf;
	int32_t cblen;
	SQLCHAR *code;
	int32_t codelen;
};

struct lcr_obj {
	char *carrier_name;
	char *gateway_name;
	char *term_host;
	char *port;
	char *digit_str;
	char *prefix;
	char *suffix;
	char *dialstring;
	float rate;
	size_t lstrip;
	size_t tstrip;
	size_t digit_len;
	struct lcr_obj *next;
};

struct max_obj {
	size_t carrier_name;
	size_t gateway_name;
	size_t digit_str;
	size_t rate;
	size_t dialstring;
};

typedef struct odbc_obj  odbc_obj_t;
typedef odbc_obj_t *odbc_handle;

typedef struct lcr_obj lcr_obj_t;
typedef lcr_obj_t *lcr_route;

typedef struct max_obj max_obj_t;
typedef max_obj_t *max_len;

struct callback_obj {
	lcr_route head;
	int matches;
	char *lookup_number;
};
typedef struct callback_obj callback_t;

static struct {
	switch_memory_pool_t *pool;
	char *dbname;
	char *odbc_dsn;
	switch_mutex_t *mutex;
	switch_odbc_handle_t *master_odbc;
	void *filler1;
} globals;


SWITCH_MODULE_LOAD_FUNCTION(mod_lcr_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_lcr_shutdown);
SWITCH_MODULE_DEFINITION(mod_lcr, mod_lcr_load, mod_lcr_shutdown, NULL);

static char *get_bridge_data(const char *dialed_number, lcr_route cur_route) {
	size_t lstrip;
	size_t  tstrip;
	char *data = NULL;
	char *destination_number = NULL; 

	destination_number = strdup(dialed_number);

	tstrip = ((cur_route->digit_len - cur_route->tstrip) + 1);
	lstrip = cur_route->lstrip;
	
	if (strlen(destination_number) > tstrip && cur_route->tstrip > 0) {
		destination_number[tstrip] = '\0';
	}
	if (strlen(destination_number) > lstrip && cur_route->lstrip > 0) {
		destination_number += lstrip;
	}
	
	if (strlen(cur_route->gateway_name) > 0) {
		 data = switch_mprintf("sofia/gateway/%s/%s%s%s", cur_route->gateway_name
							   , cur_route->prefix, destination_number, cur_route->suffix
							   );
	} else {
		char port[8];
		switch_snprintf(port, sizeof(port), ":%s", cur_route->port);
		data = switch_mprintf("sofia/${use_profile}/%s@%s%s"
							  , destination_number, cur_route->term_host, (strlen(cur_route->port) > 0 ? port : "")
							  );
	}
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Returning Dialstring %s\n", data);
	switch_safe_free(destination_number);
	return data;
}

void init_max_lens(max_len maxes) {
	maxes->digit_str = (headers[LCR_DIGITS_PLACE] == NULL ? 0 : strlen(headers[LCR_DIGITS_PLACE]));
	maxes->carrier_name = (headers[LCR_CARRIER_PLACE] == NULL ? 0 : strlen(headers[LCR_CARRIER_PLACE]));
	maxes->gateway_name = (headers[LCR_GATEWAY_PLACE] == NULL ? 0 : strlen(headers[LCR_GATEWAY_PLACE]));
	maxes->dialstring = (headers[LCR_DIALSTRING_PLACE] == NULL ? 0 : strlen(headers[LCR_DIALSTRING_PLACE]));
	maxes->digit_str = (headers[LCR_DIGITS_PLACE] == NULL ? 0 : strlen(headers[LCR_DIGITS_PLACE]));
	maxes->rate = 8;
}

switch_status_t process_max_lengths(max_obj_t *maxes, lcr_route routes, char *destination_number) {
	lcr_route current = NULL;

	if (routes == NULL) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "no routes\n");
		return SWITCH_STATUS_FALSE;
	}
	if (maxes == NULL) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "no maxes\n");
		return SWITCH_STATUS_FALSE;
	}

	init_max_lens(maxes);

	for (current = routes; current; current = current->next) {
		size_t this_len;

		if (current->gateway_name != NULL) {
			this_len = strlen(current->gateway_name);
			if (this_len > maxes->gateway_name) {				
				maxes->gateway_name = this_len;
			}
		}
		if (current->carrier_name != NULL) {
			this_len = strlen(current->carrier_name);
			if (this_len > maxes->carrier_name) {
				maxes->carrier_name = this_len;
			}
		}
		if (current->dialstring != NULL) {
			this_len = strlen(current->dialstring);
			if (this_len > maxes->dialstring) {
				maxes->dialstring = this_len;
			}
		}
		if (current->digit_str != NULL) {
			if (current->digit_len > maxes->digit_str) {
				maxes->digit_str = current->digit_len;
			} 
		}
	}
	return SWITCH_STATUS_SUCCESS;
}

static switch_bool_t lcr_execute_sql_callback(char *sql, switch_core_db_callback_func_t callback, void *pdata) {
	if (globals.odbc_dsn) {
		switch_odbc_handle_callback_exec(globals.master_odbc, sql, callback, pdata);
		return SWITCH_TRUE;
	}
	return SWITCH_FALSE;
}

int route_add_callback(void *pArg, int argc, char **argv, char **columnNames) {
	lcr_route additional = NULL;
	lcr_route current = NULL;
	callback_t *cbt = (callback_t *) pArg;
	
	cbt->matches++;

	switch_zmalloc(additional, sizeof(lcr_obj_t));

	additional->digit_len = strlen(argv[LCR_DIGITS_PLACE]);
	additional->digit_str = switch_safe_strdup(argv[LCR_DIGITS_PLACE]);
	additional->suffix = switch_safe_strdup(argv[LCR_SUFFIX_PLACE]);
	additional->prefix = switch_safe_strdup(argv[LCR_PREFIX_PLACE]);
	additional->carrier_name = switch_safe_strdup(argv[LCR_CARRIER_PLACE]);
	additional->rate = (float)atof(argv[LCR_RATE_PLACE]);
	additional->gateway_name = switch_safe_strdup(argv[LCR_GATEWAY_PLACE]);
	additional->term_host = switch_safe_strdup(argv[LCR_IP_ADDRESS_PLACE]);
	additional->port = switch_safe_strdup(argv[LCR_PORT_PLACE]);
	additional->lstrip = atoi(argv[LCR_LSTRIP_PLACE]);
	additional->tstrip = atoi(argv[LCR_TSTRIP_PLACE]);
	additional->dialstring = get_bridge_data(cbt->lookup_number, additional);

	if (cbt->head == NULL) {
		additional->next = cbt->head;
		cbt->head = additional;

		return SWITCH_STATUS_SUCCESS;
	}

	for (current = cbt->head; current; current = current->next) {

		if (switch_strlen_zero(additional->gateway_name) && switch_strlen_zero(additional->term_host) && switch_strlen_zero(additional->port) ) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING
							  , "WTF?!? There's no way to dial this Gateway: %s IP:Port %s:%s\n"
							  , additional->gateway_name, additional->term_host, additional->port
							  );
			break;
		}
			
		if (!strcmp(current->gateway_name, additional->gateway_name)) {
			if (!strcmp(current->term_host, additional->term_host) && !strcmp(current->port, additional->port)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG
								  , "Ignoring Duplicate route for termination point (%s:%s)\n"
								  , additional->term_host, additional->port
								  );
				switch_safe_free(additional);
				break;
			}
		}
			
		if (current->next == NULL) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "adding route to end of list\n");
			current->next = additional;
			break;
		}
	}
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t lcr_do_lookup(char *digits, callback_t *cb_struct) {
	/* instantiate the object/struct we defined earlier */
	switch_stream_handle_t sql_stream = { 0 };
	size_t n, digit_len = strlen(digits);
	char *digits_copy;

	if (switch_strlen_zero(digits)) {
		return SWITCH_FALSE;
	}

   	digits_copy = strdup(digits);

	SWITCH_STANDARD_STREAM(sql_stream);

	/* set up the query to be executed */
	sql_stream.write_function(&sql_stream, 
							  "SELECT l.digits, c.Carrier_Name, l.rate, cg.gateway, cg.term_host, cg.port, l.lead_strip, l.trail_strip, l.prefix, l.suffix "
							  );
	sql_stream.write_function(&sql_stream, "FROM lcr l JOIN carriers c ON l.carrier_id=c.id JOIN carrier_gateway cg ON c.id=cg.carrier_id ");
	for (n = digit_len; n > 0; n--) {
		digits_copy[n] = '\0';
		sql_stream.write_function(&sql_stream, "%s digits='%s' ", (n==digit_len ? "WHERE" : "OR"), digits_copy);
	}
	sql_stream.write_function(&sql_stream, "ORDER BY digits DESC, rate;");

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "%s\n", (char *)sql_stream.data);    

	lcr_execute_sql_callback((char *)sql_stream.data, route_add_callback, cb_struct);
	switch_safe_free(sql_stream.data);

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t lcr_load_config() {
	char *cf = "lcr.conf";
	switch_xml_t cfg, xml, settings, param;
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	char *odbc_user = NULL;
	char *odbc_pass = NULL;

	if (!(xml = switch_xml_open_cfg(cf, &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "open of %s failed\n", cf);
		return SWITCH_STATUS_TERM;
	}

	if ((settings = switch_xml_child(cfg, "settings"))) {
		for (param = switch_xml_child(settings, "param"); param; param = param->next) {
			char *var = NULL;
			char *val = NULL;
			var = (char *) switch_xml_attr_soft(param, "name");
			val = (char *) switch_xml_attr_soft(param, "value");
			if (!strcasecmp(var, "odbc-dsn") && !switch_strlen_zero(val)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "odbc_dsn is %s\n", val);
				globals.odbc_dsn = switch_core_strdup(globals.pool, val);
				if ((odbc_user = strchr(globals.odbc_dsn, ':'))) {
					*odbc_user++ = '\0';
					if ((odbc_pass = strchr(odbc_user, ':'))) {
						*odbc_pass++ = '\0';
					}
				}
			}
		}
	}
	if (globals.odbc_dsn) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO
						  , "dsn is \"%s\", user is \"%s\", and password is \"%s\"\n"
						  , globals.odbc_dsn, odbc_user, odbc_pass
						  );
		if (!(globals.master_odbc = switch_odbc_handle_new(globals.odbc_dsn, odbc_user, odbc_pass))) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Cannot Open ODBC Database!\n");
			status = SWITCH_STATUS_FALSE;
			goto done;
		}
		if (switch_odbc_handle_connect(globals.master_odbc) != SWITCH_ODBC_SUCCESS) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Cannot Open ODBC Database!\n");
			status = SWITCH_STATUS_FALSE;
			goto done;
		}
	}
 done:
	switch_xml_free(xml);
	return status;
}

static void destroy_list(lcr_route *head) {
	lcr_route cur = NULL, top = *head;

	while (top) {
		cur = top;
		top = top->next;
		switch_safe_free(cur->digit_str);
		switch_safe_free(cur->suffix);
		switch_safe_free(cur->prefix);
		switch_safe_free(cur->carrier_name);
		switch_safe_free(cur->gateway_name);
		switch_safe_free(cur);
	}
	*head = NULL;
}

SWITCH_STANDARD_DIALPLAN(lcr_dialplan_hunt) {
	switch_caller_extension_t *extension = NULL;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	callback_t routes = { 0 };
	lcr_route cur_route = { 0 };
	char *bridge_data = NULL;;

	if (!caller_profile) {
		caller_profile = switch_channel_get_caller_profile(channel);
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "LCR Lookup on %s\n", caller_profile->destination_number);
	routes.lookup_number = caller_profile->destination_number;
	if (lcr_do_lookup(caller_profile->destination_number, &routes) == SWITCH_STATUS_SUCCESS) {
		if ((extension = switch_caller_extension_new(session, caller_profile->destination_number, caller_profile->destination_number)) == 0) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "memory error!\n");
			destroy_list(&routes.head);
			return NULL;
		}

		switch_channel_set_variable(channel, SWITCH_CONTINUE_ON_FAILURE_VARIABLE, "true");
		switch_channel_set_variable(channel, SWITCH_HANGUP_AFTER_BRIDGE_VARIABLE, "true");

		for (cur_route = routes.head; cur_route; cur_route = cur_route->next) {
			//bridge_data = get_bridge_data(caller_profile->destination_number, cur_route);
			switch_caller_extension_add_application(session, extension, "bridge", cur_route->dialstring);
			switch_safe_free(bridge_data);
		}
		destroy_list(&routes.head);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "LCR lookup failed for %s\n", caller_profile->destination_number);
	}
	switch_safe_free(bridge_data);

	return extension;
}

void str_repeat(size_t how_many, char *what, switch_stream_handle_t *str_stream) {
	size_t i;

	//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "repeating %d of '%s'\n", how_many, what);

	for (i=0; i<how_many; i++) {
		str_stream->write_function(str_stream, "%s", what);
	}
}

SWITCH_STANDARD_API(dialplan_lcr_function) {
	char *argv[4] = { 0 };
	int argc;
	char *mydata = NULL;
	char *dialstring = NULL;
	char *destination_number = NULL;
	lcr_route current = NULL;
	max_obj_t maximum_lengths = { 0 };
	callback_t cb_struct = { 0 };
	//switch_malloc(maximum_lengths, sizeof(max_obj_t));

	if (switch_strlen_zero(cmd)) {
		goto usage;
	}

	mydata = switch_safe_strdup(cmd);

	if ((argc = switch_separate_string(mydata, ' ', argv, (sizeof(argv) / sizeof(argv[0]))))) {
		destination_number = strdup(argv[0]);
		cb_struct.lookup_number = destination_number;

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO
						  , "data passed to lcr is [%s]\n", cmd
						  );
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO
						  , "lcr lookup returned [%d]\n"
						  , lcr_do_lookup(destination_number, &cb_struct)
						  );
		if (cb_struct.head != NULL) {
			size_t len;

			process_max_lengths(&maximum_lengths, cb_struct.head, destination_number);

			stream->write_function(stream, " | %s", headers[LCR_DIGITS_PLACE]);
			if ((len = (maximum_lengths.digit_str - strlen(headers[LCR_DIGITS_PLACE]))) > 0) {
				str_repeat(len, " ", stream);
			}

			stream->write_function(stream, " | %s", headers[LCR_CARRIER_PLACE]);
			if ((len = (maximum_lengths.carrier_name - strlen(headers[LCR_CARRIER_PLACE]))) > 0) {
				str_repeat(len, " ", stream);
			}

			stream->write_function(stream, " | %s", headers[LCR_RATE_PLACE]);
			if ((len = (maximum_lengths.rate - strlen(headers[LCR_RATE_PLACE]))) > 0) {
				str_repeat(len, " ", stream);
			}

			stream->write_function(stream, " | %s", headers[LCR_DIALSTRING_PLACE]);
			if ((len = (maximum_lengths.dialstring - strlen(headers[LCR_DIALSTRING_PLACE]))) > 0) {
				str_repeat(len, " ", stream);
			}

			stream->write_function(stream, " |\n");

			current = cb_struct.head;
			while (current) {
				char srate[10];

				dialstring = get_bridge_data(destination_number, current);
				switch_snprintf(srate, sizeof(srate), "%0.5f", current->rate);

				stream->write_function(stream, " | %s", current->digit_str);
				str_repeat((maximum_lengths.digit_str - current->digit_len), " ", stream);
				
				stream->write_function(stream, " | %s", current->carrier_name );
				str_repeat((maximum_lengths.carrier_name - strlen(current->carrier_name)), " ", stream);
				
				stream->write_function(stream, " | %s", srate );
				str_repeat((maximum_lengths.rate - strlen(srate)), " ", stream);
								
				stream->write_function(stream, " | %s", dialstring);
				str_repeat((maximum_lengths.dialstring - strlen(dialstring)), " ", stream);
				
				stream->write_function(stream, " |\n");
				
				switch_safe_free(dialstring);
				current = current->next;
			}

			destroy_list(&cb_struct.head);
			switch_safe_free(dialstring);
		} else {
			stream->write_function(stream, "No Routes To Display\n");
		}
	}

	switch_safe_free(mydata);
	return SWITCH_STATUS_SUCCESS;
 usage:
	stream->write_function(stream, "USAGE: %s\n", LCR_SYNTAX);
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_lcr_load) {
	switch_api_interface_t *dialplan_lcr_api_interface;
	switch_dialplan_interface_t *dp_interface;
	
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

#ifndef SWITCH_HAVE_ODBC
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "You must have ODBC support in FreeSWITCH to use this module\n");
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "\t./configure --enable-core-odbc-support\n");
	return SWITCH_STATUS_FALSE;
#endif

	globals.pool = pool;

	if (lcr_load_config() != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Unable to load lcr config file\n");
		return SWITCH_STATUS_FALSE;
	}
	if (switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, globals.pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "failed to initialize mutex\n");
	}

	SWITCH_ADD_API(dialplan_lcr_api_interface, "lcr", "Least Cost Routing Module", dialplan_lcr_function, LCR_SYNTAX);
	SWITCH_ADD_DIALPLAN(dp_interface, "lcr", lcr_dialplan_hunt);
	
	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_lcr_shutdown) {
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
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 expandtab:
 */
