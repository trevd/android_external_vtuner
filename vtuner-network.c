#define LOG_TAG "vtuner-network"
#include <cutils/log.h>
#include "vtuner-network.h"
#include <string.h>
#include <stdio.h>

#define NTOHB(host,net,field) host->field=net.field
#define HTONB(net, host, field) net.field=host->field

#define HTONLc(net,field) net.field=htonl(net.field)
#define HTONSc(net,field) net.field=htons(net.field)

#define NTOHLc(net,field) net.field=ntohl(net.field)
#define NTOHSc(net,field) net.field=ntohs(net.field)

  void get_dvb_frontend_parameters(struct dvb_frontend_parameters* hfe, vtuner_message_t* netmsg, vtuner_type_t type) {
    memset(hfe, 0, sizeof(hfe));

    hfe->frequency 		= netmsg->body.fe_params.frequency;
    hfe->inversion		= netmsg->body.fe_params.inversion;
    switch (type) {
      case VT_S :
      case VT_S2:
        hfe->u.qpsk.symbol_rate	= netmsg->body.fe_params.u.qpsk.symbol_rate;
        hfe->u.qpsk.fec_inner	= netmsg->body.fe_params.u.qpsk.fec_inner;
        break;
      case VT_C:
        hfe->u.qam.symbol_rate	= netmsg->body.fe_params.u.qam.symbol_rate;
        hfe->u.qam.fec_inner	= netmsg->body.fe_params.u.qam.fec_inner;
        hfe->u.qam.modulation	= netmsg->body.fe_params.u.qam.modulation;
        break;
      case VT_T:
        hfe->u.ofdm.bandwidth			= netmsg->body.fe_params.u.ofdm.bandwidth;
        hfe->u.ofdm.code_rate_HP		= netmsg->body.fe_params.u.ofdm.code_rate_HP;
        hfe->u.ofdm.code_rate_LP		= netmsg->body.fe_params.u.ofdm.code_rate_LP;
        hfe->u.ofdm.constellation		= netmsg->body.fe_params.u.ofdm.constellation;
        hfe->u.ofdm.transmission_mode		= netmsg->body.fe_params.u.ofdm.transmission_mode;
        hfe->u.ofdm.guard_interval		= netmsg->body.fe_params.u.ofdm.guard_interval;
        hfe->u.ofdm.hierarchy_information	= netmsg->body.fe_params.u.ofdm.hierarchy_information;
    }
  }

  void set_dvb_frontend_parameters( vtuner_message_t* netmsg, struct dvb_frontend_parameters* hfe, vtuner_type_t type) {
    netmsg->body.fe_params.frequency		= hfe->frequency;
    netmsg->body.fe_params.inversion		= hfe->inversion;
    switch (type) {
      case VT_S :
      case VT_S2:
        netmsg->body.fe_params.u.qpsk.symbol_rate = hfe->u.qpsk.symbol_rate;
        netmsg->body.fe_params.u.qpsk.fec_inner   = hfe->u.qpsk.fec_inner;
        break;
      case VT_C:
        netmsg->body.fe_params.u.qam.symbol_rate  = hfe->u.qam.symbol_rate;
        netmsg->body.fe_params.u.qam.fec_inner    = hfe->u.qam.fec_inner;
        netmsg->body.fe_params.u.qam.modulation   = hfe->u.qam.modulation;
        break;
      case VT_T:
        netmsg->body.fe_params.u.ofdm.bandwidth                   = hfe->u.ofdm.bandwidth;
        netmsg->body.fe_params.u.ofdm.code_rate_HP                = hfe->u.ofdm.code_rate_HP;
        netmsg->body.fe_params.u.ofdm.code_rate_LP                = hfe->u.ofdm.code_rate_LP;
        netmsg->body.fe_params.u.ofdm.constellation               = hfe->u.ofdm.constellation;
        netmsg->body.fe_params.u.ofdm.transmission_mode           = hfe->u.ofdm.transmission_mode;
        netmsg->body.fe_params.u.ofdm.guard_interval              = hfe->u.ofdm.guard_interval;
        netmsg->body.fe_params.u.ofdm.hierarchy_information       = hfe->u.ofdm.hierarchy_information;
    }
  }

int ntoh_get_message_type( vtuner_net_message_t* netmsg ) {
  int hmsgtype = ntohs(netmsg->msg_type);
  return hmsgtype;
}

