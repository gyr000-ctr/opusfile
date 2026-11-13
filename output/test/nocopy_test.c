/*Runtime no-copy proof for the gain layer.
  A decode callback fills the decoder's output buffer with a known constant and
   records its address.  After op_read_float() returns we verify:
    1. the buffer the callback filled is the very same allocation the caller
       received (pointer identity -> no intermediate copy), and
    2. every returned sample equals the gain layer's linear factor applied to
       the constant (the in-place scale happened on that same memory).*/
#include <stdio.h>
#include <math.h>
#include <opusfile.h>

typedef struct{
  void *decoder_pcm;
  int   calls;
}cb_ctx;

static int fill_constant(void *_ctx,OpusMSDecoder *_decoder,void *_pcm,
 const ogg_packet *_op,int _nsamples,int _nchannels,int _format,int _li){
  cb_ctx *c;
  float  *f;
  int     i;
  (void)_decoder;(void)_op;(void)_format;(void)_li;
  c=(cb_ctx *)_ctx;
  c->decoder_pcm=_pcm;
  c->calls++;
  f=(float *)_pcm;
  for(i=0;i<_nsamples*_nchannels;i++)f[i]=1.0f;
  return 0;
}

int main(int _argc,const char **_argv){
  OggOpusFile *of;
  cb_ctx       ctx;
  float        user[120*48*2];
  opus_int32   gain_q8;
  double       factor;
  int          err;
  int          n,i,bad;
  if(_argc!=2){fprintf(stderr,"need input path\n");return 2;}
  err=0;
  of=op_open_file(_argv[1],&err);
  if(of==NULL){fprintf(stderr,"open failed\n");return 1;}
  gain_q8=6*256;
  if(op_set_gain_offset(of,OP_ABSOLUTE_GAIN,gain_q8)!=0)return 1;
  op_set_gain_enabled(of,1);
  ctx.decoder_pcm=NULL;ctx.calls=0;
  op_set_decode_callback(of,fill_constant,&ctx);
  factor=pow(10.0,(double)gain_q8/(20.0*256.0));
  n=op_read_float(of,user,sizeof(user)/sizeof(*user),NULL);
  if(n<=0){fprintf(stderr,"no samples: %d\n",n);return 1;}
  printf("decode callback fills: %p\n",(void *)ctx.decoder_pcm);
  printf("caller output buffer : %p\n",(void *)user);
  printf("pointer identical    : %s\n",
   ctx.decoder_pcm==(void *)user?"YES (same allocation, no copy)":"NO");
  bad=0;
  for(i=0;i<n*op_channel_count(of,-1);i++){
    if(fabs((double)user[i]-factor)>1e-6){bad++;if(bad<4)
      printf("mismatch [%d]=%.8f want %.8f\n",i,user[i],factor);}
  }
  printf("in-place scale check : %s (%d bad of %d), factor=%.7f\n",
   bad==0?"OK":"FAIL",bad,n*op_channel_count(of,-1),factor);
  op_free(of);
  return (ctx.decoder_pcm==(void *)user&&bad==0)?0:1;
}
