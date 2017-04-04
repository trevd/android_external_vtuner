#define LOG_TAG "vtunerd"
#include <cutils/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/dvb/version.h>

#include "vtunerd.h"


int dbg_level  = MSG_INFO;
int use_syslog = 1;

#define DEBUGMAIN(msg, ...)  DEBUG(MSG_MAIN, "[%d %s:%u] debug: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)

#define MAX_SESSIONS 8

typedef struct vtuner_session {
	vtuner_session_status_t status;
	int tuner_type;
	int tuner_group;
	int adapter;
	int frontend;
	int demux;
	int dvr;
	pthread_t th;
	struct sockaddr_in client_so;
} vtuner_session_t;

void *session_worker(void *data) {
	  vtuner_session_t *session = (vtuner_session_t*)data;
	  run_worker(session->adapter, session->frontend, session->demux, session->dvr, &session->client_so);
	  session->status = SST_IDLE;
	  return 0;
}

int main(int argc, char **argv) {
	int i, c;
	vtuner_session_t session[MAX_SESSIONS];
	int hw_count = 0;
	int tuner_group = VTUNER_GROUPS_ALL;
	int opt_err = 0;
	unsigned short lport = 0;
	char *lip = NULL;



	fprintf(stderr, "vtuner server (vtunerd), part of vtuner project\n"
			"Visit http://code.google.com/p/vtuner/ for more information\n"
			"Copyright (C) 2009-11 Roland Mieslinger\n"
			"This is free software; see the source for copying conditions.\n"
			"There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
			"FOR A PARTICULAR PURPOSE.\n");

	// read only verbosity, to show value
	while((c = getopt(argc, argv, "d:g:hl:p:u:v:")) != -1)
		switch(c) {
			case 'v': // verbosity
			dbg_level = atoi(optarg);
			break;
		}

	fprintf(stderr, "DVB:%d.%d NetProto:%d MsgSize:%d, Debug:0x%x\n", DVB_API_VERSION, DVB_API_VERSION_MINOR, VTUNER_PROTO_MAX, sizeof(vtuner_net_message_t), dbg_level);

	for(i=0; i<MAX_SESSIONS; ++i) session[i].status = SST_IDLE;

	// do real option parsing now
	optind = 1; // reset getopt()
	while((c = getopt(argc, argv, "d:g:hl:p:u:v:")) != -1) {
		switch(c) {
			case 'd': // device list (adap:fe:dmx:dvr)
				{
					session[hw_count].frontend = 0;
					session[hw_count].demux = 0;
					session[hw_count].dvr = 0;
					if(sscanf(optarg, "%d:%d:%d:%d", &session[hw_count].adapter, &session[hw_count].frontend, &session[hw_count].demux, &session[hw_count].dvr) < 1) {
						ALOGW("Device parameter mismatch. At least adapter index is required");
						opt_err++;
						break;
					}
					ALOGD("register device: adapter %d, frontend %d, demux %d, dvr %d",
						session[hw_count].adapter,session[hw_count].frontend,session[hw_count].demux,session[hw_count].dvr);
					hw_count++;
				}
				break;

			case 'v': // verbosity
				break; // already set

			case 'g': // tuner group
				sscanf(optarg, "%i", &tuner_group);
				break;

			case 'l': // listenning ip
				lip = optarg;
				break;

			case 'p': // listenning port
				lport = atoi(optarg);
				break;

			case 'u': // udp log
				{
					char ip[64];
					int pnum;

					if(sscanf(optarg, "%[^:]:%d", ip, &pnum) < 2) {
						ALOGW("Invalid UDP logger parameter: %s", optarg);
						break;
					}
					//open_udplog(ip, pnum);
					ALOGD("UDP logger to %s:%d opened", ip, pnum);
				}
				break;


			case 'h': // help
				fprintf(stderr, "\nCommand line options:\n"
					"    -d devs_list             : adapter[:frontend[:demux[:dvr]]] (default is 0:0:0:0)\n"
					"    -g group_mask            : listen for group members requests only\n"
					"    -l ip_address            : listen on local ip address (default is on ALL)\n"
					"    -p port_number           : listen on local port (default is %d)\n"
					"    -u ip_address:port_number: send message log to udp://ip_address:port_number\n"
					"    -v level                 : verbosity level (1:err,2:warn,3:info,4:debug)\n",
					VTUNER_DISCOVER_PORT);
				exit(1);
				break;
		}

	}
	if(!hw_count && !opt_err) {
		session[0].adapter = 0;
		session[0].frontend = 0;
		session[0].demux = 0;
		session[0].dvr = 0;
		hw_count = 1;
	}

	for(i=0;i<hw_count;++i) {
		vtuner_hw_t hw;
		if(hw_init(&hw, session[i].adapter, session[i].frontend,
					    session[i].demux, session[i].dvr)) {
			session[i].tuner_type = hw.type;
			ALOGI("adapter:%d, frontend:%d, demux,%d, dvr:%d is type:%d",
				 session[i].adapter, session[i].frontend,
				 session[i].demux, session[i].dvr, session[i].tuner_type);
		} else {
			ALOGW("Failed to init adapter:%d, frontend:%d, demux,%d, dvr:%d",
			     session[i].adapter, session[i].frontend,
				 session[i].demux, session[i].dvr);
			session[i].tuner_type = 0x00;
		}
		hw_free(&hw);
	}

	struct sockaddr_in client_so;
	int tuner_type, proto = -1;

	if (init_vtuner_service(lip, lport)) {
		ALOGE("Failed to init listening port '%s:%d'", lip ? : "*.*", lport);
		exit(2);
	}

	while(fetch_request(&client_so, &proto, &tuner_type, &tuner_group)) {
		ALOGI("received discover request proto%d, vtuner_type:%d group:0x%04X", proto, tuner_type, tuner_group);
		for(i=0; i<hw_count; ++i) {
			ALOGD("session status:%d session type:%d tuner_type:%d match:%d",
					  session[i].status, session[i].tuner_type, tuner_type,
					  (session[i].tuner_type & tuner_type) != 0);
			if( (session[i].status == SST_IDLE) &&
			    ((session[i].tuner_type & tuner_type) != 0) )
				break;
		}
		if( i < hw_count) {
			session[i].status = SST_BUSY;
			memcpy((void*)&session[i].client_so, (void*)&client_so, sizeof(client_so));
			pthread_create(&session[i].th, NULL, session_worker, (void*)&session[i]);
		} else {
			ALOGI("No idle device found");
		}
	}

}
