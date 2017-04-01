#ifndef _VTUNERNETWORK_H_
#define _VTUNERNETWORK_H_
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/dvb/version.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>


#define VTUNER_DISCOVER_PORT 0x9989

#define VTUNER_PROTO2 2
#define VTUNER_PROTO_MAX VTUNER_PROTO2

#define VTUNER_GROUPS_ALL 0xFFFF

typedef enum vtuner_type {
  VT_S = 0x01,
  VT_C = 0x02,
  VT_T = 0x04,
  VT_S2 = 0x08
} vtuner_type_t;

#define MSG_SET_FRONTEND         1
#define MSG_GET_FRONTEND         2
#define MSG_READ_STATUS          3
#define MSG_READ_BER             4
#define MSG_READ_SIGNAL_STRENGTH 5
#define MSG_READ_SNR             6
#define MSG_READ_UCBLOCKS        7
#define MSG_SET_TONE             8
#define MSG_SET_VOLTAGE          9
#define MSG_ENABLE_HIGH_VOLTAGE  10
#define MSG_SEND_DISEQC_MSG      11
#define MSG_SEND_DISEQC_BURST    13
#define MSG_PIDLIST              14
#define MSG_TYPE_CHANGED	 15
#define MSG_SET_PROPERTY 	 16
#define MSG_GET_PROPERTY 	 17

#define MSG_NULL		 1024
#define MSG_DISCOVER		 1025
#define MSG_UPDATE       	 1026

typedef struct diseqc_master_cmd {
	__u8 msg [6];
	__u8 msg_len;
} diseqc_master_cmd_t;

#if DVB_API_VERSION < 5 
struct dtv_property {
        __u32 cmd;
        __u32 reserved[3];
        union {
                __u32 data;
                struct {
                        __u8 data[32];
                        __u32 len;
                        __u32 reserved1[3];
                        void *reserved2;
                } buffer;
        } u;
        int result;
} __attribute__ ((packed));

#define DTV_UNDEFINED           0
#define DTV_TUNE                1
#define DTV_CLEAR               2
#define DTV_FREQUENCY           3
#define DTV_MODULATION          4
#define DTV_BANDWIDTH_HZ        5
#define DTV_INVERSION           6
#define DTV_DISEQC_MASTER       7
#define DTV_SYMBOL_RATE         8
#define DTV_INNER_FEC           9
#define DTV_VOLTAGE             10
#define DTV_TONE                11
#define DTV_PILOT               12
#define DTV_ROLLOFF             13
#define DTV_DISEQC_SLAVE_REPLY  14
#define DTV_FE_CAPABILITY_COUNT	15
#define DTV_FE_CAPABILITY	16
#define DTV_DELIVERY_SYSTEM	17

#define DTV_IOCTL_MAX_MSGS 64

#endif

typedef struct vtuner_message {
        __s32 type;
        union {
		struct {
			__u32	frequency;
			__u8	inversion;
			union {
				struct {
					__u32	symbol_rate;
					__u32	fec_inner;
				} qpsk;
				struct {
					__u32   symbol_rate;
					__u32   fec_inner;
					__u32	modulation;
				} qam;
				struct {
					__u32	bandwidth;
					__u32	code_rate_HP;
					__u32	code_rate_LP;
					__u32	constellation;
					__u32	transmission_mode;
					__u32	guard_interval;
					__u32	hierarchy_information;
				} ofdm;
				struct {
					__u32	modulation;
				} vsb;
			} u;
		} fe_params;
		struct dtv_property prop;
                __u32 status;
                __u32 ber;
                __u16 ss, snr;
                __u32 ucb;
                __u8 tone;
                __u8 voltage;
		diseqc_master_cmd_t diseqc_master_cmd;
                __u8 burst;
                __u16 pidlist[30];
                __u8  pad[72];
		__u32 type_changed;
        } body;
} vtuner_message_t;

/*
typedef struct vtuner_frontend_info {
  char name[128];
  vtuner_type_t type;
  __u32 frequency_min;
  __u32 frequency_max;
  __u32 frequency_stepsize;
  __u32 frequency_tolerance;
  __u32 symbol_rate_min;
  __u32 symbol_rate_max;
  __u32 symbol_rate_tolerance; 
  __u32 notifier_delay;
  __u8 caps;
} vtuner_frontend_info_t;
*/

typedef struct vtuner_discover {
//  vtuner_frontend_info_t fe_info;   
  vtuner_type_t vtype;
  __u16 port;
  __u16 tsdata_port;
  __u16 tuner_group;
  __u8 reserved[80-2];
} vtuner_discover_t;

typedef struct vtuner_update {
  __u32 status; 
  __u32 ber;
  __u32 ucb;
  __u16 ss, snr;
} vtuner_update_t;

typedef struct vtuner_net_message {
  __u8 ver;
  __u8 cap;
  __u16 msg_type;
  __u32 serial;
  union {
    vtuner_message_t vtuner;
    vtuner_discover_t discover;
    vtuner_update_t update; 
  } u;
} vtuner_net_message_t;

  void get_dvb_frontend_parameters( struct dvb_frontend_parameters*, vtuner_message_t*, vtuner_type_t); 
  void set_dvb_frontend_parameters( vtuner_message_t*, struct dvb_frontend_parameters*, vtuner_type_t);

int ntoh_get_message_type( vtuner_net_message_t*);
void hton_vtuner_net_message( vtuner_net_message_t*, vtuner_type_t);
void ntoh_vtuner_net_message( vtuner_net_message_t*, vtuner_type_t); 

void print_vtuner_net_message(vtuner_net_message_t*);

/* open source vtunerc API */
/* WARNING: Unstable! Is going to be refactored, current version is mostly 1:1 of dreambox' original */
#define VTUNER_CTRL_DEVNAME "/dev/vtunerc0"
#define VTUNER_MAJOR		226
#define VTUNER_GET_MESSAGE	_IOR(VTUNER_MAJOR, 1, struct vtuner_message *)
#define VTUNER_SET_RESPONSE 	_IOW(VTUNER_MAJOR, 2, struct vtuner_message *)
#define VTUNER_SET_NAME		_IOW(VTUNER_MAJOR, 3, char *)
#define VTUNER_SET_TYPE		_IOW(VTUNER_MAJOR, 4, char *)
#define VTUNER_SET_FE_INFO	_IOW(VTUNER_MAJOR, 6, struct dvb_frontend_info *)

#endif
