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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "internal.h"

#if defined(OP_ENABLE_ASSERTIONS)
void op_fatal_impl(const char *_str,const char *_file,int _line){
  fprintf(stderr,"Fatal (internal) error in %s, line %i: %s\n",
   _file,_line,_str);
  abort();
}
#endif

/*A version of strncasecmp() that is guaranteed to only ignore the case of
   ASCII characters.*/
int op_strncasecmp(const char *_a,const char *_b,int _n){
  int i;
  for(i=0;i<_n;i++){
    int a;
    int b;
    int d;
    a=_a[i];
    b=_b[i];
    if(a>='a'&&a<='z')a-='a'-'A';
    if(b>='a'&&b<='z')b-='a'-'A';
    d=a-b;
    if(d)return d;
  }
  return 0;
}

/*Compute the total requested gain for a link, before clamping.
  This is shared by the decode path (op_update_gain()) and the reporting API
   (op_get_gain()) so both always agree on the value.*/
opus_int32 op_gain_compute(const OggOpusFile *_of,int _li){
  const OpusHead *head;
  opus_int32      gain_q8;
  gain_q8=_of->gain_offset_q8;
  head=&_of->links[_li].head;
  /*We don't have to worry about overflow here because the header gain and
     track gain must lie in the range [-32768,32767], and the user-supplied
     offset has been pre-clamped to [-98302,98303].*/
  switch(_of->gain_type){
    case OP_ALBUM_GAIN:{
      int album_gain_q8;
      album_gain_q8=0;
      opus_tags_get_album_gain(&_of->links[_li].tags,&album_gain_q8);
      gain_q8+=album_gain_q8;
      gain_q8+=head->output_gain;
    }break;
    case OP_TRACK_GAIN:{
      int track_gain_q8;
      track_gain_q8=0;
      opus_tags_get_track_gain(&_of->links[_li].tags,&track_gain_q8);
      gain_q8+=track_gain_q8;
      gain_q8+=head->output_gain;
    }break;
    case OP_HEADER_GAIN:gain_q8+=head->output_gain;break;
    case OP_ABSOLUTE_GAIN:break;
    default:OP_ASSERT(0);
  }
  return gain_q8;
}

opus_int32 op_gain_clamp(opus_int32 _gain_q8,int *_clamped){
  opus_int32 clamped_q8;
  clamped_q8=OP_CLAMP(-32768,_gain_q8,32767);
  if(_clamped!=NULL)*_clamped=clamped_q8!=_gain_q8;
  return clamped_q8;
}
