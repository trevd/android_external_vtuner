#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <cutils/log.h>

#include "vtunerd-service.h"
#include "vtuner-utils.h"

#define xstr(s) str(s)
#define str(s) #s

static unsigned short discover_port = VTUNER_DISCOVER_PORT;
static int discover_fd = 0;

static unsigned short listen_port = VTUNER_DISCOVER_PORT;
static unsigned long listen_ip = INADDR_ANY;

int init_vtuner_service(char *ip, unsigned short port) {
	struct sockaddr_in discover_so;
	int rv;

	if( discover_fd ) {
		ALOGV("autodiscover socket already bound");
		return 0;
	}

	if( ip && strlen(ip) ) {
		unsigned long nip;
  		inet_aton(ip, &nip);
		if( nip )
			listen_ip = ntohl(nip);
	}
	if( port )
		listen_port = port;

	memset(&discover_so, 0, sizeof(discover_so));
	discover_so.sin_family = AF_INET;
	discover_so.sin_addr.s_addr = htonl(listen_ip);
	discover_so.sin_port = htons(listen_port);
	discover_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if(( rv = bind(discover_fd, (struct sockaddr *) &discover_so, sizeof(discover_so))) < 0) {
		ALOGE("failed to bind autodiscover socket %s:%d - %m", ip ? : "*.*", listen_port);
		return rv;
	}
	ALOGV("autodiscover socket bound to %s:%d", ip ? : "*.*", listen_port);
	return 0;
}

static int prepare_anon_stream_socket(struct sockaddr_in* addr, socklen_t* addrlen) {

  int listen_fd;
  int ret;

  ret = listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(ret < 0) {
    ALOGE("Failed to create socket - %m");
    goto error;
  }

  memset((char *)addr, 0, *addrlen);
  addr->sin_family = AF_INET;
  addr->sin_addr.s_addr = htonl(listen_ip);
  addr->sin_port = 0;

  if( ret = bind(listen_fd, (struct sockaddr*)addr, *addrlen) < 0) {
    ALOGE("failed to bind socket - %m");
    goto cleanup_listen;
  }

  getsockname(listen_fd, (struct sockaddr*)addr, addrlen);

  if( ret = listen(listen_fd, 1) < 0 ) {
    ALOGE("failed to listen on socket - %m");
    goto cleanup_listen;
  }
  
  ALOGI("anon stream socket prepared %d", listen_fd);
  return(listen_fd);

cleanup_listen:
  close(listen_fd);

error:
  return(ret);
}

static void set_socket_options(int fd) {
    int opt=1;
    if( setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0)
      ALOGW("setsockopt TCP_NODELAY %d failed -%m",opt);
    else
      ALOGV("setsockopt TCP_NODELAY %d successful",opt);

    opt=1;
    if( setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0)
      ALOGW("setsockopt SO_KEEPALIVE %d failed -%m",opt);
    else
      ALOGV("setsockopt SO_KEEPALIVE %d successful",opt);

    // keepalive interval 15;
    opt=15;
    if( setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &opt, sizeof(opt)) < 0)
      ALOGW("setsockopt TCP_KEEPIDLE %d failed -%m",opt);
    else
      ALOGV("setsockopt TCP_KEEPIDLE %d successful",opt);

    // retry twice
    opt=2;
    if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &opt, sizeof(opt))  < 0)
      ALOGW("setsockopt TCP_KEEPCNT %d failed -%m",opt);
    else
      ALOGV("setsockopt TCP_KEEPCNT %d successful",opt);

    // allow 2 sec. to answer on keep alive
    opt=2;
    if(setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &opt, sizeof(opt)) < 0)
      ALOGW("setsockopt TCP_KEEPINTVL %d failed -%m",opt);
    else
      ALOGV("setsockopt TCP_KEEPINTVL %d successful",opt);
}

typedef enum tsdata_worker_status {
	DST_RUNNING,
	DST_EXITING,
	DST_FAILED,
	DST_ENDED
} tsdata_worker_status_t;

typedef struct tsdata_worker_data {
	int in;
	int listen_fd;
	tsdata_worker_status_t status;
} tsdata_worker_data_t;

