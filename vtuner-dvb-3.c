#define LOG_TAG "vtuner-dvb"
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <poll.h>
#include <cutils/log.h>

#include "vtuner-dvb-3.h"
#include "vtuner-utils.h"

int hw_init(vtuner_hw_t* hw, int adapter, int frontend, int demux, int dvr) {

  char devstr[80];
  int i;

  hw->adapter = adapter;
  hw->frontend = frontend;
  hw->demux = demux;

  hw->frontend_fd = 0;
  hw->streaming_fd = 0;
  memset(hw->demux_fd, 0, sizeof(hw->demux_fd)); 
  memset(hw->pids, 0xff, sizeof(hw->pids));

  hw->num_props = 0;
  memset(hw->props, 0x00, sizeof(hw->props));

  sprintf( devstr, "/dev/dvb/adapter%d/frontend%d", hw->adapter, hw->frontend);
  hw->frontend_fd = open( devstr, O_RDWR);
  if(hw->frontend_fd < 0) {
    ALOGE("failed to open %s\n", devstr);
    goto error;
  }

  if(ioctl(hw->frontend_fd, FE_GET_INFO, &hw->fe_info) != 0) {
    ALOGE("FE_GET_INFO failed for %s\n", devstr);
    goto error;    
  }

  switch(hw->fe_info.type) {
    case FE_QPSK: 
      #if DVB_API_VERSION < 5
        hw->type = VT_S;
      #else
        if( hw->fe_info.caps & ( FE_HAS_EXTENDED_CAPS | FE_CAN_2G_MODULATION ) ) {
          hw->type = VT_S2; 
        } else {
          hw->type = VT_S;
        } 
      #endif
      break;
    case FE_QAM:  hw->type = VT_C; break;
    case FE_OFDM: hw->type = VT_T; break;
    default: 
      ALOGE("Unknown frontend type %d\n", hw->fe_info.type); 
      goto cleanup_fe;
  }
  ALOGI("FE_GET_INFO dvb-type:%d vtuner-type:%d\n", hw->fe_info.type, hw->type);

  sprintf( devstr, "/dev/dvb/adapter%d/dvr%d", hw->adapter, dvr); 
  hw->streaming_fd = open( devstr, O_RDONLY);
  if(hw->streaming_fd < 0) {
    ALOGE("failed to open %s\n", devstr);
    goto cleanup_fe;
  }

  if( fcntl(hw->streaming_fd, F_SETFL, O_NONBLOCK) != 0) {
      ALOGE("O_NONBLOCK failed for %s\n",devstr);
      goto cleanup_fe;
  }

  sprintf( devstr, "/dev/dvb/adapter%d/demux%d", hw->adapter, demux);
  for(i=0; i<MAX_DEMUX; ++i) {
    hw->demux_fd[i] = open(devstr, O_RDWR|O_NONBLOCK);
    if(hw->demux_fd[i]<0) {
      ALOGE("failed to open %s\n", devstr);
      goto cleanup_demux;
    }

    if( ioctl(hw->demux_fd[i], DMX_SET_BUFFER_SIZE, 1024*16) != 0 ) {
      ALOGE("DMX_SET_BUFFER_SIZE failed for %s\n",devstr);
      goto cleanup_demux;
    }
/*
    if( fcntl(hw->demux_fd[i], F_SETFL, O_NONBLOCK) != 0) {
      ALOGE("O_NONBLOCK failed for %s\n",devstr);
      goto cleanup_demux;
    }
*/
  }

  return 1;

cleanup_demux:
  for(i=0;i<MAX_DEMUX; ++i) 
    if(hw->demux_fd[i] > 0) 
      close(hw->demux_fd[i]);

cleanup_dvr:
  close(hw->streaming_fd);

cleanup_fe:
  close(hw->frontend_fd);

error:
  return 0;
} 

void hw_free(vtuner_hw_t *hw) {
	if(hw->frontend_fd>0) close(hw->frontend_fd);
	if(hw->streaming_fd>0) close(hw->streaming_fd);
	int i;
	for(i=0;i<MAX_DEMUX; ++i)
		if(hw->demux_fd[i] > 0)
			close(hw->demux_fd[i]);
}

