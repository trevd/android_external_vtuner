#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <linux/dvb/frontend.h>

int main(int argc, char **argv) {

  int i;
  //for(i=0; i<4; ++i) {
    char devstr[80];
    //sfprintf(stderr, devstr, , i);

    int fe_fd = open( "/dev/dvb0.frontend0", O_RDWR);
    fprintf(stderr,"fe_fd=%d\n",fe_fd);
    if(fe_fd>0) {
      struct dvb_frontend_info fe_info;
       ioctl(fe_fd, FE_GET_INFO, &fe_info);
        //fprintf(stderr,"// dvb_frontend_info for %s\n", devstr);
        fprintf(stderr,"struct dvb_frontend_info FETYPE = {\n");
        fprintf(stderr,"  .name		         = \"%s\",\n", fe_info.name); 
        fprintf(stderr,"  .type                  = %d,\n", fe_info.type);
        fprintf(stderr,"  .frequency_min         = %d,\n", fe_info.frequency_min);
        fprintf(stderr,"  .frequency_max         = %d,\n", fe_info.frequency_max);
        fprintf(stderr,"  .frequency_stepsize    = %d,\n", fe_info.frequency_stepsize);
        fprintf(stderr,"  .frequency_tolerance   = %d,\n", fe_info.frequency_tolerance);
        fprintf(stderr,"  .symbol_rate_min       = %d,\n", fe_info.symbol_rate_min);
        fprintf(stderr,"  .symbol_rate_max       = %d,\n", fe_info.symbol_rate_max);
        fprintf(stderr,"  .symbol_rate_tolerance = %d,\n", fe_info.symbol_rate_tolerance);
        fprintf(stderr,"  .notifier_delay        = %d,\n", fe_info.notifier_delay);
        fprintf(stderr,"  .caps                  = 0x%x\n", fe_info.caps);
        fprintf(stderr,"};\n");
      }
    
  //}
}
