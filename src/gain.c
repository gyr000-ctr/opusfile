/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE libopusfile SOFTWARE CODEC SOURCE CODE. *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE libopusfile SOURCE CODE IS (C) COPYRIGHT 2012-2020           *
 * by the Xiph.Org Foundation and contributors https://xiph.org/    *
 *                                                                  *
 ********************************************************************/
#include "gain.h"

#include <math.h>

opus_int32 op_gain_clamp(OggOpusFile *_of,opus_int32 _gain_q8){
  opus_int32 clamped_q8;
  clamped_q8=OP_CLAMP(OP_GAIN_Q8_MIN,_gain_q8,OP_GAIN_Q8_MAX);
  /*Record every out-of-range request so the application can report it.
    A request equal to either bound is legal and does not count.*/
  if(clamped_q8!=_gain_q8){
    _of->gain_clamp_count++;
    _of->gain_last_requested_q8=_gain_q8;
    _of->gain_last_applied_q8=clamped_q8;
  }
  return clamped_q8;
}

double op_gain_to_linear(opus_int32 _gain_q8){
  /*The Q8 dB value is divided by 256 to get dB, and then converted to a
     linear amplitude factor with the standard 20*log10() definition.*/
  return pow(10.0,(double)_gain_q8/(20.0*256.0));
}

void op_gain_apply(OggOpusFile *_of,op_sample *_pcm,
 int _nsamples,int _nchannels){
  int             nsamples_total;
  int             i;
#if defined(OP_FIXED_POINT)
  /*The decoder output is signed 16-bit PCM.
    We keep the linear factor in Q16 and widen the multiply to 32 bits, which
     has ample headroom for the full legal gain range.
    Round to nearest by hand so this does not depend on lrintf()/libm, which a
     fixed-point build may not link.*/
  double           scale;
  opus_int32       scale_q16;
#else
  float            scale;
#endif
  nsamples_total=_nsamples*_nchannels;
#if defined(OP_FIXED_POINT)
  scale=op_gain_to_linear(_of->gain_applied_q8)*65536.0;
  scale_q16=(opus_int32)(scale<0?scale-0.5:scale+0.5);
  for(i=0;i<nsamples_total;i++){
    opus_int32 scaled;
    scaled=((opus_int32)_pcm[i]*scale_q16+(1<<15))>>16;
    _pcm[i]=(op_sample)OP_CLAMP(-32768,scaled,32767);
  }
#else
  scale=(float)op_gain_to_linear(_of->gain_applied_q8);
  for(i=0;i<nsamples_total;i++)_pcm[i]*=scale;
#endif
}