void print_frontend_parameters(vtuner_hw_t* hw, struct dvb_frontend_parameters* fe_params, char *msg, size_t msgsize) {
  switch(hw->type) {
    case VT_S2:
    case VT_S: snprintf(msg, msgsize, "freq:%d inversion:%d SR:%d FEC:%d\n", \
                        fe_params->frequency, fe_params->inversion, \
                        fe_params->u.qpsk.symbol_rate, fe_params->u.qpsk.fec_inner);
               break;
    case VT_C: snprintf(msg, msgsize, "freq:%d inversion:%d SR:%d FEC:%d MOD:%d\n", \
                        fe_params->frequency, fe_params->inversion, \
                        fe_params->u.qam.symbol_rate, fe_params->u.qam.fec_inner, fe_params->u.qam.modulation);
               break;
    case VT_T: snprintf(msg, msgsize, "freq:%d inversion:%d BW:%d CRHP:%d CRLP:%d\n", \
                        fe_params->frequency, fe_params->inversion, \
                        fe_params->u.ofdm.bandwidth, fe_params->u.ofdm.code_rate_HP, fe_params->u.ofdm.code_rate_LP ); 
               break;
  }
}

int hw_get_frontend(vtuner_hw_t* hw, struct dvb_frontend_parameters* fe_params) {
  int ret;
  ret = ioctl(hw->frontend_fd, FE_GET_FRONTEND, fe_params);
  if( ret != 0 ) {
    ALOGW( "FE_GET_FRONTEND failed. It seems your DVB driver has incomplete implementation.\n");
    // Follows workaround for crappy drivers which have not implemented .get_frontend() callback
    memset(fe_params, 0, sizeof(struct dvb_frontend_parameters));
    ret = 0;
  }
  return ret;
}

int hw_set_frontend(vtuner_hw_t* hw, struct dvb_frontend_parameters* fe_params) {
  int ret;
  char msg[1024];
  print_frontend_parameters(hw, fe_params, msg, sizeof(msg));
  ALOGI("FE_SET_FRONTEND parameters: %s", msg);
  #if DVB_API_VERSION < 5 
    ret = ioctl(hw->frontend_fd, FE_SET_FRONTEND, fe_params);
  #else
    struct dtv_properties cmdseq = {
      .num = 0,
      .props = NULL
    };

    struct dtv_property CLEAR[] = {
    	{ .cmd = DTV_CLEAR },
    };

    cmdseq.num = 1;
    cmdseq.props = CLEAR;
    if( ioctl(hw->frontend_fd, FE_SET_PROPERTY, &cmdseq) != 0 )
    	ALOGW( "FE_SET_FRONTEND DTV_CLEAR failed - %m\n");

    struct dtv_property S[] = {
      { .cmd = DTV_DELIVERY_SYSTEM,   .u.data = SYS_DVBS },
      { .cmd = DTV_FREQUENCY,         .u.data = fe_params->frequency },
      { .cmd = DTV_MODULATION,        .u.data = QPSK },
      { .cmd = DTV_SYMBOL_RATE,       .u.data = fe_params->u.qpsk.symbol_rate },
      { .cmd = DTV_INNER_FEC,         .u.data = fe_params->u.qpsk.fec_inner },
      { .cmd = DTV_INVERSION,         .u.data = INVERSION_AUTO },
      { .cmd = DTV_ROLLOFF,           .u.data = ROLLOFF_AUTO },
      { .cmd = DTV_PILOT,             .u.data = PILOT_AUTO },
      { .cmd = DTV_TUNE },
    };

    switch(hw->type) { 
      case VT_S:
      case VT_S2: {
        cmdseq.num = 9;
        cmdseq.props = S;
        if( ( hw->type == VT_S || hw->type == VT_S2) &&  fe_params->u.qpsk.fec_inner > FEC_AUTO) {
          cmdseq.props[0].u.data = SYS_DVBS2;
          switch( fe_params->u.qpsk.fec_inner ) {
            case 19: cmdseq.props[2].u.data = PSK_8;
            case 10: cmdseq.props[4].u.data = FEC_1_2 ; break;
            case 20: cmdseq.props[2].u.data = PSK_8;
            case 11: cmdseq.props[4].u.data = FEC_2_3 ; break;
            case 21: cmdseq.props[2].u.data = PSK_8;
            case 12: cmdseq.props[4].u.data = FEC_3_4 ; break;
            case 22: cmdseq.props[2].u.data = PSK_8;
            case 13: cmdseq.props[4].u.data = FEC_5_6 ; break;
            case 23: cmdseq.props[2].u.data = PSK_8;
            case 14: cmdseq.props[4].u.data = FEC_7_8 ; break;
            case 24: cmdseq.props[2].u.data = PSK_8;
            case 15: cmdseq.props[4].u.data = FEC_8_9 ; break;
            case 25: cmdseq.props[2].u.data = PSK_8;
            case 16: cmdseq.props[4].u.data = FEC_3_5 ; break;
            case 26: cmdseq.props[2].u.data = PSK_8;
            case 17: cmdseq.props[4].u.data = FEC_4_5 ; break;
            case 27: cmdseq.props[2].u.data = PSK_8;
            case 18: cmdseq.props[4].u.data = FEC_9_10; break;
          }
          switch( fe_params->inversion & 0x0c ) {
            case 0: cmdseq.props[6].u.data = ROLLOFF_35; break;
            case 4: cmdseq.props[6].u.data = ROLLOFF_25; break;
            case 8: cmdseq.props[6].u.data = ROLLOFF_20; break;
            default: ALOGW( "ROLLOFF unknnown\n");
          }
          switch( fe_params->inversion & 0x30 ) {
            case 0:    cmdseq.props[7].u.data = PILOT_OFF;  break;
            case 0x10: cmdseq.props[7].u.data = PILOT_ON;   break;
            case 0x20: cmdseq.props[7].u.data = PILOT_AUTO; break;
            default: ALOGW( "PILOT unknown\n");
          }
          cmdseq.props[5].u.data &= 0x04;
        }
        ALOGD("S2API tuning SYS:%d MOD:%d FEC:%d INV:%d ROLLOFF:%d PILOT:%d\n", cmdseq.props[0].u.data, cmdseq.props[2].u.data, cmdseq.props[4].u.data, cmdseq.props[5].u.data, cmdseq.props[6].u.data, cmdseq.props[7].u.data);
        ret=ioctl(hw->frontend_fd, FE_SET_PROPERTY, &cmdseq);
        break;
      }
      case VT_C:
      case VT_T:  // even If we would have S2API, the old is sufficent to tune
        ret = ioctl(hw->frontend_fd, FE_SET_FRONTEND, fe_params); 
        break;
      default:
	ALOGW( "tuning not implemented for HW-type:%d (S:%d, S2:%d C:%d T:%d)\n", hw->type, VT_S, VT_S2, VT_C, VT_T);
    }
  #endif
  if( ret != 0 ) ALOGW( "FE_SET_FRONTEND failed %s - %m\n", msg);
  return ret;
}