static void *tsdata_worker(void *d) {
  tsdata_worker_data_t* data = (tsdata_worker_data_t*)d;

  int out_fd;
  struct sockaddr_in addr;
  socklen_t addrlen=sizeof(addr);

  data->status = DST_RUNNING;
  ALOGV("tsdata_worker thread started.");

  #ifdef DBGTS
  int dbg_fd=open( xstr(DBGTS) , O_RDWR|O_TRUNC);
  if(dbg_fd<0)
    ALOGV("Can't open debug ts file %s - %m",xstr(DBGTS));
  else
    ALOGV("copy TS data to %s", xstr(DBGTS));
  #endif

  out_fd = accept(data->listen_fd, (struct sockaddr *)&addr, &addrlen);
  if( out_fd < 0) {
    ALOGE("accept failed on data socket - %m");
    data->status = DST_FAILED;
    goto error;
  }

  data->status = DST_RUNNING;
  close(data->listen_fd);
  data->listen_fd=0;

  set_socket_options(out_fd);
 
  #define RMAX (188*174)
  #define WMAX (4*RMAX)  // match client read size
  unsigned char buffer[WMAX*4]; 
  int bufptr = 0, bufptr_write = 0;

  size_t window_size = sizeof(buffer);
  if(setsockopt(out_fd, SOL_SOCKET, SO_SNDBUF, (char *) &window_size, sizeof(window_size))) {
    ALOGW("set window size failed - %m");
  }

  if( fcntl(out_fd, F_SETFL, O_NONBLOCK) != 0) {
    ALOGW("O_NONBLOCK failed for socket - %m");
  }

  long long now, last_written;
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  last_written = (long long)t.tv_sec*1000 + (long long)t.tv_nsec/1000000;

  while(data->status == DST_RUNNING) {
    struct pollfd pfd[] = { {data->in, POLLIN, 0}, {out_fd, POLLOUT, 0} };
    int waiting = poll(pfd, 2, 10);

    if(pfd[0].revents & POLLIN) {
      int rmax = (sizeof(buffer) - bufptr);
      // reading too much data can delay writing, read 1/4
      // of RMAX
      rmax = (rmax>RMAX)?RMAX:rmax;
      if(rmax == 0) {
        ALOGW("no space left in buffer to read data, data loss possible");
      } else {
        int rlen = read(data->in, buffer + bufptr, rmax);
        if(rlen>0) bufptr += rlen;

        ALOGV("receive buffer stats size:%d(%d,%d), read:%d(%d,%d)", \
                  rmax, rmax/188, rmax%188,
                  rlen, rlen/188, rlen%188); 

      }
    } 

    int w = bufptr - bufptr_write;
    clock_gettime(CLOCK_MONOTONIC, &t);
    now = (long long)t.tv_sec*1000 + (long long)t.tv_nsec/1000000;
    long long delta = now - last_written;

    // 2010-04-04:
    // send data in the same amount as received on the
    // client side, this should reduce syscalls on both
    // ends of the connection
    if( (pfd[1].revents & POLLOUT) && \
        (w >= WMAX || (now - last_written > 100 && w > 0)) ) {
      w = w>WMAX?WMAX:w; // write the same amount of data the client prefers to read
      int wlen = write(out_fd,  buffer + bufptr_write, w);
/*
      if(delta>100) {
        ALOGI("data sent late: size:%d, written:%d, delay: %lld", \
              bufptr - bufptr_write, wlen, delta);
      }
*/
      #ifdef DBGTS
      int dgblen = write(dbg_fd, buffer + bufptr_write, w);
      if( wlen != dgblen) {
        ALOGV("stream write:%d debug file write:%d. debug file me be corrupt.");
      }
      #endif
      
      if(wlen>=0) {
        bufptr_write += wlen;
        // 2010-01-30 do not reset on each write
        // last_written = now;
      } else {
        if( errno != EAGAIN ) {
          data->status = DST_FAILED;
          ALOGE("stream write failed %d!=%d - %m", errno, EAGAIN);
        }
      }
      if (bufptr_write == bufptr) {
        bufptr_write = bufptr = 0;
        // 2010-01-30 reset last_writen only if buffer is empty
        last_written = now;
      }
    } else {
      // 2010-01-30
      // if nothing is written, wait a few ms to avoid reading
      // data in small chunks, max. read chunk is ~128kB
      // 20ms wait. should give ~6.4MB/s 
      usleep(20*1000);
    }
  }
  return 0;
error:
  ALOGI("TS data copy thread terminated.");
  close(out_fd);
  data->status = DST_ENDED;
  return 0;
}

int fetch_request(struct sockaddr_in *client_so, int *proto, int *tuner_type, int *tuner_group) {

	int clientlen = sizeof(*client_so);
	vtuner_net_message_t msg;

	ALOGI("waiting for autodiscover packet (groups 0x%04X) ...", *tuner_group);
	do {
		if( recvfrom(discover_fd, &msg, sizeof(msg), 0, (struct sockaddr *) client_so, &clientlen) <= 0 )
			return 0;

		ntoh_vtuner_net_message(&msg, 0); // we don't care frontend type
		*proto = msg.ver;
		*tuner_type = msg.u.discover.vtype;
		if(msg.ver >= VTUNER_PROTO2 && *tuner_group != -1) {
			if(((*tuner_group) & msg.u.discover.tuner_group) == 0) {
				ALOGI("request for group 0x%04X, not accepting", msg.u.discover.tuner_group);
				continue;
			}
		}
		ALOGV("request received (group 0x%04X)", msg.u.discover.tuner_group);
		return 1;
	} while(1);

	return 0; // should never pass, but to be sure 
}