void hton_vtuner_net_message(vtuner_net_message_t* netmsg, vtuner_type_t type) {
  ALOGD(" v%d/%x %d %d", netmsg->ver, netmsg->cap, netmsg->msg_type, netmsg->u.vtuner.type );

  switch (netmsg->msg_type) {
    case MSG_GET_FRONTEND:
    case MSG_SET_FRONTEND:
      ALOGD(" %d %d %d %d", netmsg->u.vtuner.body.fe_params.frequency, netmsg->u.vtuner.body.fe_params.inversion, netmsg->u.vtuner.body.fe_params.u.qpsk.symbol_rate, netmsg->u.vtuner.body.fe_params.u.qpsk.fec_inner);
      HTONLc(netmsg->u.vtuner.body.fe_params, frequency);
      switch (type) {
        case VT_S :
        case VT_S2:
        case VT_S|VT_S2:
          ALOGD(" VT_S/VT_S2");
          HTONLc( netmsg->u.vtuner.body.fe_params, u.qpsk.symbol_rate);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.qpsk.fec_inner);
          break;
        case VT_C:
          ALOGD(" VT_C");
          HTONLc( netmsg->u.vtuner.body.fe_params, u.qam.symbol_rate); 
          HTONLc( netmsg->u.vtuner.body.fe_params, u.qam.fec_inner);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.qam.modulation);
          break;
        case VT_T:
          ALOGD(" VT_T");
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.bandwidth);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.code_rate_HP);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.code_rate_LP);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.constellation);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.transmission_mode);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.guard_interval);
          HTONLc( netmsg->u.vtuner.body.fe_params, u.ofdm.hierarchy_information);
	  break;
	default:
          ALOGW( "unkown frontend type %d (known types are %d,%d,%d,%d)\n",type,VT_S,VT_C,VT_T,VT_S2);
      };
      break;
    case MSG_READ_STATUS:
      HTONLc( netmsg->u.vtuner.body, status);
      ALOGD(" %d", netmsg->u.vtuner.body.status);
      break;
    case MSG_READ_BER:
      ALOGD(" %d", netmsg->u.vtuner.body.ber);
      HTONLc( netmsg->u.vtuner.body, ber);
      break;
    case MSG_READ_SIGNAL_STRENGTH:
      ALOGD(" %d", netmsg->u.vtuner.body.ss);
      HTONSc( netmsg->u.vtuner.body, ss);
      break;
    case MSG_READ_SNR:
      ALOGD(" %d", netmsg->u.vtuner.body.snr);
      HTONSc( netmsg->u.vtuner.body, snr);
      break;
    case MSG_READ_UCBLOCKS:
      ALOGD(" %d", netmsg->u.vtuner.body.ucb);
      HTONLc( netmsg->u.vtuner.body, ucb);
      break;
    case MSG_SET_PROPERTY:
    case MSG_GET_PROPERTY:
      HTONLc( netmsg->u.vtuner.body, prop.cmd ); 
      HTONLc( netmsg->u.vtuner.body, prop.u.data );
      break;
    case MSG_PIDLIST: {
      int i;
      for(i=0; i<30; ++i) {
        ALOGD(" %d", netmsg->u.vtuner.body.pidlist[i]);
        HTONSc( netmsg->u.vtuner.body, pidlist[i]);
      }
      break;
    case MSG_DISCOVER:
//      ALOGD(" %d %d %d %d", netmsg->u.discover.port, netmsg->u.discover.fe_info.type, netmsg->u.discover.fe_info.frequency_min, netmsg->u.discover.fe_info.frequency_max);
      HTONSc( netmsg->u.discover, tuner_group);
      HTONSc( netmsg->u.discover, port);
      HTONSc( netmsg->u.discover, tsdata_port);
      HTONLc( netmsg->u.discover, vtype);
      break;
    case MSG_UPDATE:
      HTONLc( netmsg->u.update, status);
      HTONLc( netmsg->u.update, ber);
      HTONLc( netmsg->u.update, ucb);
      HTONSc( netmsg->u.update, ss);
      HTONSc( netmsg->u.update, snr);
      break;    
    default:
      if(netmsg->msg_type < 1 || (netmsg->msg_type > MSG_GET_PROPERTY && netmsg->msg_type < MSG_NULL ) || netmsg->msg_type > MSG_UPDATE)
        ALOGW( "unkown message type %d\n",netmsg->msg_type);
    }
  }

  if(netmsg->msg_type < MSG_DISCOVER) HTONLc( netmsg->u.vtuner, type );
  netmsg->msg_type = htons( netmsg->msg_type );
  netmsg->ver = VTUNER_PROTO2;
  
  ALOGD(" %x %x\n", netmsg->msg_type, netmsg->u.vtuner.type);
  #ifdef DEBUG_NET
    print_vtuner_net_message(netmsg);
  #endif
}

