#ifndef _VTUNER_DVB_3_H_
#define _VTUNER_DVB_3_H_

#include "vtuner-network.h"
#include <utils/Log.h>



#ifndef MAX_DEMUX
#define MAX_DEMUX 30
#endif

typedef struct vtuner_hw {

  vtuner_type_t type;
  struct dvb_frontend_info fe_info; 

  int frontend_fd;
  int demux_fd[MAX_DEMUX];
  __u16 pids[MAX_DEMUX];
  int streaming_fd;

  int adapter;
  int frontend;
  int demux;

  int num_props;
  struct dtv_property props[DTV_IOCTL_MAX_MSGS];

} vtuner_hw_t;

int hw_init(vtuner_hw_t*, int, int, int, int);
void hw_free(vtuner_hw_t*);
int hw_get_frontend(vtuner_hw_t*, struct dvb_frontend_parameters*);
int hw_set_frontend(vtuner_hw_t*, struct dvb_frontend_parameters*);
int hw_get_property(vtuner_hw_t*, struct dtv_property*);
int hw_set_property(vtuner_hw_t*, struct dtv_property*);
int hw_read_status(vtuner_hw_t*, __u32*);
int hw_set_tone(vtuner_hw_t*, __u8);
int hw_set_voltage(vtuner_hw_t*, __u8);
int hw_send_diseq_msg(vtuner_hw_t*, diseqc_master_cmd_t*);
int hw_send_diseq_burst(vtuner_hw_t*, __u8);
int hw_pidlist(vtuner_hw_t*, __u16*);

#endif
