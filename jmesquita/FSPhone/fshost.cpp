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
 * Joao Mesquita <jmesquita@freeswitch.org>
 *
 */

#include <QtGui>
#include "fshost.h"
#include "call.h"
#include "mod_qsettings/mod_qsettings.h"

/* Declare it globally */
FSHost g_FSHost;

FSHost::FSHost(QObject *parent) :
    QThread(parent)
{
    /* Initialize libs & globals */
    printf("Initializing globals...\n");
    switch_core_setrlimits();
    switch_core_set_globals();

    qRegisterMetaType<Call>("Call");

}

void FSHost::run(void)
{
    switch_core_flag_t flags = SCF_USE_SQL | SCF_USE_AUTO_NAT;
    const char *err = NULL;
    switch_bool_t console = SWITCH_FALSE;
    switch_status_t destroy_status;

    /* Create directory structure for softphone with default configs */
    QDir conf_dir = QDir(QDir::home());
    if (!conf_dir.exists(".fsphone"))
    {
        conf_dir.mkpath(".fsphone/conf/accounts");
        QFile rootXML(":/confs/freeswitch.xml");
        QString dest = QString("%1/.fsphone/conf/freeswitch.xml").arg(conf_dir.absolutePath());
        rootXML.copy(dest);

        QFile defaultAccount(":/confs/example.xml");
        dest = QString("%1/.fsphone/conf/accounts/example.xml").arg(conf_dir.absolutePath());
        defaultAccount.copy(dest);
    }

    /* Set all directories to the home user directory */
    if (conf_dir.cd(".fsphone"))
    {
        SWITCH_GLOBAL_dirs.conf_dir = (char *) malloc(strlen(QString("%1/conf").arg(conf_dir.absolutePath()).toAscii().constData()) + 1);
        if (!SWITCH_GLOBAL_dirs.conf_dir) {
            emit coreLoadingError("Cannot allocate memory for conf_dir.");
        }
        strcpy(SWITCH_GLOBAL_dirs.conf_dir, QString("%1/conf").arg(conf_dir.absolutePath()).toAscii().constData());

        SWITCH_GLOBAL_dirs.log_dir = (char *) malloc(strlen(QString("%1/log").arg(conf_dir.absolutePath()).toAscii().constData()) + 1);
        if (!SWITCH_GLOBAL_dirs.log_dir) {
            emit coreLoadingError("Cannot allocate memory for log_dir.");
        }
        strcpy(SWITCH_GLOBAL_dirs.log_dir, QString("%1/log").arg(conf_dir.absolutePath()).toAscii().constData());

        SWITCH_GLOBAL_dirs.run_dir = (char *) malloc(strlen(QString("%1/run").arg(conf_dir.absolutePath()).toAscii().constData()) + 1);
        if (!SWITCH_GLOBAL_dirs.run_dir) {
            emit coreLoadingError("Cannot allocate memory for run_dir.");
        }
        strcpy(SWITCH_GLOBAL_dirs.run_dir, QString("%1/run").arg(conf_dir.absolutePath()).toAscii().constData());

        SWITCH_GLOBAL_dirs.db_dir = (char *) malloc(strlen(QString("%1/db").arg(conf_dir.absolutePath()).toAscii().constData()) + 1);
        if (!SWITCH_GLOBAL_dirs.db_dir) {
            emit coreLoadingError("Cannot allocate memory for db_dir.");
        }
        strcpy(SWITCH_GLOBAL_dirs.db_dir, QString("%1/db").arg(conf_dir.absolutePath()).toAscii().constData());

        SWITCH_GLOBAL_dirs.script_dir = (char *) malloc(strlen(QString("%1/script").arg(conf_dir.absolutePath()).toAscii().constData()) + 1);
        if (!SWITCH_GLOBAL_dirs.script_dir) {
            emit coreLoadingError("Cannot allocate memory for script_dir.");
        }
        strcpy(SWITCH_GLOBAL_dirs.script_dir, QString("%1/script").arg(conf_dir.absolutePath()).toAscii().constData());

        SWITCH_GLOBAL_dirs.htdocs_dir = (char *) malloc(strlen(QString("%1/htdocs").arg(conf_dir.absolutePath()).toAscii().constData()) + 1);
        if (!SWITCH_GLOBAL_dirs.htdocs_dir) {
            emit coreLoadingError("Cannot allocate memory for htdocs_dir.");
        }
        strcpy(SWITCH_GLOBAL_dirs.htdocs_dir, QString("%1/htdocs").arg(conf_dir.absolutePath()).toAscii().constData());
    }

    /* If you need to override configuration directories, you need to change them in the SWITCH_GLOBAL_dirs global structure */
    printf("Initializing core...\n");
    /* Initialize the core and load modules, that will startup FS completely */
    if (switch_core_init_and_modload(flags, console, &err) != SWITCH_STATUS_SUCCESS) {
        fprintf(stderr, "Failed to initialize FreeSWITCH's core: %s\n", err);
        emit coreLoadingError(err);
    }

    printf("Everything OK, Entering runtime loop.\n");

    if (switch_event_bind("FSHost", SWITCH_EVENT_ALL, SWITCH_EVENT_SUBCLASS_ANY, eventHandlerCallback, NULL) != SWITCH_STATUS_SUCCESS) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind!\n");
            printf("Something went really wrong while binding to events...");
    }

    /* Load our QSettings module */
    if (switch_loadable_module_build_dynamic("mod_qsettings",mod_qsettings_load,NULL,mod_qsettings_shutdown,SWITCH_FALSE) != SWITCH_STATUS_SUCCESS)
    {
        printf("Something went wrong when loading our QSettings module\n");
    }
    emit ready();
    /* Go into the runtime loop. If the argument is true, this basically sets runtime.running = 1 and loops while that is set
     * If its false, it initializes the libedit for the console, then does the same thing
     */
    switch_core_runtime_loop(!console);
    fflush(stdout);


    switch_event_unbind_callback(eventHandlerCallback);
    /* When the runtime loop exits, its time to shutdown */
    destroy_status = switch_core_destroy();
    if (destroy_status == SWITCH_STATUS_SUCCESS)
    {
        printf("We have properly shutdown the core.\n");
    }
}