int hw_get_property(vtuner_hw_t* hw, struct dtv_property* prop) {
  ALOGW( "FE_GET_PROPERTY: not implemented %d\n", prop->cmd);
  return 0;
}

int hw_set_property(vtuner_hw_t* hw, struct dtv_property* prop) {
  int ret=0;
#if DVB_API_VERSION < 5
  ret = -1;
  ALOGW( "FE_SET_PROPERTY is not available\n");
#else
  ALOGD("FE_SET_PROPERTY %d\n", prop->cmd);
  switch( prop->cmd ) {
    case DTV_UNDEFINED: break;
    case DTV_CLEAR: 
      hw->num_props = 0;
      ALOGD("FE_SET_PROPERTY: DTV_CLEAR\n");
      break;
    case DTV_TUNE: {
      hw->props[hw->num_props].cmd = prop->cmd;
      hw->props[hw->num_props].u.data = prop->u.data;
      ++hw->num_props;

      struct dtv_properties cmdseq;
      cmdseq.num = hw->num_props;
      cmdseq.props = hw->props;
      ALOGD("FE_SET_PROPERTY: DTV_TUNE\n");
      ret=ioctl(hw->frontend_fd, FE_SET_PROPERTY, &cmdseq);
      if( ret != 0 ) ALOGW( "FE_SET_PROPERTY failed - %m\n");
    } break;
    case DTV_FREQUENCY:
    case DTV_MODULATION:
    case DTV_BANDWIDTH_HZ:
    case DTV_INVERSION:
    case DTV_DISEQC_MASTER:
    case DTV_SYMBOL_RATE:
    case DTV_INNER_FEC:
    case DTV_VOLTAGE:
    case DTV_TONE:
    case DTV_PILOT:
    case DTV_ROLLOFF:
    case DTV_DISEQC_SLAVE_REPLY:
    case DTV_FE_CAPABILITY_COUNT:
    case DTV_FE_CAPABILITY:
    case DTV_DELIVERY_SYSTEM:
      if(hw->num_props < DTV_IOCTL_MAX_MSGS) {
        hw->props[hw->num_props].cmd = prop->cmd;
        hw->props[hw->num_props].u.data = prop->u.data;
        ALOGD("FE_SET_PROPERTY: set %d to %d, %d properties collected\n", hw->props[hw->num_props].cmd, hw->props[hw->num_props].u.data, hw->num_props+1);
        ++hw->num_props;
      } else {
        ALOGW( "FE_SET_PROPERTY properties limit (%d) exceeded.\n", DTV_IOCTL_MAX_MSGS);
        ret = -1;
      } break;
    default: 
      ALOGW( "FE_SET_PROPERTY unknown property %d\n", prop->cmd);
      ret = -1;
  }
#endif
  return ret;
}