void ntoh_vtuner_net_message(vtuner_net_message_t* netmsg, vtuner_type_t type) {
  #ifdef DEBUG_NET
    print_vtuner_net_message(netmsg);
  #endif
  ALOGD(" v%d/%x %d %d", netmsg->ver, netmsg->cap, netmsg->msg_type, netmsg->u.vtuner.type );

  netmsg->msg_type = ntohs( netmsg->msg_type );
  if(netmsg->msg_type < MSG_DISCOVER) HTONLc( netmsg->u.vtuner, type );
  netmsg->ver = VTUNER_PROTO2;

  ALOGD(" %d %d", netmsg->msg_type, netmsg->u.vtuner.type );

  switch (netmsg->msg_type) {
    case MSG_GET_FRONTEND: 
    case MSG_SET_FRONTEND: 
      NTOHLc( netmsg->u.vtuner.body.fe_params, frequency);
      switch (type) {
        case VT_S :
        case VT_S2:
        case VT_S|VT_S2:
          ALOGD(" VT_S/VT_S2");
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.qpsk.symbol_rate);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.qpsk.fec_inner);
          break;
        case VT_C:
          ALOGD(" VT_C");
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.qam.symbol_rate);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.qam.fec_inner);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.qam.modulation);
          break;
        case VT_T:
          ALOGD(" VT_T");
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.bandwidth);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.code_rate_HP);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.code_rate_LP);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.constellation);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.transmission_mode);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.guard_interval);
          NTOHLc( netmsg->u.vtuner.body.fe_params, u.ofdm.hierarchy_information);
          break;
        default:
          ALOGW("unkown frontend type %d (known types are %d,%d,%d,%d)\n",type,VT_S,VT_C,VT_T,VT_S2);
      }
      ALOGD(" %d %d %d %d", netmsg->u.vtuner.body.fe_params.frequency, netmsg->u.vtuner.body.fe_params.inversion, netmsg->u.vtuner.body.fe_params.u.qpsk.symbol_rate, netmsg->u.vtuner.body.fe_params.u.qpsk.fec_inner);
      break;
    case MSG_READ_STATUS:
      NTOHLc( netmsg->u.vtuner.body, status);
      ALOGD(" %d", netmsg->u.vtuner.body.status);
      break;
    case MSG_READ_BER:
      NTOHLc( netmsg->u.vtuner.body, ber);
      ALOGD(" %d", netmsg->u.vtuner.body.ber);
      break;
    case MSG_READ_SIGNAL_STRENGTH:
      NTOHSc( netmsg->u.vtuner.body, ss);
      ALOGD(" %d", netmsg->u.vtuner.body.ss);
      break;
    case MSG_READ_SNR:
      NTOHSc( netmsg->u.vtuner.body, snr);
      ALOGD(" %d", netmsg->u.vtuner.body.snr);
      break;
    case MSG_READ_UCBLOCKS:
      NTOHLc( netmsg->u.vtuner.body, ucb);
      ALOGD(" %d", netmsg->u.vtuner.body.ucb);
      break;
    case MSG_PIDLIST: {
      int i;
      for(i=0; i<30; ++i) {
        NTOHSc( netmsg->u.vtuner.body, pidlist[i]);
        ALOGD(" %d", netmsg->u.vtuner.body.pidlist[i]);
      }
      break;
    case MSG_SET_PROPERTY: 
    case MSG_GET_PROPERTY: 
      NTOHLc( netmsg->u.vtuner.body, prop.u.data );
      NTOHLc( netmsg->u.vtuner.body, prop.cmd );
      break;
    case MSG_DISCOVER:
      NTOHSc( netmsg->u.discover, tuner_group);
      NTOHSc( netmsg->u.discover, port);
      NTOHSc( netmsg->u.discover, tsdata_port);
      NTOHLc( netmsg->u.discover, vtype);
      break;
    case MSG_UPDATE:
      NTOHLc( netmsg->u.update, status);
      NTOHLc( netmsg->u.update, ber);
      NTOHLc( netmsg->u.update, ucb);
      NTOHSc( netmsg->u.update, ss);
      NTOHSc( netmsg->u.update, snr);
      break;
    default:
      if(netmsg->msg_type < 1 || (netmsg->msg_type > MSG_GET_PROPERTY && netmsg->msg_type < MSG_NULL ) || netmsg->msg_type > MSG_UPDATE)
        ALOGW("unknown message type %d\n",netmsg->msg_type);
    }
  }
  
}

void print_vtuner_net_message(vtuner_net_message_t* netmsg) {
  char* bytes;
  int i;
  bytes=(char*)netmsg;
  ALOGD(" (%d) ",sizeof(vtuner_net_message_t));
  for(i=0; i<sizeof(vtuner_net_message_t); ++i) {
    ALOGD("%x ", bytes[i]);
  }
  
}
