#define LOG_TAG "vtunerc"
#include <cutils/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/dvb/version.h>
#include <time.h>
#include <sys/param.h>
#include <pthread.h>


#include "vtuner-network.h"

#define MSG_ERROR	1
#define MSG_WARN	2
#define MSG_INFO	3
#define MSG_DEBUG	4


#ifndef BUILDVER
#define BUILDVER 0
#endif

#define TS_PKT_LEN 188
#define TS_HDR_SYNC 0x47

int dbg_level =  MSG_INFO;
int use_syslog = 1;
int opt_delay = 50;
// 2010-01-30
// net.ipv4.tcp_wmem max is typically 131072, choosen a value
// below that is a multiple of TS package size
// need to have a buffer greater than this to detect if we're
// receiving to slow
int opt_pktrmax=696;

typedef enum tsdata_worker_status {
  DST_UNKNOWN,
  DST_RUNNING,
  DST_EXITING,
  DST_FAILED
} tsdata_worker_status_t;

typedef struct tsdata_worker_data {
  int in;
  int out;
  tsdata_worker_status_t status;  
} tsdata_worker_data_t;

void *tsdata_worker(void *d) {
  tsdata_worker_data_t* data = (tsdata_worker_data_t*)d;

  data->status = DST_RUNNING;

  // 2010-01-30
  // net.ipv4.tcp_wmem max is typically 131072, choosen a value
  // below that is a multiple of TS packet size
  // need to have a buffer greater than this to detect if we're
  // receiving to slow
  int rmax=opt_pktrmax*TS_PKT_LEN;
  unsigned char buf[(opt_pktrmax+100)*TS_PKT_LEN];
  unsigned char *readptr = buf;
  unsigned char *writeptr = buf;
  int bytesread = 0;
  int bytes2read = sizeof(buf);
  int bytes2write = 0;
  int offset = 0;
  int offsetfound = 0;
  int tail = 0;
  size_t window_size = sizeof(buf);
  setsockopt(data->in, SOL_SOCKET, SO_RCVBUF, (char *) &window_size, sizeof(window_size));

  int delay=opt_delay; //ms to sleep after each write

  while(data->status == DST_RUNNING) {
    struct pollfd pfd[] = { { data->in, POLLIN, 0 } };
    // don't poll forever to catch data->status != DST_RUNNING
    if( poll(pfd, 1, 500) != 0) {
      // we're polling one fd here so we know that reading can't block
      int r = read(data->in, readptr, bytes2read);

      if(r <= 0) {
        ALOGI("TS data socket closed.");
        data->status = DST_EXITING;
	continue;
      }

      bytesread += r;
      bytes2read -= r;
      readptr += r;

      if (bytesread < TS_PKT_LEN * 3) {
        if (dbg_level == MSG_DEBUG) 
			ALOGD("less than 3 TS packets read (actually:%d), read more.", r);
        continue; 
      }

      for (offset = 0, offsetfound = 0; offset < TS_PKT_LEN; offset++) {
        unsigned char *ptr;

        offsetfound = 1; 
        for (ptr = buf; ptr < readptr; ptr += TS_PKT_LEN) {
          if (*(ptr + offset) != TS_HDR_SYNC) {
             offsetfound = 0;
             break;
          }
        }
        if (offsetfound)
          break;
      }

      if (!offsetfound) {
        ALOGI("start of TS packet not found, throwing buffer away.");
        readptr = buf;
        bytesread = 0;
        bytes2read = sizeof(buf);
        tail = 0;
        continue;
      } 

      if (offset)
        ALOGW("offset to TS packet %d bytes.", offset);
      writeptr = buf + offset;
      tail = (bytesread - offset) % TS_PKT_LEN;
      bytes2write = bytesread - offset - tail;

      // 2010-04-03 try to calculate optimal read delay.
      // read to often wastes CPU resources
      // reading to slow delays playback and makes
      // scaning impossible
      // the ideo is to adjust the delay to always receive 
      // packets in the range of 70% - 90% of rmax
    
      if (opt_delay) {
        if( bytesread >= rmax && delay >= 70) {
          delay = 10;
           if (dbg_level == MSG_DEBUG) ALOGD("decreased delay: bytesread:%d rmax:%d, delay:%d", bytesread, rmax, delay);
        } else if( bytesread > 0.90*rmax && delay > 4) {
          if( bytesread > 2*rmax )
            delay = 3;
          else if(delay < 10)
            delay -= 2;
          else if(delay < 20)
            delay -= 4;
          else if(delay < 40)
            delay -= 8;
          else {
	    delay -= 16;
           if (dbg_level == MSG_DEBUG) ALOGD("decreased delay: bytesread:%d rmax:%d, delay:%d", bytesread, rmax, delay);
	   }
        } else if( bytesread < 0.50*rmax && delay < 70) {
          if(delay < 20)
            delay += 2;
          else {
            delay += 4;
           if (dbg_level == MSG_DEBUG) ALOGD("increased delay: bytesread:%d rmax:%d, delay:%d", bytesread, rmax, delay);
	   }
        } 
      }

      if (bytesread <= 0) {
        ALOGI("tcp read - %m");
        data->status = DST_FAILED;
      } else {
        if (write(data->out, writeptr, bytes2write) != bytes2write) {
          ALOGI("write failed - %m");
          data->status = DST_FAILED;
        } else {
          // ALOGD("receive buffer stats. size:%d, rmax:%d, delay:%d", bytes2write, rmax, delay); 
          // suggested from H2Deetoo to prevent pixelation with
          // crypted HD DVB-C channels
          // 2010-01-30
          // wait 100 ms instead of 10 to allow a large chunk of 
          // data to be received in between 
	  if (opt_delay && delay > 0)
            usleep(delay*1000);
        }
        memmove(buf, readptr - tail, tail);
        readptr = buf + tail;
        bytesread = tail;
        bytes2read = sizeof(buf) - tail ;
      }
    } else {
      if (delay > 10) {
         if (dbg_level == MSG_DEBUG) ALOGD("decreased delay: bytesread:%d rmax:%d, delay:%d", bytesread, rmax, delay);
      delay = 10; 
  }
    }
  }

  ALOGI("TS data copy thread terminated.");
  data->status = DST_EXITING;
  return 0;
}

