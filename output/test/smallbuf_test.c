/*Exercises the small-buffer decode path.
  When the caller's buffer cannot hold a whole packet, the library decodes into
   its internal od_buffer and hands the samples back in pieces.  The gain layer
   still runs exactly once, in place, when the packet is decoded.
  This decodes the stereo input with a deliberately tiny buffer (gain layer
   on, absolute gain g) so the od_buffer path is forced on every packet.*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opusfile.h>

int main(int _argc,const char **_argv){
  OggOpusFile *of;
  FILE        *fp;
  int          err;
  long         g;
  if(_argc!=4){fprintf(stderr,"usage: %s <in.opus> <gain_q8> <out.raw>\n",
   _argv[0]);return 2;}
  g=strtol(_argv[2],NULL,10);
  err=0;
  of=op_open_file(_argv[1],&err);
  if(of==NULL){fprintf(stderr,"open failed %d\n",err);return 1;}
  if(op_set_gain_offset(of,OP_ABSOLUTE_GAIN,(opus_int32)g)!=0)return 1;
  op_set_gain_enabled(of,1);
  fp=fopen(_argv[3],"wb");
  if(fp==NULL)return 1;
  for(;;){
    /*Only 256 samples per channel per call: far smaller than any Opus packet,
       forcing decode into the internal od_buffer every time.*/
    float pcm[256*2];
    int   n;
    n=op_read_float(of,pcm,sizeof(pcm)/sizeof(*pcm),NULL);
    if(n<0){fprintf(stderr,"decode error %d\n",n);return 1;}
    if(n==0)break;
    if(fwrite(pcm,sizeof(float),(size_t)n*2,fp)!=(size_t)n*2)return 1;
  }
  fclose(fp);
  printf("small-buffer decode complete, clamp_count=%lld\n",
   (long long)op_gain_clamp_count(of));
  op_free(of);
  return 0;
}
