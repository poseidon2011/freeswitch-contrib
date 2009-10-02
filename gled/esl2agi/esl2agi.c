/*
 * Copyright (c) 2009, Tristan Mahe for TELEMAQUE
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * Contrib:
 *  main: Anthony Minessale II
 *
 */

#include "esl2agi.h"

/* 
 * Translator thread entry point
 */
static void *esl2agi_thread(void *data) {
        esl_accept_t esl_req;
	esl_accept_t *tmp;
	esl2agi_pipes_t pipes;
        esl_handle_t eslC = {{0}};

        char script_path[1024] = "\0";
        char *buf;
	int pid;
	int thread_running;

	tmp = VOID_TO_ESL_A(data);
	esl_req.client_sock = tmp->client_sock;
	esl_req.addr = tmp->addr;
	free(tmp);

        esl_attach_handle(&eslC, esl_req.client_sock, esl_req.addr);
        if (!(buf = esl_event_get_header(eslC.info_event, "variable_ivr_path"))) {
                esl_disconnect(&eslC);
                perror("No ivr_path variable set, check your dialplan!");
                return NULL;
        }

        strncpy(script_path, buf, sizeof(script_path) - 1);

	if ( pipe(pipes.script) < 0) {
                esl_disconnect(&eslC);
                perror("script pipes failed...");
                return NULL;
	}
	if ( pipe(pipes.socket) < 0 ) {
		close(pipes.script[0]);
		close(pipes.script[1]);
                esl_disconnect(&eslC);
                perror("socket pipes failed...");
                return NULL;
	}

	thread_running = 1;
	pid = fork();
	if (pid < 0) {
		close(pipes.script[0]);
		close(pipes.script[1]);
		close(pipes.socket[0]);
		close(pipes.socket[1]);
                esl_disconnect(&eslC);
                perror("Fork failed, aie aie aie!");
		return NULL;
	}
	else if (pid == 0) {
		/* Child */
		close(pipes.socket[1]);
		close(pipes.script[0]);
		close(esl_req.client_sock);
		close(STDERR_FILENO);

	        dup2(pipes.socket[0], STDIN_FILENO);
        	dup2(pipes.script[1], STDOUT_FILENO);

	        execl(script_path, script_path, NULL);
		thread_running=0;
		perror("Error running script");
		close(pipes.socket[0]);
		close(pipes.script[1]);
		exit(0);
	}
	else if (pid > 0 ) { 
		esl_status_t status;
		char sbuf[2048];

		close(pipes.socket[0]);
		close(pipes.script[1]);

		signal(SIGCHLD, SIG_IGN);
		signal(SIGPIPE, SIG_IGN);
		// fprintf(stderr,"New call, handling setup...\n");
		handle_setup_env(pipes.socket[1],&eslC);

		/* TODO: check status */
		status = esl_send(&eslC,"myevents");
		eslC.async_execute = 1;
		while ( eslC.connected && thread_running) {
			int r , nfds = 0;
			fd_set rd;

			FD_ZERO(&rd);
			FD_SET(pipes.script[0], &rd);
			nfds = MAX(nfds,pipes.script[0]);

			r = select(nfds + 1, &rd, NULL, NULL, NULL);
			if (r == -1 )
				break;
			else if (FD_ISSET(pipes.script[0],&rd) ) { /* AGI has something to say */
				r = read(pipes.script[0],&sbuf,2047);
				if (r<0)
					goto end;
				else {
					/*TODO: correct ugly hack to remove final \n */
					sbuf[r-1] = '\0';
					// fprintf(stderr,"Treating: '%s'\n",sbuf);
					if ( find_and_exec_command(&eslC,pipes.socket[1],(char *) &sbuf) < 0 )
						goto end;
				}
			}
		}
end:
		kill(pid,SIGHUP);
		close(pipes.socket[1]);
		close(pipes.script[0]);
		if (eslC.connected)
			esl_execute(&eslC,"hangup",NULL,NULL);
	        esl_disconnect(&eslC);
	}
        return NULL;
}

/*
 * Callback on accept, launch a detached thread then return.
 */
static void esl2agi(esl_socket_t server_sock, esl_socket_t client_sock, struct sockaddr_in *addr) {
	esl_accept_t *esl_req;

	/* Will be freed in the thread after read */
	esl_req = malloc(sizeof(esl_accept_t) + 1);
	esl_req->client_sock = client_sock;
	esl_req->addr = addr;

	pthread_t translator;

	pthread_create(&translator,NULL,esl2agi_thread,ESL_A_TO_VOID(esl_req));
	pthread_detach(translator);

	return;
}

/* Main entry
 * Esl listen and that's all.
 * Copyright (c) 2009, Anthony Minessale II
 * TODO:	- add mod_dialplan_asterisk lookup and load if not present.
 */