typedef enum discover_worker_status {
  DWS_IDLE,
  DWS_RUNNING,
  DWS_DISCOVERD,
  DWS_FAILED
} discover_worker_status_t;

typedef struct discover_worker_data {
  __u32 types;
  struct sockaddr_in server_addr;
  discover_worker_status_t status;
  vtuner_net_message_t msg;
  char *direct_ip;
  __u32 groups;
  unsigned short port;
} discover_worker_data_t;

int *discover_worker(void *d) {
  discover_worker_data_t* data = (discover_worker_data_t*)d;

  ALOGI("starting discover thread");
  data->status = DWS_RUNNING;
  int discover_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int broadcast = -1;
  if(!data->direct_ip)
    setsockopt(discover_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)); 
  
  struct sockaddr_in discover_addr;
  memset(&discover_addr, 0, sizeof(discover_addr));
  discover_addr.sin_family = AF_INET;
  discover_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  discover_addr.sin_port = 0;

  if (bind(discover_fd, (struct sockaddr *) &discover_addr, sizeof(discover_addr)) < 0) {
    ALOGI("can't bind discover socket - %m");
    close(discover_fd);
    data->status = DWS_FAILED;
    goto discover_worker_end;
  } 

  struct sockaddr_in  msg_addr;
  msg_addr.sin_family = AF_INET;
  if(data->direct_ip) {
    struct in_addr dirip;
    if(!inet_aton(data->direct_ip, &msg_addr.sin_addr)) {
      ALOGI("can't parse direct ip: %s", data->direct_ip);
      close(discover_fd);
      data->status = DWS_FAILED;
      goto discover_worker_end;
    }
  } else
    msg_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
  msg_addr.sin_port = htons(data->port);

  int timeo = 300;
  do {

    memset(&data->msg, 0, sizeof(data->msg));
    data->msg.msg_type = MSG_DISCOVER;
    data->msg.u.discover.vtype = data->types;
    data->msg.u.discover.tuner_group = data->groups;
    data->msg.u.discover.port = 0;
    hton_vtuner_net_message(&data->msg, 0); // we don't care tuner type for conversion of discover

    ALOGI("Sending %sdiscover message for device types %x", data->direct_ip ? "direct " : "", data->types);
    sendto(discover_fd, &data->msg, sizeof(data->msg), 0, (struct sockaddr *) &msg_addr, sizeof(msg_addr));
    struct pollfd pfd[] = { { discover_fd, POLLIN, 0 } }; 
    while( data->msg.u.discover.port == 0 &&
           poll(pfd, 1, timeo) == 1 ) {
      int server_addrlen = sizeof(data->server_addr);
      recvfrom( discover_fd,  &data->msg, sizeof(data->msg), 0, (struct sockaddr *)&data->server_addr, &server_addrlen );
    }
    if( timeo < 10000) timeo *= 2;
  } while (data->msg.u.discover.port == 0); 

  data->server_addr.sin_port = data->msg.u.discover.port; // no need for ntoh here
  ntoh_vtuner_net_message(&data->msg, 0);
  
  ALOGI("Received discover message from %s proto%d control %d data %d", inet_ntoa(data->server_addr.sin_addr), data->msg.ver, data->msg.u.discover.port, data->msg.u.discover.tsdata_port);
  data->status = DWS_DISCOVERD;

