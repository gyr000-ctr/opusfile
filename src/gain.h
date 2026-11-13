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
/*Common gain layer.
  This module owns the single piece of state describing the gain to apply to
   decoded output, and provides the in-place routines used to apply it directly
   to the buffer the decoder wrote into.
  All decode entry points (op_read(), op_read_float(), and their stereo
   variants) funnel through these routines, so the gain is applied exactly once
   and never requires a second, full-size copy of the output.*/
#if !defined(_gain_h)
# define _gain_h (1)

# include "internal.h"

/*The legal range of a gain value, in 1/256ths of a dB.
  These match the range accepted by the libopus OPUS_SET_GAIN ctl: the total
   gain (header/track gain plus any user offset) is always clamped to this
   range before being applied.*/
# define OP_GAIN_Q8_MIN (-32768)
# define OP_GAIN_Q8_MAX (32767)

/*Clamp a gain, in 1/256ths of a dB, to the legal range.
  If the value had to be clamped, the clamp statistics in the OggOpusFile are
   updated exactly once per call.*/
opus_int32 op_gain_clamp(OggOpusFile *_of,opus_int32 _gain_q8);

/*Compute the linear scale factor equivalent to the given Q8 dB gain.
  This is pow(10,_gain_q8/(20.0*256)), computed in double precision so the
   float and fixed-point output paths share one implementation.*/
double op_gain_to_linear(opus_int32 _gain_q8);

/*Apply a linear gain directly to the decoded samples in _pcm.
  The samples are scaled in place: no scratch buffer is allocated and the data
   is not copied.
  _pcm must contain _nsamples*_nchannels interleaved op_sample values.*/
void op_gain_apply(OggOpusFile *_of,op_sample *_pcm,
 int _nsamples,int _nchannels);

#endif