int main(int argc, char *argv[])
{
	int i;
	char *ip = NULL;
	int port = 0;
	
	for (i = 1; i + 1 < argc; ) {
		if (!strcasecmp(argv[i], "-h")) {
			ip = argv[++i];
		} else if (!strcasecmp(argv[i], "-p")) {
			port = atoi(argv[++i]);
		} else {
			i++;
		}
	}

	if (!(ip && port)) {
		// fprintf(stderr, "Usage %s -h <host> -p <port>\n", argv[0]);
		return -1;
	}

	signal(SIGCHLD, SIG_IGN);

	esl_listen(ip, port, esl2agi);
	perror("We should never come here");	
	return 0;
}

static int safe_int_snprintf_buffer(char **buf,const char *format, int ret) {
	int size;
	size = 2 + strlen(format); // Max 2 digits in int
	if (*buf != NULL)
		*buf = realloc(*buf,size+1);
	else
		*buf = malloc(size +1);
	memset(*buf,0,size+1);
	size = snprintf(*buf,size+1,format,ret);
	return size;
}

/*
 * Return malloc'd buf containing header value specified with %format
 */
static int fill_buffer_from_header(esl_event_t *event,char **buf,char *header,const char *format) {
	int size=0;
	char *sbuf;
	sbuf = esl_event_get_header(event, header);
	if (sbuf) {
		size = strlen(sbuf) + strlen(format);
		if (*buf == NULL)
			*buf = malloc(size+1);
		else
			*buf = realloc(*buf,size+1);
		memset(*buf,0,size+1);
		size = snprintf(*buf,size,format,sbuf);
	}
	return size;
}

/* Setup env 
 * TODO:	- check size, if no header found we should not block
 */
static int handle_setup_env(int fd,esl_handle_t *eslC) {
	char *sbuf=NULL;
	int size;

	/* Setup Env for AGI scripts */
	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"variable_ivr_path","agi_request: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"variable_channel_name","agi_channel: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( write(fd, "agi_language: en\n",strlen("agi_language: en\n") ) < 0 )
		return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Source","agi_type: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Unique-ID","agi_uniqueid: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Caller-ID-Number","agi_callerid: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Caller-ID-Name","agi_calleridname: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Screen-Bit","agi_callingpres: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Caller-Destination-Number","agi_dnid: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Context","agi_context: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	if ( (size = fill_buffer_from_header(eslC->info_event,&sbuf,"Channel-Destination-Number","agi_extension: %s\n") ) > 0) {
		if ( write(fd,sbuf,size) < 0)
			return -1;
	}
	else
			return -1;

	/* TODO 
	 * agi_callingani2
         * agi_callington
	 * agi_callingtns
         * agi_rdnis
	 * agi_priority
         */

	if ( write(fd, "agi_enhanced: 0.0\n", strlen("agi_enhanced: 0.0\n") ) < 0 )
		return -1;
	if ( write(fd, "agi_accountcode: \n", strlen("agi_accountcode: \n") ) < 0 )
		return -1;

	if ( write(fd, "\n\n", 2 ) < 0 )
		return -1;

	return 0;
}

/*
 * Execute an application and wait for CHANNEL_EXECUTE_COMPLETE to return
 * binds esl_execute in a nice way :)
 * Be carefull, we might be in async mode, so we should match application we called before.
 */
static int do_execute(esl_handle_t *eslC,char *cmd,char *args,char *uuid,esl_event_t **save_reply) {
	int done=0;

	if ( esl_execute(eslC,cmd,args,uuid) != ESL_FAIL) {
		while (!done && eslC->connected) {
			if (esl_recv_event(eslC,1,NULL) == ESL_FAIL)
				return -1; // Maybe point of failure
			else if (eslC->last_ievent) {
				if (eslC->last_ievent->event_id == ESL_EVENT_CHANNEL_EXECUTE_COMPLETE ) {
					if (! strcasecmp(cmd,esl_event_get_header(eslC->last_ievent,"Application") ) )
						done = 1;
				}
			}
		}
		if (save_reply)
			*save_reply = eslC->last_ievent;
		return done;
	}
	else
		return -1;
}

/*
 * Answer AGI cmd
 * TODO: Why is there an A on the AGI side ?
 */
static int handle_answer(esl_handle_t *eslC,int fd,int *argc, char *argv[]) {
	int res;
	int size;
	char *buf=NULL;
	res = do_execute(eslC,"answer",NULL,NULL,NULL);

	size = safe_int_snprintf_buffer(&buf,"200 result=%d\n\n",res);

	res = write(fd,buf,size);
	free(buf);
	return res;
}


/*
 * Hangup AGI cmd
 * TODO:	- Add arg support to know which channel we want to hangup.
 */
static int handle_hangup(esl_handle_t *eslC,int fd,int *argc, char *argv[]) {
	int res;
	int size;
	char *buf=NULL;

	res = do_execute(eslC,"hangup",NULL,NULL,NULL);

	size = safe_int_snprintf_buffer(&buf,"200 result=%d\n\n",res);

	res = write(fd,buf,size);
	free(buf);
	return res;
}

/*
 * STREAM FILE agi cmd
 * TODO: rewrites with good string handling :)
 */ 