discover_worker_end:
  close(discover_fd);
  return 0;
}

typedef enum vtuner_status {
  VTS_DISCONNECTED,
  VTS_DISCOVERING,
  VTS_CONNECTED
} vtuner_status_t;

// dvb_frontend_info for DVB-S2
struct dvb_frontend_info fe_info_dvbs2 = {
  .name                  = "vTuner DVB-S2",
  .type                  = 0,
  .frequency_min         = 925000,
  .frequency_max         = 2175000,
  .frequency_stepsize    = 125000,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 1000000,
  .symbol_rate_max       = 45000000,
  .symbol_rate_tolerance = 0,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_RECOVER //0x400006ff
};


struct dvb_frontend_info fe_info_dvbs = {
  .name                  = "vTuner DVB-S",
  .type                  = 0,
  .frequency_min         = 950000,
  .frequency_max         = 2150000,
  .frequency_stepsize    = 125,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 1000000,
  .symbol_rate_max       = 45000000,
  .symbol_rate_tolerance = 500,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO | FE_CAN_QPSK //0x6af
};

struct dvb_frontend_info fe_info_dvbc = {
  .name                  = "vTuner DVB-C",
  .type                  = 1,
  .frequency_min         = 55000000,
  .frequency_max         = 862000000,
  .frequency_stepsize    = 0,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 451875,
  .symbol_rate_max       = 7230000,
  .symbol_rate_tolerance = 0,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 | FE_CAN_QAM_128 | FE_CAN_QAM_256 | FE_CAN_RECOVER
};

struct dvb_frontend_info fe_info_dvbt = {
  .name                  = "vTuner DVB-T",
  .type                  = 2,
  .frequency_min         = 51000000,
  .frequency_max         = 858000000,
  .frequency_stepsize    = 166667,
  .frequency_tolerance   = 0,
  .symbol_rate_min       = 0,
  .symbol_rate_max       = 0,
  .symbol_rate_tolerance = 0,
  .notifier_delay        = 0,
  .caps                  = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO | FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO | FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO //0xb2eaf
};

#define MAX_NUM_VTUNER_MODES 3

