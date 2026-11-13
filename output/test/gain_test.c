/*Verification harness for the software gain layer.
  This is a test program (not part of the library).  It is kept in its own
   directory and receives all untrusted input paths as arguments.

  Modes:
    identity  <in> <off.raw> <on.raw>
        Decode twice at an explicit 0 dB absolute gain, once with the layer off
        and once on.  The two raw outputs must be sample-for-sample identical.
    clamp     <in> <gain_q8>
        Set an out-of-range absolute gain, decode with the layer on, and print
        the clamp report.
    apply     <in> <gain_q8> <out.raw>
        Decode with the layer on and the given absolute gain, for external
        energy comparison across files.*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <opusfile.h>

static OggOpusFile *open_layer(const char *_path,int _enable,int _set_abs,
 opus_int32 _abs_gain_q8){
  OggOpusFile *of;
  int          err;
  of=op_open_file(_path,&err);
  if(of==NULL){fprintf(stderr,"open failed: %d\n",err);return NULL;}
  if(_set_abs&&op_set_gain_offset(of,OP_ABSOLUTE_GAIN,_abs_gain_q8)!=0){
    fprintf(stderr,"set_gain_offset failed\n");return NULL;
  }
  op_set_gain_enabled(of,_enable);
  return of;
}

static int decode_raw(OggOpusFile *_of,const char *_out){
  FILE *fp;
  int   nchannels;
  fp=fopen(_out,"wb");
  if(fp==NULL){fprintf(stderr,"cannot open %s\n",_out);return 1;}
  nchannels=op_channel_count(_of,-1);
  if(nchannels<=0){fprintf(stderr,"bad channel count\n");return 1;}
  for(;;){
    float pcm[120*48*2];
    int   n;
    n=op_read_float(_of,pcm,sizeof(pcm)/sizeof(*pcm),NULL);
    if(n<0){fprintf(stderr,"decode error %d\n",n);return 1;}
    if(n==0)break;
    if(fwrite(pcm,sizeof(float),(size_t)n*nchannels,fp)
     !=(size_t)n*nchannels){
      fprintf(stderr,"short write\n");return 1;
    }
  }
  fclose(fp);
  return 0;
}

int main(int _argc,const char **_argv){
  const char *mode;
  if(_argc<2){fprintf(stderr,"need a mode\n");return 2;}
  mode=_argv[1];
  if(strcmp(mode,"identity")==0){
    OggOpusFile *a,*b;
    opus_int64  ca,cb;
    if(_argc!=5){fprintf(stderr,"identity needs 3 args\n");return 2;}
    /*0 dB absolute gain on both runs; only the layer switch differs.*/
    a=open_layer(_argv[2],0,1,0);
    b=open_layer(_argv[2],1,1,0);
    if(a==NULL||b==NULL)return 1;
    if(decode_raw(a,_argv[3])||decode_raw(b,_argv[4]))return 1;
    ca=op_gain_clamp_count(a);
    cb=op_gain_clamp_count(b);
    printf("identity: clamp_count off=%lld on=%lld\n",
     (long long)ca,(long long)cb);
    op_free(a);op_free(b);
    return 0;
  }
  if(strcmp(mode,"clamp")==0){
    OggOpusFile *of;
    opus_int64   count;
    opus_int32   req,app;
    int          rep;
    long         g;
    if(_argc!=4){fprintf(stderr,"clamp needs 2 args\n");return 2;}
    g=strtol(_argv[3],NULL,10);
    of=open_layer(_argv[2],1,1,(opus_int32)g);
    if(of==NULL)return 1;
    if(decode_raw(of,"/dev/null"))return 1;
    count=op_gain_clamp_count(of);
    req=app=0;
    rep=op_gain_clamp_last(of,&req,&app);
    printf("clamp: requested=%ld report=%d count=%lld last_req=%d "
     "last_applied=%d\n",(long)g,rep,(long long)count,(int)req,(int)app);
    op_free(of);
    return 0;
  }
  if(strcmp(mode,"apply")==0){
    OggOpusFile *of;
    long         g;
    if(_argc!=5){fprintf(stderr,"apply needs 3 args\n");return 2;}
    g=strtol(_argv[3],NULL,10);
    of=open_layer(_argv[2],1,1,(opus_int32)g);
    if(of==NULL)return 1;
    if(decode_raw(of,_argv[4]))return 1;
    printf("apply: gain_q8=%ld clamp_count=%lld out=%s\n",(long)g,
     (long long)op_gain_clamp_count(of),_argv[4]);
    op_free(of);
    return 0;
  }
  fprintf(stderr,"unknown mode %s\n",mode);
  return 2;
}