static int handle_streamfile(esl_handle_t *eslC,int fd,int *argc, char *argv[]) {
	char *buf;
	int offset=0;
	char dtmf[2]={"0"};
	int res;

	esl_event_t *reply=NULL;

	if (*argc < 3 || *argc > 5)
		return -1;

	if (argv[3]) {
		/* We should set playback_terminators var there */
		buf = malloc(strlen(argv[3])+22);
		memset(buf,0,strlen(argv[3])+22);
		snprintf(buf,22,"playback_terminators=%s",argv[3]);
		do_execute(eslC,"set",buf,NULL,NULL);
		free(buf);
	}

	if (argv[4]) {
		offset = atoi(argv[4]);
	}

	buf = malloc(strlen(argv[2]) + 1);
	memset(buf,0,strlen(argv[2])+1);

	if (offset > 0) {
		buf = realloc(buf,strlen(argv[2]) + strlen(argv[4]) + 3);
		snprintf(buf,strlen(argv[2]) + strlen(argv[4]) + 3,"%s@@%d",argv[2],offset);

	}
	else
		snprintf(buf,strlen(argv[2]) + 1,"%s",argv[2]);

	offset = 0;
	if ( do_execute(eslC,"playback",buf,NULL,&reply) < 0 ) { /* We should write about NON success there instead of returning stock */
		offset = -1;
		fprintf(stderr,"do_execute stream file failed\n");
	}
	else {
		free(buf);
		buf=NULL;
		if (reply) {
			res = fill_buffer_from_header(eslC->info_event,&buf,"variable_playback_samples","%d");
			if ( res <= 0 )
				offset = 0;
			else 
				offset = atoi(buf);

			fprintf(stderr,"Offset is %d, res is %d, buf is %s\n",offset,res,buf);

			res = fill_buffer_from_header(reply,&buf,"variable_playback_terminator_used","%s");
			// sbuf = esl_event_get_header(reply, "variable_playback_terminator_used");
			if (res > 0)
				snprintf((char *)&dtmf,2,"%s",buf);
		}
		else {
			fprintf(stderr,"No event reply in stream file\n");
			offset=-1;
		}
	}
	if (buf != NULL)
		free(buf);
	fprintf(stderr,"End stream file %d %s\n",offset,dtmf);
	if (offset < 0) {
		if ( write(fd,"200 result=-1 endpos=0\n\n",128) < 0 ) {
			fprintf(stderr,"Write failed\n");
			return -1;
		}
	}
	else {
		buf=malloc(65);
		memset(buf,0,65);
		snprintf(buf,64,"200 result=%s endpos=%d\n\n",dtmf,offset);
		if ( write(fd,buf,64) < 0 ) {
			free(buf);
			fprintf(stderr,"Write failed\n");
			return -1;
		}
		free(buf);
	}
	return 0;
}

static void parse_args(char *buf,int *argc,char *argv[_MAX_CMD_ARGS]) {
	char *cur;
	int x = 0;
	int quote = 0;
	int escaped = 0;
	int space=1;

	cur = buf;
	while (*buf) {
		switch (*buf) {
			case '"':
				if (escaped)
					goto ok;
				else
					quote = !quote;
				if (quote && space) {
					argv[x++] = cur;
					space=0;
				}
				escaped = 0;
			break;
			case ' ':
			case '\t':
				if (!quote && !escaped) {
					space = 1;
					*(cur++) = '\0';
				} else
					goto ok;
			break;
			case '\\':
				if (escaped)
					goto ok;
				else
					escaped = 1;
			break;
			default:
ok:
				if (space) {
					argv[x++] = cur;
					space=0;
				}
				*(cur++) = *buf;
				escaped=0;
		}
		buf++;
	}
	*(cur++) = '\0';
	argv[x]= NULL;
	*argc = x;
	return;
}

static command_binding_t bindings[_MAX_CMD] = {
	{ {"ANSWER" , NULL}, handle_answer },
	{ {"HANGUP" , NULL}, handle_hangup },
	{ {"STREAM", "FILE" , NULL}, handle_streamfile },
};


static command_binding_t *find_binding(char *cmd[]) {
	int x,y,match=0;

	for (x=0; x < sizeof(bindings); x++) {
		if ( ! bindings[x].cmd[0] ) {
			break;
		}
		match = 1;

		for (y=0; match && cmd[y]; y++) {
			if (!bindings[x].cmd[y]) /* Is the next part existing ?*/
				break;
			if ( strncasecmp(bindings[x].cmd[y],cmd[y], MAX(strlen(bindings[x].cmd[y]),strlen(cmd[y]) ) ) )
				match=0;
		}
		if (bindings[x].cmd[y]) /* There's a word missing in cmd */
			match=0;
		if (match)
			return &bindings[x];
	}
	return NULL;
}

static int find_and_exec_command(esl_handle_t *eslC,int fd,char *buf) {
	char *argv[1024];
	int argc = 0;
	int r = -1;
	command_binding_t *bind;

	parse_args(buf,&argc,argv);

	bind = find_binding(argv);
	if (bind) {
		r = bind->handler(eslC,fd,&argc,argv);
	}
	return r;
}
