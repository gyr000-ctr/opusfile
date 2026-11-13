/*Fixed-point functional check for the Q16 gain branch.
  Verifies: (1) layer off vs on at 0 dB gives identical 16-bit output energy,
            (2) a +6 dB gain raises energy by f^2, f = 10^(6/20).*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <opusfile.h>

static double decode_energy(const char *p,int enable,long g,int *n_out){
  OggOpusFile *of;int err=0;double e=0;int n_total=0;
  of=op_open_file(p,&err);
  if(of==NULL){fprintf(stderr,"open %d\n",err);exit(1);}
  op_set_gain_offset(of,OP_ABSOLUTE_GAIN,(opus_int32)g);
  op_set_gain_enabled(of,enable);
  for(;;){
    opus_int16 pcm[120*48*2];int n,i,ch;
    n=op_read(of,pcm,sizeof(pcm)/sizeof(*pcm),NULL);
    if(n<0){fprintf(stderr,"decode %d\n",n);exit(1);}
    if(n==0)break;
    ch=op_channel_count(of,-1);
    for(i=0;i<n*ch;i++){double v=pcm[i];e+=v*v;}
    n_total+=n*ch;
  }
  *n_out=n_total;op_free(of);return e;
}

int main(int argc,char**argv){
  int n0=0,n1=0,ng=0;
  double e_off,e_on0,e_g,f,expect_ratio;
  if(argc!=2){fprintf(stderr,"need input\n");return 2;}
  e_off=decode_energy(argv[1],0,0,&n0);
  e_on0=decode_energy(argv[1],1,0,&n1);
  e_g  =decode_energy(argv[1],1,6*256,&ng);
  f=pow(10.0,6.0/20.0);
  expect_ratio=f*f;
  printf("fixed-point samples: off=%d on0=%d on+6dB=%d\n",n0,n1,ng);
  printf("energy off(0dB)=%.0f  on(0dB)=%.0f  identical=%s\n",
   e_off,e_on0,e_off==e_on0?"YES":"NO");
  printf("energy ratio +6dB/0dB = %.5f (expect %.5f, err %.2e)\n",
   e_g/e_on0,expect_ratio,fabs(e_g/e_on0-expect_ratio)/expect_ratio);
  if(e_off!=e_on0)return 1;
  if(fabs(e_g/e_on0-expect_ratio)/expect_ratio>0.02)return 1;
  printf("FIXED-POINT Q16 GAIN OK\n");
  return 0;
}
