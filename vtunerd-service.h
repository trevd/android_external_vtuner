#ifndef _VTUNERDSERVICE_H_
#define _VTUNERDSERVICE_H_

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <unistd.h>

#include "vtuner-network.h"

#if HAVE_DVB_API_VERSION < 3
  #include "vtuner-dmm-2.h"
#else
  #ifdef HAVE_DREAMBOX_HARDWARE
    #include "vtuner-dmm-3.h"
  #else
    #include "vtuner-dvb-3.h"
  #endif
#endif

#define DEBUGSRV(msg, ...) DEBUG(MSG_SRV, msg, ## __VA_ARGS__)

typedef enum vtuner_session_status {
	SST_IDLE,
	SST_BUSY
} vtuner_session_status_t;

int init_vtuner_service(char *, unsigned short);
int fetch_request(struct sockaddr_in*, int*, int*, int*);
int run_worker(int, int, int, int, struct sockaddr_in*);

#endif