switch_status_t FSHost::processAlegEvent(switch_event_t * event, QString uuid)
{
    if (_active_calls.value(uuid)->getDirection() == FSPHONE_CALL_DIRECTION_INBOUND)
    {
        switch(event->event_id) {
        case SWITCH_EVENT_CHANNEL_ANSWER:
            {
                _active_calls.value(uuid)->setbUUID(switch_event_get_header_nil(event, "Other-Leg-Unique-ID"));
                _bleg_uuids.insert(switch_event_get_header_nil(event, "Other-Leg-Unique-ID"), uuid);
                emit answered(uuid);
                break;
            }
        case SWITCH_EVENT_CHANNEL_HANGUP_COMPLETE:
            {
                emit hungup(uuid);
                break;
            }
        case SWITCH_EVENT_CHANNEL_STATE:
            {
                printf("CHANNEL_STATE Answer-State: %s | Channel-State: %s | %s | %s\n", switch_event_get_header_nil(event, "Answer-State"),switch_event_get_header_nil(event, "Channel-State"), uuid.toAscii().constData(), switch_event_get_header_nil(event, "Other-Leg-Unique-ID"));
                break;
            }
        default:
            {
                break;
            }
        }
    }
    /* Outbound call */
    else
    {
        switch(event->event_id)
        {
        case SWITCH_EVENT_CHANNEL_BRIDGE:
            {
                _active_calls.value(uuid)->setbUUID(switch_event_get_header_nil(event, "Other-Leg-Unique-ID"));
                _bleg_uuids.insert(switch_event_get_header_nil(event, "Other-Leg-Unique-ID"), uuid);
                break;
            }
        default:
            break;
        }
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t FSHost::processBlegEvent(switch_event_t * event, QString buuid)
{
    QString uuid = _bleg_uuids.value(buuid);
    printf("We know this is an uuid related to our call: %s | Event: %s\n", buuid.toAscii().constData(),
           switch_event_name(event->event_id));
    /* Inbound call */
    if (_active_calls.value(uuid)->getDirection() == FSPHONE_CALL_DIRECTION_INBOUND)
    {
        qDebug() << " Inbound call";
    }
    /* Outbound call */
    else
    {
        switch(event->event_id)
        {
        case SWITCH_EVENT_CHANNEL_ANSWER:
            {
                emit answered(uuid);
            }
        default:
            break;
        }
    }
    return SWITCH_STATUS_SUCCESS;
}

void FSHost::generalEventHandler(switch_event_t *event)
{
    QString uuid = switch_event_get_header_nil(event, "Unique-ID");

    if (_bleg_uuids.contains(uuid))
    {
        if (processBlegEvent(event, uuid) == SWITCH_STATUS_SUCCESS)
        {
            return;
        }
    }
    if (_active_calls.contains(uuid))
    {
        if (processAlegEvent(event, uuid) == SWITCH_STATUS_SUCCESS)
        {
            return;
        }
    }

    /* This is how we identify new calls, inbound and outbound */
    switch(event->event_id) {
    case SWITCH_EVENT_CUSTOM:
        {
            if (strcmp(event->subclass_name, "portaudio::ringing") == 0 && !_active_calls.contains(uuid))
            {
                Call *call = new Call(atoi(switch_event_get_header_nil(event, "call_id")),
                                      switch_event_get_header_nil(event, "Caller-Caller-ID-Name"),
                                      switch_event_get_header_nil(event, "Caller-Caller-ID-Number"),
                                      FSPHONE_CALL_DIRECTION_INBOUND,
                                      uuid);
                _active_calls.insert(uuid, call);
                emit ringing(uuid);
            }
            else if (strcmp(event->subclass_name, "portaudio::makecall") == 0)
            {
                Call *call = new Call(atoi(switch_event_get_header_nil(event, "call_id")),NULL,
                                      switch_event_get_header_nil(event, "Caller-Destination-Number"),
                                      FSPHONE_CALL_DIRECTION_OUTBOUND,
                                      uuid);
                _active_calls.insert(uuid, call);
                emit newOutgoingCall(uuid);
            }
            else if (strcmp(event->subclass_name, "sofia::gateway_state") == 0)
            {
                QString state = switch_event_get_header_nil(event, "State");
                QString gw = switch_event_get_header_nil(event, "Gateway");
                if (state == "TRYING")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_TRYING);
                else if (state == "REGISTER")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_REGISTER);
                else if (state == "REGED")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_REGED);
                else if (state == "UNREGED")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_UNREGED);
                else if (state == "UNREGISTER")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_UNREGISTER);
                else if (state =="FAILED")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_FAILED);
                else if (state == "FAIL_WAIT")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_FAIL_WAIT);
                else if (state == "EXPIRED")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_EXPIRED);
                else if (state == "NOREG")
                    emit gwStateChange(gw, FSPHONE_GW_STATE_NOREG);
            }
            else
            {
                //printf("We got a not treated custom event: %s\n", (!zstr(event->subclass_name) ? event->subclass_name : "NULL"));
            }
            break;
        }
    }
}

switch_status_t FSHost::sendCmd(const char *cmd, const char *args, QString *res)
{
    switch_status_t status = SWITCH_STATUS_FALSE;
    switch_stream_handle_t stream = { 0 };
    SWITCH_STANDARD_STREAM(stream);
    status = switch_api_execute(cmd, args, NULL, &stream);
    *res = switch_str_nil((char *) stream.data);

    return status;
}
