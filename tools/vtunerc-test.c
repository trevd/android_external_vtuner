/*
 * this can be used to simulate a vtuner client
 * It only works with Astra 19.2 
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>

#include "vtuner-network.h"

#define PVR_FLUSH_BUFFER    0
#define VTUNER_GET_MESSAGE  1
#define VTUNER_SET_RESPONSE 2
#define VTUNER_SET_NAME     3
#define VTUNER_SET_TYPE     4
#define VTUNER_SET_HAS_OUTPUTS 5
#define VTUNER_SET_FE_INFO 6
int dbg_level = 2;

#ifdef DEBUG_MAIN
#define DEBUG(msg, ...) fprintf(stderr,"[%d %s:%u] debug: " msg, getpid(), __FILE__, __LINE__, ## __VA_ARGS__)
#define DEBUGC(msg, ...) fprintf(stderr,msg, ## __VA_ARGS__)
#else
#define DEBUG(msg, ...)
#define DEBUGC(msg, ...)
#endif

typedef enum tsdata_worker_status {
  DST_UNKNOWN,
  DST_RUNNING,
  DST_EXITING
} tsdata_worker_status_t;

typedef struct tsdata_worker_data {
  int in;
  int out;
  tsdata_worker_status_t status;  
} tsdata_worker_data_t;

void *tsdata_worker(void *d) {
  tsdata_worker_data_t* data = (tsdata_worker_data_t*)d;

  data->status = DST_RUNNING;

  unsigned char buf[4096*188];
  int bufptr = 0, bufptr_write = 0;

  while(data->status == DST_RUNNING) {
    struct pollfd pfd[] = { { data->in, 0, 0 }, { data->out, 0, 0 } };
    int can_read = (sizeof(buf) - bufptr) > 1500;
    int can_write = ( bufptr - 65536 > bufptr_write);
    if (can_read) pfd[0].events |= POLLIN;
    if (can_write) pfd[1].events |= POLLOUT;
    poll(pfd, 2, 1000);  // don't poll forever to catch data->status != DST_RUNNING

    if (pfd[0].revents & POLLIN) {
      int r = read(data->in, buf + bufptr, sizeof(buf) - bufptr);
      if (r <= 0) {
        WARN("udp read: %m\n");
      } else {
        bufptr += r;
      }
    }

    if (pfd[1].revents & POLLOUT) {
      int w = bufptr - bufptr_write;
      if (write(data->out, buf + bufptr_write, w) != w) {
        ERROR("write failed - %m");
        exit(1);
      }
      bufptr_write += w;
      if (bufptr_write == bufptr) bufptr_write = bufptr = 0;
    }

  }
  data->status = DST_EXITING;
}

int main(int argc, char **argv)
{
	int type;
	char ctype[7];
        type = VT_S; 
	strncpy(ctype,"DVB-S2",sizeof(ctype));
       	INFO("Simulating a %s tuner\n", ctype); 

	struct sockaddr_in server_so;
        socklen_t serverlen = sizeof(server_so);

        int udpsock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        struct sockaddr_in dataaddr;

        memset(&dataaddr, 0, sizeof(dataaddr));
        dataaddr.sin_family = AF_INET;
        dataaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        dataaddr.sin_port = htons(0x9988);
        if (bind(udpsock, (struct sockaddr *) &dataaddr, sizeof dataaddr) < 0)
        {
                ERROR("bind: %m\n");
                return 1;
        }

        vtuner_net_message_t msg;
	struct sockaddr_in  msg_so;
	msg_so.sin_family = AF_INET;
	msg_so.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	msg_so.sin_port = htons(0x9989);
	msg.msg_type = MSG_DISCOVER;
	msg.u.discover.fe_info.type = type;
	msg.u.discover.port = 0;
	int broadcast = -1;
	setsockopt(udpsock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
	hton_vtuner_net_message(&msg, 0);
	sendto(udpsock, &msg, sizeof(msg), 0, (struct sockaddr *) &msg_so, sizeof(msg_so));

        while(msg.u.discover.port == 0)  {
          INFO("Waiting to receive autodiscovery packet.\n"); 
          if( recvfrom( udpsock,  &msg, sizeof(msg), 0, (struct sockaddr *)&server_so, &serverlen ) > 0 && msg.u.discover.port != 0 ) { 
            INFO("Received discover message from %s:%d\n", inet_ntoa(server_so.sin_addr), ntohs(server_so.sin_port));
          }
        }

        int vfd = socket(PF_INET, SOCK_STREAM, 0);
	if(vfd<0) {
          ERROR("socket - %m\n");
          exit(1);
        }

        server_so.sin_port = msg.u.discover.port;
        INFO("connect to %s:%d\n", inet_ntoa(server_so.sin_addr), ntohs(server_so.sin_port));
        if(connect(vfd, (struct sockaddr *)&server_so, serverlen) < 0) {
	  ERROR("connect - %m\n");
        }

        // send empty message to fully open connection
        msg.msg_type = MSG_NULL;
        write(vfd, &msg, sizeof(msg));
        read(vfd, &msg, sizeof(msg));

	unsigned char buf[4096*188];
	int bufptr = 0, bufptr_write = 0;

        tsdata_worker_data_t dwd;
        dwd.in = udpsock;
        dwd.out = open("vtunerc-test.ts", O_RDWR|O_CREAT);
        dwd.status = DST_UNKNOWN;
        pthread_t dwt;
        pthread_create( &dwt, NULL, tsdata_worker, &dwd );
	
  int i;

  INFO("MSG_SEND_DISEQC_MSG\n");
  msg.msg_type = msg.u.vtuner.type = MSG_SEND_DISEQC_MSG;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
  ntoh_vtuner_net_message( &msg, type );
  if(msg.u.vtuner.type != 0) WARN("MSG_SEND_DISEQC_MSG failed\n");

  INFO("MSG_SEND_DISEQC_BURST\n");
  msg.msg_type = msg.u.vtuner.type = MSG_SEND_DISEQC_BURST;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
  ntoh_vtuner_net_message( &msg, type );
  if(msg.u.vtuner.type != 0) WARN("MSG_SEND_DISEQC_BURST failed\n");

  INFO("MSG_SET_VOLTAGE\n");
  msg.msg_type = msg.u.vtuner.type = MSG_SET_VOLTAGE;
  msg.u.vtuner.body.voltage = SEC_VOLTAGE_18;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
  ntoh_vtuner_net_message( &msg, type );
  if(msg.u.vtuner.type != 0) WARN("MSG_SET_VOLTAGE failed %d\n",msg.u.vtuner.type);

  INFO("MSG_SET_TONE\n");
  msg.msg_type = msg.u.vtuner.type = MSG_SET_TONE;
  msg.u.vtuner.body.voltage = SEC_TONE_ON;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
  ntoh_vtuner_net_message( &msg, type );
  if(msg.u.vtuner.type != 0) WARN("MSG_SET_TONE failed %d\n",msg.u.vtuner.type);

  INFO("MSG_SET_FRONTEND\n");
  msg.msg_type = msg.u.vtuner.type = MSG_SET_FRONTEND;
  msg.u.vtuner.body.fe_params.frequency = 1236000;
  msg.u.vtuner.body.fe_params.inversion = INVERSION_AUTO ;
  msg.u.vtuner.body.fe_params.u.qpsk.symbol_rate = 27500000;
  msg.u.vtuner.body.fe_params.u.qpsk.fec_inner = FEC_AUTO;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
  ntoh_vtuner_net_message( &msg, type );
  if(msg.u.vtuner.type != 0) WARN("MSG_SET_FRONTEND failed %d\n",msg.u.vtuner.type);

  do {
    usleep(1000000);
    msg.msg_type = msg.u.vtuner.type = MSG_READ_STATUS;
    hton_vtuner_net_message( &msg, type );
    if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
    if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
    ntoh_vtuner_net_message( &msg, type );
    INFO("MSG_READ_STATUS %x\n", msg.u.vtuner.body.status);
  } while (msg.u.vtuner.body.status < 0x1f);  

  INFO("MSG_PIDLIST PAT\n");
  msg.msg_type = msg.u.vtuner.type = MSG_PIDLIST;
  for(i=0; i<30; ++i)  msg.u.vtuner.body.pidlist[i] = 0xffff;
  i=0;
  msg.u.vtuner.body.pidlist[i++] = 0;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  usleep(10000000);

  INFO("MSG_PIDLIST Audio & Video\n");
  msg.msg_type = msg.u.vtuner.type = MSG_PIDLIST;
  for(i=0; i<30; ++i)  msg.u.vtuner.body.pidlist[i] = 0xffff;
  i=0;
  msg.u.vtuner.body.pidlist[i++] = 0;
  msg.u.vtuner.body.pidlist[i++] = 0x12;
  msg.u.vtuner.body.pidlist[i++] = 0x14;
  msg.u.vtuner.body.pidlist[i++] = 101;
  msg.u.vtuner.body.pidlist[i++] = 102;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  usleep(10000000);

  INFO("MSG_PIDLIST\n");
  msg.msg_type = msg.u.vtuner.type = MSG_PIDLIST;
  for(i=0; i<30; ++i)  msg.u.vtuner.body.pidlist[i] = 0xffff;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");

  INFO("MSG_SET_FRONTEND\n");
  msg.msg_type = msg.u.vtuner.type = MSG_SET_FRONTEND;
  msg.u.vtuner.body.fe_params.frequency = 1353000;
  msg.u.vtuner.body.fe_params.inversion = INVERSION_AUTO ;
  msg.u.vtuner.body.fe_params.u.qpsk.symbol_rate = 27500000;
  msg.u.vtuner.body.fe_params.u.qpsk.fec_inner = FEC_AUTO;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
  ntoh_vtuner_net_message( &msg, type );
  if(msg.u.vtuner.type != 0) WARN("MSG_SET_FRONTEND failed %d\n",msg.u.vtuner.type);

  do {
    usleep(1000000);
    msg.msg_type = msg.u.vtuner.type = MSG_READ_STATUS;
    hton_vtuner_net_message( &msg, type );
    if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
    if(read(vfd, &msg, sizeof(msg)) <=0) WARN("read failed -%m\n");
    ntoh_vtuner_net_message( &msg, type );
    INFO("MSG_READ_STATUS %x\n", msg.u.vtuner.body.status);
  } while (msg.u.vtuner.body.status < 0x1f);

    INFO("MSG_PIDLIST\n");
  msg.msg_type = msg.u.vtuner.type = MSG_PIDLIST;
  for(i=0; i<30; ++i)  msg.u.vtuner.body.pidlist[i] = 0xffff;
  i=0;
  msg.u.vtuner.body.pidlist[i++] = 0;
  msg.u.vtuner.body.pidlist[i++] = 0x12;
  msg.u.vtuner.body.pidlist[i++] = 0x14;
  msg.u.vtuner.body.pidlist[i++] = 110;
  msg.u.vtuner.body.pidlist[i++] = 120;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");
  usleep(10000000);

  INFO("MSG_PIDLIST\n");
  msg.msg_type = msg.u.vtuner.type = MSG_PIDLIST;
  for(i=0; i<30; ++i)  msg.u.vtuner.body.pidlist[i] = 0xffff;
  hton_vtuner_net_message( &msg, type );
  if(write(vfd, &msg, sizeof(msg))<=0) WARN("write failed - %m\n");

  INFO("waiting for thread to exit.\n");
  dwd.status = DST_EXITING;
  pthread_join( dwt, NULL );
}
