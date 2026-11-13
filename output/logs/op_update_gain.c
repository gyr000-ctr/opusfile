static void op_update_gain(OggOpusFile *_of){
  opus_int32 gain_q8;
  int        li;
  /*If decode isn't ready, then we'll apply the gain when we initialize the
     decoder.*/
  if(_of->ready_state<OP_INITSET)return;
  li=_of->seekable?_of->cur_link:0;
  /*When gain application is disabled, bypass the header/tag/offset gains
     entirely and decode exactly as captured.*/
  gain_q8=_of->gain_enabled?op_gain_compute(_of,li):0;
  gain_q8=op_gain_clamp(gain_q8,NULL);
  OP_ASSERT(_of->od!=NULL);
#if defined(OPUS_SET_GAIN)
  opus_multistream_decoder_ctl(_of->od,OPUS_SET_GAIN(gain_q8));
#else
/*A fallback that works with both float and fixed-point is a bunch of work,
   so just force people to use a sufficiently new version.
  This is deployed well enough at this point that this shouldn't be a burden.*/
# error "libopus 1.0.1 or later required"
#endif
}