int main(int argc, char **argv) {

  //openlog("vtunerc", LOG_PERROR, LOG_USER);

  int modes, mode;
  char ctypes[MAX_NUM_VTUNER_MODES][32] = {"none", "none", "none"};
  struct dvb_frontend_info* vtuner_info[MAX_NUM_VTUNER_MODES];
  int types[MAX_NUM_VTUNER_MODES];
  char *pext, direct_ip[128];
  unsigned int groups = VTUNER_GROUPS_ALL;
  unsigned int discover_port = VTUNER_DISCOVER_PORT;
  int argadd = 0;
  int vtuner_control;
  struct stat st;
  char *ctrl_devname = VTUNER_CTRL_DEVNAME;
  int c;
  char *act;
  FILE *pidfile;
  char pidname[MAXPATHLEN], *piddir = "/data/misc";
  int ctrl_idx = 0;
  struct hostent *he;
  struct in_addr **addr_list;

  mode = 0;
  modes = 0;
  direct_ip[0] = '\0';

  fprintf(stderr, "vtuner client (vtunerc), part of vtuner project\n"
			"Visit http://code.google.com/p/vtuner/ for more information\n"
			"Copyright (C) 2009-11 Roland Mieslinger\n"
			"This is free software; see the source for copying conditions.\n"
			"There is NO warranty; not even for MERCHANTABILITY or FITNESS\n"
			"FOR A PARTICULAR PURPOSE.\n");

  // read only verbosity, to show value
  while((c = getopt(argc, argv, "d:f:n:r:x:hv:")) != -1)
    switch(c) {
    case 'v': // verbosity
      dbg_level = atoi(optarg);
      break;
    }

 fprintf(stderr, "Revision:%s%s DVB:%d.%d allow:%d.x NetProto:%d MsgSize:%d, Debug:%d\n", BUILDVER, MODFLAG, DVB_API_VERSION, DVB_API_VERSION_MINOR, HAVE_DVB_API_VERSION, VTUNER_PROTO_MAX, sizeof(vtuner_net_message_t), dbg_level);

  // do real option parsing now
  optind = 1; // reset getopt()
  while((c = getopt(argc, argv, "d:f:n:r:x:hv:")) != -1) {
    switch(c) {
    case 'd': // device name
      if(!stat(optarg, &st) && S_ISCHR(st.st_mode))
        ctrl_devname = optarg;
      else {
         fprintf(stderr,"can't open control device: %s", optarg);
        exit(1);
      }
      ctrl_idx = ctrl_devname[strlen(ctrl_devname) - 1] - '0'; //FIXME: it works "only" for first ten devices
      break;

    case 'f': // frontends type (eg: s2,c,t)
      act = optarg;
      do {
        char *msk;
        char *nxt = strchr(act, ',');
	if(nxt)
	  *nxt = '\0';
        if((msk = strchr(act, ':')))
	  *msk = '\0';

	// tuner type parsing
        if(strcmp(act,"s2")==0) {
          types[modes] = VT_S | VT_S2;
          vtuner_info[modes] = &fe_info_dvbs2;
          strncpy(ctypes[modes],"DVB-S2",sizeof(ctypes[0]));
        } else if(strcmp(act,"S2")==0) {
          types[modes] = VT_S2;
          vtuner_info[modes] = &fe_info_dvbs2;
          strncpy(ctypes[modes],"DVB-S2",sizeof(ctypes[0]));
        } else if(strcmp(act,"s")==0) {
          types[modes] = VT_S | VT_S2;
          vtuner_info[modes] = &fe_info_dvbs;
          strncpy(ctypes[modes],"DVB-S",sizeof(ctypes[0]));
        } else if(strcmp(act,"S")==0) {
          types[modes] = VT_S;
          vtuner_info[modes] = &fe_info_dvbs;
          strncpy(ctypes[modes],"DVB-S",sizeof(ctypes[0]));
        } else if(strcmp(act,"C")==0) {
          types[modes] = VT_C;
          vtuner_info[modes] = &fe_info_dvbc;
          strncpy(ctypes[modes],"DVB-C",sizeof(ctypes[0]));
        } else if(strcmp(act,"T")==0) {
          types[modes] = VT_T;
          vtuner_info[modes] = &fe_info_dvbt;
          strncpy(ctypes[modes],"DVB-T",sizeof(ctypes[0]));
        } else {
           fprintf(stderr,"unknown tuner mode specified: %s allowed values are: -s -S -s2 -S2 -c -t (with optional group mask)", optarg);
          exit(1);
        }

	// tuner mask parsing
	if(msk) {
	  if(sscanf(msk + 1, "%i", &groups) < 1)
	    groups = VTUNER_GROUPS_ALL;
	}

	act = nxt;
	if(nxt)
	  act++;
        fprintf(stderr,"added frontend mode %s as mode %d, searching for tuner types %x", ctypes[modes], modes, types[modes]);
        modes++;
#ifndef HAVE_DREAMBOX_HARDWARE
	if(modes > 0) {
	  if(modes > 1)
            ALOGW("frontend switch: only one mode is allowed. First is used");
	  break; // only one mode is allowed for opensourced vtunerc.ko
	}
#endif
      } while (act);
      break;

    case 'n': // network: direct connection
      if((c = sscanf(optarg, "%[^:]:%d", direct_ip, &discover_port)) < 1)
      //if(strchr(optarg, ':'))
      {
         ALOGI("Network parameter requires at least IP address or hostname (rv=%d)", c);
         exit(1);
      }
      if((he = gethostbyname(direct_ip)) == NULL) {
        ALOGI("Failed to parse IP/hostname: %s", direct_ip);
        exit(1);
      }
      addr_list = (struct in_addr **)he->h_addr_list;
      strcpy(direct_ip, inet_ntoa(*addr_list[0]));
      ALOGI("direct connection: host='%s' port=%d", direct_ip, discover_port);
      break;

    case 'x': // calc delay
      opt_delay = atoi(optarg);
      break;

    case 'r': // rbuf_size
      opt_pktrmax = atoi(optarg);
      break;

    case 'v': // verbosity
      dbg_level = atoi(optarg);
      break;

    case 'h': // help
      fprintf(stderr, "Command line options:\n"
                      "  Required:\n"
#ifdef HAVE_DREAMBOX_HARDWARE
                      "    -f dvb_type[:tuner_mask][,<dvb_type_2>[:tuner_mask_2][,dvb_type_3[:tuner_mask_3]]] : tuner type to ask (dvb_type=S,s,S2,s2,T,C) and optional tuner group mask (every bit represents one tuner group)\n"
#else
                      "    -f dvb_type[:tuner_mask] : tuner type to ask (dvb_type=S,S2,T,C) and optional tuner group mask (every bit represents one tuner group)\n"
#endif
                      "  Optional:\n"
                      "    -d dev_name              : path to controlling device (usually /dev/vtunerc0)\n"
                      "    -n hostname[:port]       : do direct request for tuner (multicast by default)\n"
                      "    -r rbuf_size             : receive buffer size\n"
                      "    -x max_delay             : max delay or 0\n"
                      "    -v level                 : verbosity level (1:err,2:warn,3:info,4:debug)\n");
      exit(1);
      break;
    }
  }

  if(modes == 0) {
    fprintf(stderr,"Missing required -f parameter");
    exit(1);
  }

  // create pidfile
  sprintf(pidname, "%s/vtunerc%d.pid", piddir, ctrl_idx);
  if ((pidfile = fopen(pidname, "r")) != NULL) {
    // check for pid alive
    int pid;

    if(fscanf(pidfile, "%d", &pid) == 1 && kill(pid, 0) == 0) {
      ALOGI("Process with PID %d still accupies control device %s", pid, ctrl_devname);
      exit(1);
    }
    fclose(pidfile);
  }
  if ((pidfile = fopen(pidname, "w")) == NULL) {
      ALOGW("Can't create pid file %s - %m", ctrl_devname);
  } else {
    fprintf(pidfile, "%d", getpid());
    fclose(pidfile);
    ALOGI("Created pidfile %s", pidname);
  }

  vtuner_control = open(ctrl_devname, O_RDWR);
  if (vtuner_control < 0) {
    perror(ctrl_devname);
    return 1;
  }

  if (ioctl(vtuner_control, VTUNER_SET_NAME, "vTuner"))  {
    ALOGI("VTUNER_SET_NAME failed - %m");
    exit(1);
  }

#ifdef HAVE_DREAMBOX_HARDWARE
  if (ioctl(vtuner_control, VTUNER_SET_HAS_OUTPUTS, "no")) {
    ALOGI("VTUNER_SET_HAS_OUTPUTS failed - %m");
    exit(1);
  }

  int f;
  f = open("/proc/stb/info/model",O_RDONLY);
  if(f>0) {

    char model[20];
    int len;
    len = read(f, &model, sizeof(model)-1); 
    close(f);
    model[len] = 0;
    ALOGI("Box is a %s", model);
  }
#endif

  discover_worker_data_t dsd;
  dsd.status = DWS_IDLE;
  dsd.direct_ip = strlen(direct_ip) ? strdup(direct_ip) : NULL;
  dsd.groups = groups;
  dsd.port = discover_port;
  vtuner_status_t vts = VTS_DISCONNECTED;
  long values_received = 0;
  vtuner_update_t values;
  int vfd;

  #define RECORDLEN 5
  vtuner_net_message_t record[RECORDLEN]; // SET_TONE, SET_VOLTAGE, SEND_DISEQC_MSG, MSG_PIDLIST
  memset(&record, 0, sizeof(vtuner_net_message_t)*RECORDLEN);

  while(dsd.status != DWS_FAILED) {
    pthread_t dwt, dst;
    tsdata_worker_data_t dwd;
    vtuner_net_message_t msg;

    if( vts == VTS_DISCONNECTED ) {
      ALOGI("no server connected. discover thread is %d (DWS_IDLE:%d, DWS_RUNNING:%d)", dsd.status, DWS_IDLE, DWS_RUNNING);
      if( dsd.status == DWS_IDLE ) {
        ALOGI("changing frontend mode to %s", ctypes[mode]);
#ifndef HAVE_DREAMBOX_HARDWARE
        if (ioctl(vtuner_control, VTUNER_SET_TYPE, ctypes[0])) {
          ALOGI("VTUNER_SET_TYPE failed - %m");
          exit(1);
        }
#else
	if( modes == 1 ) {
          if (ioctl(vtuner_control, VTUNER_SET_TYPE, ctypes[0])) {
            ALOGI("VTUNER_SET_TYPE failed - %m");
            exit(1);
          }
	} else {
          if( ioctl(vtuner_control, VTUNER_SET_NUM_MODES, modes) ) {
            ALOGI("VTUNER_SET_NUM_MODES( %d ) failed - %m", modes);
            exit(1);
          }
          if( ioctl(vtuner_control, VTUNER_SET_MODES, ctypes) ) {
            ALOGI(" VTUNER_SET_MODES failed( %s, %s, %s ) - %m", ctypes[0], ctypes[1], ctypes[2]);
            exit(1);
          }
	}
#endif

        if (ioctl(vtuner_control, VTUNER_SET_FE_INFO, vtuner_info[mode])) {
          ALOGI("VTUNER_SET_FE_INFO failed - %m");
          exit(1);
        }

        ALOGI("Start discover worker for device type %x groups %x", types[mode], groups);
        dsd.types = types[mode];
        dsd.status = DWS_RUNNING;
        pthread_create( &dst, NULL, discover_worker, &dsd);
        vts = VTS_DISCOVERING;
      }
    }

    if( vts == VTS_DISCOVERING ) {
      if(dsd.status == DWS_DISCOVERD) {

        // pthread_join(&dst, NULL); // wait till discover threads finish, if hasn't
        dsd.status = DWS_IDLE;    // now it's sure to be idle

        vfd = socket(PF_INET, SOCK_STREAM, 0);
        if(vfd<0) {
          ALOGI("Can't create server message socket - %m");
          exit(1);
        }
        
        ALOGI("connect control socket to %s:%d", inet_ntoa(dsd.server_addr.sin_addr), ntohs(dsd.server_addr.sin_port));
        if(connect(vfd, (struct sockaddr *)&dsd.server_addr, sizeof(dsd.server_addr)) < 0) {
          ALOGI("Can't connect to server control socket - %m");
          vts = VTS_DISCONNECTED;
          close(vfd);

        } else {
          
          dsd.server_addr.sin_port = htons(dsd.msg.u.discover.tsdata_port);
          dwd.in = socket(PF_INET, SOCK_STREAM, 0);
          if( connect(dwd.in, (struct sockaddr *)&dsd.server_addr, sizeof(dsd.server_addr)) < 0) {
            ALOGI("Can't connect to server data socket -%m");
            close(vfd);
            close(dwd.in);
            vts = VTS_DISCONNECTED;
          } else {

          ALOGI("connected data socket to %s:%d", inet_ntoa(dsd.server_addr.sin_addr), ntohs(dsd.server_addr.sin_port));
            dwd.out = vtuner_control;
            dwd.status = DST_UNKNOWN; 
            pthread_create( &dwt, NULL, tsdata_worker, &dwd ); 
            vts = VTS_CONNECTED;

            // send null message to fully open connection;
            msg.msg_type = MSG_NULL;
            hton_vtuner_net_message( &msg, types[mode] );
            write(vfd, &msg, sizeof(msg));
            read(vfd, &msg, sizeof(msg));

            int i;
            for(i=0; i<RECORDLEN; ++i) {
              if(record[i].msg_type != 0) {
                memcpy(&msg, &record[i], sizeof(msg));
                hton_vtuner_net_message( &msg, types[mode] );
                if(write(vfd, &msg, sizeof(msg))>0) {
                   if (dbg_level == MSG_DEBUG) ALOGD("replay message %d", i);
                  if(record[i].msg_type != MSG_PIDLIST) {
                    if(read(vfd, &msg, sizeof(msg))>0) {
                       if (dbg_level == MSG_DEBUG) ALOGD("got response for message %d", i);
                    }
                  }
                }
              }
            }
            
          }
        }

      }
    }

    int nrp = 2;
    struct pollfd pfd[] = { { vtuner_control, POLLPRI, 0 }, {vfd, POLLIN, 0} };
    if( vts != VTS_CONNECTED) nrp = 1; // don't poll for vfd if not connected
    poll(pfd, nrp, 100); // don't poll forever cause of status changes can happen

    if(pfd[0].revents & POLLPRI) {
       if (dbg_level == MSG_DEBUG) ALOGD("vtuner message!");
      if (ioctl(vtuner_control, VTUNER_GET_MESSAGE, &msg.u.vtuner)) {
        ALOGI("VTUNER_GET_MESSAGE- %m");
        exit(1);
      }
      // we need to save to msg_type here as hton works in place
      // so it's not save to access msg_type afterwards
      int msg_type = msg.msg_type = msg.u.vtuner.type;

      if(msg_type == MSG_TYPE_CHANGED) {
        // this msg isn't forwarded to the server, instead
        // we have to change the frontend type and discover
        // another server on the network

        mode = msg.u.vtuner.body.type_changed;
        msg.u.vtuner.type = 0;
        if (ioctl(vtuner_control, VTUNER_SET_RESPONSE, &msg.u.vtuner)) {
          ALOGI("VTUNER_SET_RESPONSE - %m");
          exit(1);
        }
        // empty all recorded reconnecion information
        memset(&record, 0, sizeof(vtuner_net_message_t)*RECORDLEN);

	// disconnect from server
        vts = VTS_DISCONNECTED;
        close(vfd);

        // if disvoery is ongoing cancle it
        if(dsd.status == DWS_RUNNING) {
          ALOGW("discover thread is running, try to cancle it.");
          pthread_kill(dst, 0);
          pthread_join(dst, NULL);	  
          ALOGW("discover thread terminated.");
          dsd.status = DWS_IDLE;
        }
         if (dbg_level == MSG_DEBUG) ALOGD("msg: %d completed", msg_type);

      } else {
        // fill the record array in the correct order
        int recordnr = -1;
        switch(msg.u.vtuner.type) {
          case MSG_SET_FRONTEND:     recordnr=3; break;
          case MSG_SET_TONE:         recordnr=1; break;
          case MSG_SET_VOLTAGE:      recordnr=2; break;
          case MSG_SEND_DISEQC_MSG:  recordnr=0; break;
          case MSG_PIDLIST:          recordnr=4; break;
        }

        if( recordnr != -1 ) {
           memcpy(&record[recordnr], &msg, sizeof(msg));
           // this is a "state changeing" msg, make cache expired
           values_received = 0;
        }

        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long now=t.tv_sec*1000 + t.tv_nsec/1000000;

        int dontsend = ( now - values_received < 1000 ) && 
                       ( vts == VTS_CONNECTED ) &&
                       ( msg_type == MSG_READ_STATUS || msg_type == MSG_READ_BER || 
                         msg_type == MSG_READ_SIGNAL_STRENGTH || msg_type == MSG_READ_SNR || 
                         msg_type == MSG_READ_UCBLOCKS );

        if( vts == VTS_CONNECTED && ! dontsend )  {
          hton_vtuner_net_message( &msg, types[mode] );
          write(vfd, &msg, sizeof(msg));
        }

        if( dontsend ) {
          switch(msg_type) {
            case MSG_READ_STATUS:          msg.u.vtuner.body.status = values.status; break;
            case MSG_READ_BER:             msg.u.vtuner.body.ber    = values.ber; break;
            case MSG_READ_SIGNAL_STRENGTH: msg.u.vtuner.body.ss     = values.ss; break;
            case MSG_READ_SNR:             msg.u.vtuner.body.snr    = values.snr; break; 
            case MSG_READ_UCBLOCKS:        msg.u.vtuner.body.ucb    = values.ucb; break;
          }
           if (dbg_level == MSG_DEBUG) ALOGD("cached values are up to date");
        }

        if (msg_type != MSG_PIDLIST) {
          if( vts == VTS_CONNECTED ) {
            if( ! dontsend ) {
              read(vfd, &msg, sizeof(msg));
              ntoh_vtuner_net_message( &msg, types[mode] );
            }
          } else {
             if (dbg_level == MSG_DEBUG) ALOGD("fake server answer");
            switch(msg.u.vtuner.type) {
              case MSG_READ_STATUS:  
                msg.u.vtuner.body.status = 0; // tuning failed
                break; 
            }
            msg.u.vtuner.type = 0; // report success
          }
          if (ioctl(vtuner_control, VTUNER_SET_RESPONSE, &msg.u.vtuner)) {
            ALOGI("VTUNER_SET_RESPONSE - %m");
            exit(1);
          }
        }
         if (dbg_level == MSG_DEBUG) ALOGD("msg: %d completed", msg_type);
      }
    }
         
    if( pfd[1].revents & POLLIN ) {
      int rlen = read(vfd, &msg, sizeof(msg));
      if(rlen == 0) {
        ALOGI("Server disconncted");
        vts = VTS_DISCONNECTED;        
        close(vfd);
      } 
      if( rlen == sizeof(msg)) {
        ntoh_vtuner_net_message( &msg, types[mode] );
        switch(msg.msg_type) {
          case MSG_UPDATE: {
            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);
            values_received=t.tv_sec*1000 + t.tv_nsec/1000000;
            memcpy(&values, &msg.u.update, sizeof(values));
            break;
          }
        } 
      }
    }
  }

  return 0;
}