int hw_read_status(vtuner_hw_t* hw, __u32* status) {
  int ret;
  ret = ioctl(hw->frontend_fd, FE_READ_STATUS, status);
  if( ret != 0 ) ALOGW( "FE_READ_STATUS failed\n");
  return ret;
}

int hw_set_tone(vtuner_hw_t* hw, __u8 tone) {
  int ret=0;
  ret = ioctl(hw->frontend_fd, FE_SET_TONE, tone);
  if( ret != 0 ) ALOGW( "FE_SET_TONE failed - %m\n");
  return ret;
}

int hw_set_voltage(vtuner_hw_t* hw, __u8 voltage) {
  int ret=0;
  if( hw->type == VT_S || hw->type == VT_S2 ) { // Dream supports this on DVB-T, but not plain linux
    ret = ioctl(hw->frontend_fd, FE_SET_VOLTAGE, voltage);
    if( ret != 0 ) ALOGW( "FE_SET_VOLTAGE failed - %m\n");
  }
  return ret;
}

int hw_send_diseq_msg(vtuner_hw_t* hw, diseqc_master_cmd_t* cmd) {
  int ret=0;
  ret=ioctl(hw->frontend_fd, FE_DISEQC_SEND_MASTER_CMD, cmd);
  if( ret != 0 ) ALOGW( "FE_DISEQC_SEND_MASTER_CMD failed - %m\n");
  return ret;
}

int hw_send_diseq_burst(vtuner_hw_t* hw, __u8 burst) {
  int ret=0;
  ret=ioctl(hw->frontend_fd, FE_DISEQC_SEND_BURST, burst);
  if( ret != 0 ) ALOGW( "FE_DISEQC_SEND_BURST  - %m\n");
  return ret;
}

int hw_pidlist(vtuner_hw_t* hw, __u16* pidlist) {
  int i,j;
  struct dmx_pes_filter_params flt;

  ALOGI("hw_pidlist befor: ");
  for(i=0; i<MAX_DEMUX; ++i) if(hw->pids[i] != 0xffff) ALOGI("%d ", hw->pids[i]);
  
  ALOGI("hw_pidlist sent:  ");
  for(i=0; i<MAX_DEMUX; ++i) if(pidlist[i] != 0xffff) ALOGI("%d ", pidlist[i]);
  

  for(i=0; i<MAX_DEMUX; ++i) 
    if(hw->pids[i] != 0xffff) {
      for(j=0; j<MAX_DEMUX; ++j) 
        if(hw->pids[i] == pidlist[j])
          break;
      if(j == MAX_DEMUX) {
        ioctl(hw->demux_fd[i], DMX_STOP, 0);
        hw->pids[i] = 0xffff;
      }
    }

  for(i=0; i<MAX_DEMUX; ++i) 
    if(pidlist[i] != 0xffff) {
      for(j=0; j<MAX_DEMUX; ++j) 
        if(pidlist[i] == hw->pids[j])
          break;
      if(j == MAX_DEMUX) {
        for(j=0; j<MAX_DEMUX; ++j) 
          if(hw->pids[j] == 0xffff) 
            break;
        if(j==MAX_DEMUX) {
          ALOGW( "no free demux found. skip pid %d\n",pidlist[i]);
        } else {
          flt.pid = hw->pids[j] = pidlist[i];
          flt.input = DMX_IN_FRONTEND;
          flt.pes_type = DMX_PES_OTHER;
          flt.output = DMX_OUT_TS_TAP;
          flt.flags = DMX_IMMEDIATE_START;
          ioctl(hw->demux_fd[j], DMX_SET_PES_FILTER, &flt);
        }
      }
    }

  ALOGI("hw_pidlist after: ");
  for(i=0; i<MAX_DEMUX; ++i) if(hw->pids[i] != 0xffff) ALOGI("%d ", hw->pids[i]);  


  return 0;
}