int run_worker(int adapter, int fe, int demux, int dvr, struct sockaddr_in *client_so) {

	vtuner_net_message_t msg;
	vtuner_hw_t hw;

	struct sockaddr_in ctrl_so, data_so;
	socklen_t ctrllen = sizeof(ctrl_so), datalen = sizeof(data_so);

	int listen_fd, ctrl_fd;
	int ex = 0;

		struct dvb_frontend_parameters fe_params;

	if( ! hw_init(&hw, adapter, fe, demux, dvr)) {
		ALOGE("hardware init failed");
		goto cleanup_hw;
	}

	listen_fd = prepare_anon_stream_socket( &ctrl_so, &ctrllen);
	if( listen_fd < 0) {
		ALOGE("control socket init failed");
		goto cleanup_hw;
	}

	msg.u.discover.port = ntohs(ctrl_so.sin_port);
	ALOGI("control socket bound to %d", msg.u.discover.port);

	tsdata_worker_data_t dwd;
	dwd.in = hw.streaming_fd;
	dwd.listen_fd =  prepare_anon_stream_socket( &data_so, &datalen);
	if( dwd.listen_fd < 0)
		goto cleanup_listen;
	dwd.status = DST_RUNNING;
	pthread_t dwt;
	pthread_create( &dwt, NULL, tsdata_worker, &dwd );

	msg.u.discover.tsdata_port = ntohs(data_so.sin_port);
	msg.msg_type = MSG_DISCOVER;
	ALOGI("session prepared control:%d data:%d", msg.u.discover.port, msg.u.discover.tsdata_port);
    hton_vtuner_net_message(&msg, 0);
    if(sendto(discover_fd, &msg, sizeof(msg), 0, (struct sockaddr *)client_so, sizeof(*client_so))>0)
    	ALOGV("Answered discover request");
    else {
    	ALOGE("Failed to sent discover packet - %m");
    	goto cleanup_worker_thread;
    }


    struct pollfd afd[] = { { listen_fd, POLLIN, 0 } };
    if(poll(afd,1,5000)) {
    	ctrl_fd = accept(listen_fd, (struct sockaddr *)&ctrl_so, &ctrllen);
    	if(ctrl_fd<0) {
    		ALOGE("accept failed on control socket - %m");
    		goto cleanup_worker_thread;
    	}
    } else {
    	ALOGI("no client connected. timeout");
    	goto cleanup_worker_thread;
    }

	close(listen_fd);
	listen_fd=0;

	set_socket_options(ctrl_fd);

	// client sends SET_FRONTEND with invalid data after a SET_PROPERTY
	int skip_set_frontend = 0;

	ALOGI("session running");
	while(dwd.status == DST_RUNNING) {
		struct pollfd pfd[] = { { ctrl_fd, POLLIN, 0 } };
		poll(pfd, 1, 750);
		if(pfd[0].revents & POLLIN) {
			ALOGV("control message received");
			int rlen = read(ctrl_fd, &msg, sizeof(msg));
			if(rlen<=0) goto cleanup_worker_thread;
			int ret=0;
			if( sizeof(msg) == rlen) {
				ntoh_vtuner_net_message(&msg, hw.type);
				if(msg.msg_type < 1023 ) {
					switch (msg.u.vtuner.type) {
						case MSG_SET_FRONTEND:
							get_dvb_frontend_parameters( &fe_params, &msg.u.vtuner, hw.type);
							if( skip_set_frontend ) {
								ret = 0; // fake successful call
								ALOGV("MSG_SET_FRONTEND skipped %d", skip_set_frontend);
							} else {
								ret=hw_set_frontend(&hw, &fe_params);
								ALOGV("MSG_SET_FRONTEND %d", skip_set_frontend);
							}
							break;
						case MSG_GET_FRONTEND:
							ret=hw_get_frontend(&hw, &fe_params);
							set_dvb_frontend_parameters(&msg.u.vtuner, &fe_params, hw.type);
							ALOGV("MSG_GET_FRONTEND");
							break;
						case MSG_READ_STATUS:
							ret=hw_read_status(&hw, &msg.u.vtuner.body.status);
							ALOGV("MSG_READ_STATUS: 0x%x", msg.u.vtuner.body.status);
							break;
						case MSG_READ_BER:
							ret=ioctl(hw.frontend_fd, FE_READ_BER, &msg.u.vtuner.body.ber);
							ALOGV("MSG_READ_BER: %d", msg.u.vtuner.body.ber);
							break;
						case MSG_READ_SIGNAL_STRENGTH:
							ret=ioctl(hw.frontend_fd, FE_READ_SIGNAL_STRENGTH, &msg.u.vtuner.body.ss);
							ALOGV("MSG_READ_SIGNAL_STRENGTH: %d", msg.u.vtuner.body.ss);
							break;
						case MSG_READ_SNR:
							ret=ioctl(hw.frontend_fd, FE_READ_SNR, &msg.u.vtuner.body.snr);
							ALOGV("MSG_READ_SNR: %d", msg.u.vtuner.body.snr);
							break;
						case MSG_READ_UCBLOCKS:
							ioctl(hw.frontend_fd, FE_READ_UNCORRECTED_BLOCKS, &msg.u.vtuner.body.ucb);
							ALOGV("MSG_READ_UCBLOCKS %d", msg.u.vtuner.body.ucb);
							break;
						case MSG_SET_TONE:
							ret=hw_set_tone(&hw, msg.u.vtuner.body.tone);
							ALOGV("MSG_SET_TONE: 0x%x", msg.u.vtuner.body.tone);
							break;
						case MSG_SET_VOLTAGE:
							ret=hw_set_voltage(&hw, msg.u.vtuner.body.voltage);
							ALOGV("MSG_SET_VOLTAGE: 0x%x", msg.u.vtuner.body.voltage);
							break;
						case MSG_ENABLE_HIGH_VOLTAGE:
							//FIXME: need to know how information is passed to client
							ALOGW("MSG_ENABLE_HIGH_VOLTAGE is not implemented: %d", msg.u.vtuner.body.pad[0]);
							break;
						case MSG_SEND_DISEQC_MSG: {
							ret=hw_send_diseq_msg(&hw, &msg.u.vtuner.body.diseqc_master_cmd);
							ALOGV("MSG_SEND_DISEQC_MSG: ");
							break;
						}
						case MSG_SEND_DISEQC_BURST: {
							ret=hw_send_diseq_burst(&hw, msg.u.vtuner.body.burst);
							ALOGV("MSG_SEND_DISEQC_BURST: %d %d", msg.u.vtuner.body.burst,ret);
							break;
						}
						case MSG_SET_PROPERTY:
							ret=hw_set_property(&hw, &msg.u.vtuner.body.prop);
							// in case the call was successful, we have to skip
							// the next call to SET_FRONTEND
							skip_set_frontend = (ret == 0);
							ALOGV("MSG_SET_PROPERTY: %d %d %d", msg.u.vtuner.body.prop.cmd, skip_set_frontend, ret);
							break;
						case MSG_GET_PROPERTY:
							ret=hw_get_property(&hw, &msg.u.vtuner.body.prop);
							ALOGV("MSG_GET_PROPERTY: %d %d", msg.u.vtuner.body.prop.cmd,ret);
							break;
						case MSG_PIDLIST:
							ret=hw_pidlist(&hw, msg.u.vtuner.body.pidlist );
							break;
						default:
							ret = 0;
							ALOGW("unknown vtuner message %d", msg.u.vtuner.type);
							// don't stop here, instead send message
							// back to avoid client hang
					}

					if (msg.u.vtuner.type != MSG_PIDLIST ) {
						if( ret!= 0 )
							ALOGW("vtuner call failed, type:%d reason:%d", msg.u.vtuner.type, ret);
						msg.u.vtuner.type = ret;
						hton_vtuner_net_message(&msg, hw.type);
						write(ctrl_fd, &msg, sizeof(msg));
					}
				} else {
					switch (msg.msg_type) {
						case MSG_NULL:
							ret = 0;
							break;
						default:
							ret = 0;
							ALOGW("received out-of-state control message: %d", msg.msg_type);
							// don't stop here, instead send message
							// back to avoid client hang
					}
					msg.u.vtuner.type = ret;
					hton_vtuner_net_message(&msg, hw.type);
					write(ctrl_fd, &msg, sizeof(msg));
				}
			} else {
				ALOGW("message size is invalid: %d", rlen);
			}
		}
	}
	ex= 1;

	cleanup_worker_thread:
		dwd.status = DST_EXITING;
		// FIXME: need a better way to know if thread has finished
		ALOGV("wait for TS data copy thread to terminate");
		pthread_join(dwt, NULL);
		ALOGV("TS data copy thread terminated - %m");

	cleanup_ctrl:
		close(ctrl_fd);

	cleanup_listen_data:
		close(dwd.listen_fd);

	cleanup_listen:
		close(listen_fd);

	cleanup_hw:
		hw_free(&hw);

	error:
		ALOGI("control thread terminated.");

	return ex;
}

