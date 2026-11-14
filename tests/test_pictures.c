/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE libopusfile SOFTWARE CODEC SOURCE CODE. *
 * USE, DISTRIBUTION AND REDISTRIBUTION OF THIS LIBRARY SOURCE IS   *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE libopusfile SOURCE CODE IS (C) COPYRIGHT 2012-2020           *
 * by the Xiph.Org Foundation and contributors https://xiph.org/    *
 *                                                                  *
 ********************************************************************/
/*Tests for the METADATA_BLOCK_PICTURE support (op_picture_count()/
   op_picture()).
  These build complete Ogg Opus streams in memory (using libogg to mux and
   libopus to encode valid audio packets) and verify that
    - a single cover image is parsed and exposed,
    - multiple pictures are all exposed, in order,
    - malformed BASE64 and malformed picture blocks are skipped without
       losing the other tags,
    - oversized fields (dimensions, depth, data length) are rejected,
    - pictures work independently per link in chained streams,
    - the existing op_tags()/seek/decode behavior is unchanged.*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <opus.h>
#include <opusfile.h>

static int nchecks;
static int nfailures;

#define CHECK(_cond) \
  do{ \
    nchecks++; \
    if(!(_cond)){ \
      nfailures++; \
      fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#_cond); \
    } \
  }while(0)

/*--------------------------------------------------------------------
  Growable byte buffer.
 *--------------------------------------------------------------------*/
typedef struct{
  unsigned char *data;
  size_t         len;
  size_t         cap;
}buf;

static void buf_append(buf *_b,const void *_data,size_t _len){
  if(_b->len+_len>_b->cap){
    size_t cap;
    cap=_b->cap==0?4096:_b->cap;
    while(cap<_b->len+_len)cap<<=1;
    _b->data=(unsigned char *)realloc(_b->data,cap);
    if(_b->data==NULL){
      fprintf(stderr,"Out of memory.\n");
      exit(2);
    }
    _b->cap=cap;
  }
  memcpy(_b->data+_b->len,_data,_len);
  _b->len+=_len;
}

static void buf_append_u32le(buf *_b,ogg_uint32_t _v){
  unsigned char p[4];
  p[0]=(unsigned char)_v;
  p[1]=(unsigned char)(_v>>8);
  p[2]=(unsigned char)(_v>>16);
  p[3]=(unsigned char)(_v>>24);
  buf_append(_b,p,4);
}

static void buf_append_u32be(buf *_b,ogg_uint32_t _v){
  unsigned char p[4];
  p[0]=(unsigned char)(_v>>24);
  p[1]=(unsigned char)(_v>>16);
  p[2]=(unsigned char)(_v>>8);
  p[3]=(unsigned char)_v;
  buf_append(_b,p,4);
}

static void buf_clear(buf *_b){
  free(_b->data);
  _b->data=NULL;
  _b->len=_b->cap=0;
}

/*--------------------------------------------------------------------
  BASE64 encoding (for building METADATA_BLOCK_PICTURE tags).
 *--------------------------------------------------------------------*/
static const char B64[]=
 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char *base64_encode(const unsigned char *_data,size_t _len){
  size_t out_len;
  char  *out;
  size_t i;
  size_t o;
  out_len=4*((_len+2)/3);
  out=(char *)malloc(out_len+1);
  if(out==NULL){
    fprintf(stderr,"Out of memory.\n");
    exit(2);
  }
  o=0;
  for(i=0;i+3<=_len;i+=3){
    unsigned v;
    v=_data[i]<<16|_data[i+1]<<8|_data[i+2];
    out[o++]=B64[v>>18&63];
    out[o++]=B64[v>>12&63];
    out[o++]=B64[v>>6&63];
    out[o++]=B64[v&63];
  }
  if(i<_len){
    unsigned v;
    v=_data[i]<<16;
    if(i+1<_len)v|=_data[i+1]<<8;
    out[o++]=B64[v>>18&63];
    out[o++]=B64[v>>12&63];
    out[o++]=i+1<_len?B64[v>>6&63]:'=';
    out[o++]='=';
  }
  out[o]='\0';
  return out;
}

/*--------------------------------------------------------------------
  METADATA_BLOCK_PICTURE tag construction.
 *--------------------------------------------------------------------*/

/*Build a "METADATA_BLOCK_PICTURE=<base64>" tag string from the individual
   picture block fields.*/
static char *make_picture_tag(ogg_uint32_t _type,const char *_mime,
 const char *_description,ogg_uint32_t _width,ogg_uint32_t _height,
 ogg_uint32_t _depth,ogg_uint32_t _colors,
 const unsigned char *_data,ogg_uint32_t _data_len){
  buf    block;
  char  *b64;
  char  *tag;
  size_t tag_len;
  block.data=NULL;
  block.len=block.cap=0;
  buf_append_u32be(&block,_type);
  buf_append_u32be(&block,(ogg_uint32_t)strlen(_mime));
  buf_append(&block,_mime,strlen(_mime));
  buf_append_u32be(&block,(ogg_uint32_t)strlen(_description));
  buf_append(&block,_description,strlen(_description));
  buf_append_u32be(&block,_width);
  buf_append_u32be(&block,_height);
  buf_append_u32be(&block,_depth);
  buf_append_u32be(&block,_colors);
  buf_append_u32be(&block,_data_len);
  if(_data_len>0)buf_append(&block,_data,_data_len);
  b64=base64_encode(block.data,block.len);
  buf_clear(&block);
  tag_len=strlen("METADATA_BLOCK_PICTURE=")+strlen(b64)+1;
  tag=(char *)malloc(tag_len);
  if(tag==NULL){
    fprintf(stderr,"Out of memory.\n");
    exit(2);
  }
  strcpy(tag,"METADATA_BLOCK_PICTURE=");
  strcat(tag,b64);
  free(b64);
  return tag;
}

/*--------------------------------------------------------------------
  Sample image data.
 *--------------------------------------------------------------------*/

/*A minimal PNG with an IHDR for a _width x _height 32-bit RGBA image.
  The CRCs are bogus, but nothing we test validates them.*/
static size_t make_png(unsigned char *_out,ogg_uint32_t _width,
 ogg_uint32_t _height){
  static const unsigned char sig[8]={0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
  size_t n;
  n=0;
  memcpy(_out+n,sig,8);
  n+=8;
  _out[n++]=0;_out[n++]=0;_out[n++]=0;_out[n++]=13;
  memcpy(_out+n,"IHDR",4);
  n+=4;
  _out[n++]=(unsigned char)(_width>>24);
  _out[n++]=(unsigned char)(_width>>16);
  _out[n++]=(unsigned char)(_width>>8);
  _out[n++]=(unsigned char)_width;
  _out[n++]=(unsigned char)(_height>>24);
  _out[n++]=(unsigned char)(_height>>16);
  _out[n++]=(unsigned char)(_height>>8);
  _out[n++]=(unsigned char)_height;
  _out[n++]=8;/*Bit depth.*/
  _out[n++]=6;/*Color type: RGBA.*/
  _out[n++]=0;_out[n++]=0;_out[n++]=0;
  memset(_out+n,0,4);/*CRC.*/
  n+=4;
  _out[n++]=0;_out[n++]=0;_out[n++]=0;_out[n++]=0;
  memcpy(_out+n,"IEND",4);
  n+=4;
  memset(_out+n,0,4);/*CRC.*/
  n+=4;
  return n;
}

/*A minimal JPEG with a SOF0 segment for a _width x _height 24-bit image.*/
static size_t make_jpeg(unsigned char *_out,ogg_uint32_t _width,
 ogg_uint32_t _height){
  size_t n;
  n=0;
  _out[n++]=0xFF;_out[n++]=0xD8;/*SOI.*/
  _out[n++]=0xFF;_out[n++]=0xC0;/*SOF0.*/
  _out[n++]=0;_out[n++]=17;/*Segment length.*/
  _out[n++]=8;/*Precision.*/
  _out[n++]=(unsigned char)(_height>>8);
  _out[n++]=(unsigned char)_height;
  _out[n++]=(unsigned char)(_width>>8);
  _out[n++]=(unsigned char)_width;
  _out[n++]=3;/*Components.*/
  memset(_out+n,0,9);/*Component specs.*/
  n+=9;
  _out[n++]=0xFF;_out[n++]=0xD9;/*EOI.*/
  return n;
}

/*A minimal GIF89a header for a _width x _height image.*/
static size_t make_gif(unsigned char *_out,ogg_uint32_t _width,
 ogg_uint32_t _height){
  size_t n;
  memcpy(_out,"GIF89a",6);
  n=6;
  _out[n++]=(unsigned char)_width;
  _out[n++]=(unsigned char)(_width>>8);
  _out[n++]=(unsigned char)_height;
  _out[n++]=(unsigned char)(_height>>8);
  _out[n++]=0xF7;/*Global color table, 256 colors.*/
  _out[n++]=0;_out[n++]=0;
  memset(_out+n,0,6);/*Start of a (tiny) global color table.*/
  n+=6;
  return n;
}

/*--------------------------------------------------------------------
  Ogg Opus stream construction.
 *--------------------------------------------------------------------*/

/*Append one whole link (headers + _nframes 20 ms silence packets) to _out.*/
static void build_link(buf *_out,ogg_uint32_t _serialno,
 const char *const *_tags,int _ntags,int _nframes){
  ogg_stream_state os;
  ogg_packet       op;
  ogg_page         og;
  OpusEncoder     *enc;
  unsigned char    head[19];
  buf              tags;
  opus_int16       pcm[960*2];
  unsigned char    packet[4096];
  int              err;
  int              ci;
  int              fi;
  ogg_stream_init(&os,(int)_serialno);
  /*ID header: version 1, stereo, pre-skip 0, 48 kHz, gain 0, family 0.*/
  memcpy(head,"OpusHead",8);
  head[8]=1;
  head[9]=2;
  memset(head+10,0,9);
  head[18]=0;
  memset(&op,0,sizeof(op));
  op.packet=head;
  op.bytes=19;
  op.b_o_s=1;
  op.granulepos=0;
  op.packetno=0;
  ogg_stream_packetin(&os,&op);
  /*Comment header.*/
  tags.data=NULL;
  tags.len=tags.cap=0;
  buf_append(&tags,"OpusTags",8);
  buf_append_u32le(&tags,8);
  buf_append(&tags,"opusfile",8);
  buf_append_u32le(&tags,(ogg_uint32_t)_ntags);
  for(ci=0;ci<_ntags;ci++){
    buf_append_u32le(&tags,(ogg_uint32_t)strlen(_tags[ci]));
    buf_append(&tags,_tags[ci],strlen(_tags[ci]));
  }
  memset(&op,0,sizeof(op));
  op.packet=tags.data;
  op.bytes=(long)tags.len;
  op.granulepos=0;
  op.packetno=1;
  ogg_stream_packetin(&os,&op);
  while(ogg_stream_flush(&os,&og)){
    buf_append(_out,og.header,og.header_len);
    buf_append(_out,og.body,og.body_len);
  }
  buf_clear(&tags);
  /*Audio packets: _nframes frames of digital silence.*/
  enc=opus_encoder_create(48000,2,OPUS_APPLICATION_AUDIO,&err);
  if(err!=OPUS_OK){
    fprintf(stderr,"Failed to create Opus encoder: %s\n",opus_strerror(err));
    exit(2);
  }
  memset(pcm,0,sizeof(pcm));
  for(fi=0;fi<_nframes;fi++){
    int nb;
    nb=opus_encode(enc,pcm,960,packet,(opus_int32)sizeof(packet));
    if(nb<0){
      fprintf(stderr,"Opus encode failed: %s\n",opus_strerror(nb));
      exit(2);
    }
    memset(&op,0,sizeof(op));
    op.packet=packet;
    op.bytes=nb;
    op.granulepos=(fi+1)*960;
    op.e_o_s=fi==_nframes-1;
    op.packetno=2+fi;
    ogg_stream_packetin(&os,&op);
    while(ogg_stream_pageout(&os,&og)){
      buf_append(_out,og.header,og.header_len);
      buf_append(_out,og.body,og.body_len);
    }
  }
  while(ogg_stream_flush(&os,&og)){
    buf_append(_out,og.header,og.header_len);
    buf_append(_out,og.body,og.body_len);
  }
  opus_encoder_destroy(enc);
  ogg_stream_clear(&os);
}

/*--------------------------------------------------------------------
  Unseekable memory stream callbacks.
 *--------------------------------------------------------------------*/
typedef struct{
  const unsigned char *data;
  size_t               size;
  size_t               pos;
}mem_stream;

static int mem_read(void *_stream,unsigned char *_ptr,int _buf_size){
  mem_stream *ms;
  size_t      n;
  ms=(mem_stream *)_stream;
  n=ms->size-ms->pos;
  if(n>(size_t)_buf_size)n=(size_t)_buf_size;
  memcpy(_ptr,ms->data+ms->pos,n);
  ms->pos+=n;
  return (int)n;
}

/*--------------------------------------------------------------------
  The tests.
 *--------------------------------------------------------------------*/

static void test_single_cover(void){
  unsigned char png[64];
  size_t        png_len;
  char         *pic_tag;
  const char   *tags[2];
  buf           file;
  OggOpusFile  *of;
  const OpusPictureTag *pic;
  const OpusPictureTag *pic2;
  const OpusTags       *ot;
  int           err;
  fprintf(stderr,"test_single_cover...\n");
  png_len=make_png(png,32,32);
  pic_tag=make_picture_tag(1,"image/png","Icon",0,0,0,0,png,
   (ogg_uint32_t)png_len);
  tags[0]="TITLE=Cover Test";
  tags[1]=pic_tag;
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,1111,tags,2,3);
  of=op_open_memory(file.data,file.len,&err);
  CHECK(of!=NULL);
  if(of==NULL)return;
  /*The picture was parsed exactly once at open time.*/
  CHECK(op_picture_count(of,0)==1);
  CHECK(op_picture_count(of,-1)==1);
  pic=op_picture(of,0,0);
  CHECK(pic!=NULL);
  if(pic!=NULL){
    CHECK(pic->type==1);
    CHECK(pic->format==OP_PIC_FORMAT_PNG);
    CHECK(strcmp(pic->mime_type,"image/png")==0);
    CHECK(strcmp(pic->description,"Icon")==0);
    /*The parser extracts the real parameters from the PNG data.*/
    CHECK(pic->width==32);
    CHECK(pic->height==32);
    CHECK(pic->depth==32);
    CHECK(pic->data_length==png_len);
    CHECK(pic->data!=NULL&&memcmp(pic->data,png,png_len)==0);
  }
  /*Repeated calls return the same storage: the data is not re-parsed or
     copied per call.*/
  pic2=op_picture(of,0,0);
  CHECK(pic2==pic);
  /*Out-of-range indices fail cleanly.*/
  CHECK(op_picture(of,0,1)==NULL);
  CHECK(op_picture(of,0,-1)==NULL);
  /*The raw tags are all still there, unchanged.*/
  ot=op_tags(of,0);
  CHECK(ot!=NULL);
  if(ot!=NULL){
    CHECK(ot->comments==2);
    CHECK(opus_tags_query_count(ot,"METADATA_BLOCK_PICTURE")==1);
    CHECK(opus_tags_query(ot,"TITLE",0)!=NULL
     &&strcmp(opus_tags_query(ot,"TITLE",0),"Cover Test")==0);
  }
  /*The ID header is unaffected.*/
  CHECK(op_head(of,0)!=NULL&&op_head(of,0)->channel_count==2);
  /*Total duration: 3 frames of 960 samples with no pre-skip.*/
  CHECK(op_pcm_total(of,-1)==3*960);
  op_free(of);
  free(pic_tag);
  buf_clear(&file);
}

static void test_multiple_pictures(void){
  unsigned char  png[64];
  unsigned char  jpg[64];
  size_t         png_len;
  size_t         jpg_len;
  const char    *url;
  char          *tag0;
  char          *tag1;
  char          *tag2;
  const char    *tags[3];
  buf            file;
  OggOpusFile   *of;
  const OpusPictureTag *pic;
  int            err;
  fprintf(stderr,"test_multiple_pictures...\n");
  png_len=make_png(png,32,32);
  jpg_len=make_jpeg(jpg,64,48);
  url="https://example.com/artist.png";
  tag0=make_picture_tag(3,"image/png","Front cover",0,0,0,0,png,
   (ogg_uint32_t)png_len);
  tag1=make_picture_tag(4,"image/jpeg","Back cover",0,0,0,0,jpg,
   (ogg_uint32_t)jpg_len);
  tag2=make_picture_tag(0,"-->","",0,0,0,0,(const unsigned char *)url,
   (ogg_uint32_t)strlen(url));
  tags[0]=tag0;
  tags[1]=tag1;
  tags[2]=tag2;
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,2222,tags,3,3);
  of=op_open_memory(file.data,file.len,&err);
  CHECK(of!=NULL);
  if(of==NULL)return;
  CHECK(op_picture_count(of,0)==3);
  pic=op_picture(of,0,0);
  CHECK(pic!=NULL&&pic->type==3&&pic->format==OP_PIC_FORMAT_PNG
   &&strcmp(pic->description,"Front cover")==0);
  pic=op_picture(of,0,1);
  CHECK(pic!=NULL&&pic->type==4&&pic->format==OP_PIC_FORMAT_JPEG
   &&pic->width==64&&pic->height==48&&pic->depth==24);
  pic=op_picture(of,0,2);
  CHECK(pic!=NULL&&pic->format==OP_PIC_FORMAT_URL
   &&pic->data_length==strlen(url)
   &&memcmp(pic->data,url,strlen(url))==0
   &&pic->data[pic->data_length]=='\0');
  op_free(of);
  free(tag0);
  free(tag1);
  free(tag2);
  buf_clear(&file);
}

static void test_bad_base64(void){
  unsigned char  png[64];
  size_t         png_len;
  char          *good_tag;
  /*A truncated picture block: declares more fields than it contains.*/
  unsigned char  truncated[16];
  char          *trunc_b64;
  char          *trunc_tag;
  const char    *tags[7];
  buf            file;
  OggOpusFile   *of;
  const OpusTags *ot;
  int            err;
  fprintf(stderr,"test_bad_base64...\n");
  png_len=make_png(png,32,32);
  good_tag=make_picture_tag(3,"image/png","",0,0,0,0,png,
   (ogg_uint32_t)png_len);
  memset(truncated,0,sizeof(truncated));
  trunc_b64=base64_encode(truncated,sizeof(truncated));
  trunc_tag=(char *)malloc(strlen("METADATA_BLOCK_PICTURE=")
   +strlen(trunc_b64)+1);
  strcpy(trunc_tag,"METADATA_BLOCK_PICTURE=");
  strcat(trunc_tag,trunc_b64);
  tags[0]="ARTIST=Someone";
  /*Invalid BASE64 characters.*/
  tags[1]="METADATA_BLOCK_PICTURE=!!!!";
  /*Length not a multiple of 4.*/
  tags[2]="METADATA_BLOCK_PICTURE=QUJD";
  /*Decodes to fewer bytes than the minimum picture block size.*/
  tags[3]="METADATA_BLOCK_PICTURE=QUJDRA==";
  /*A truncated picture block (length fields run past the end).*/
  tags[4]=trunc_tag;
  /*colors set but width/height/depth zero: inconsistent fields.*/
  tags[5]=make_picture_tag(3,"image/png","",0,0,0,5,png,
   (ogg_uint32_t)png_len);
  /*One valid picture: must survive.*/
  tags[6]=good_tag;
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,3333,tags,7,3);
  of=op_open_memory(file.data,file.len,&err);
  CHECK(of!=NULL);
  if(of==NULL)return;
  /*Only the valid picture is exposed.*/
  CHECK(op_picture_count(of,0)==1);
  CHECK(op_picture(of,0,0)!=NULL
   &&op_picture(of,0,0)->format==OP_PIC_FORMAT_PNG);
  /*All of the original tags, including the malformed ones, are still
     available through the regular comment API.*/
  ot=op_tags(of,0);
  CHECK(ot!=NULL);
  if(ot!=NULL){
    CHECK(ot->comments==7);
    CHECK(opus_tags_query_count(ot,"METADATA_BLOCK_PICTURE")==6);
    CHECK(opus_tags_query(ot,"ARTIST",0)!=NULL
     &&strcmp(opus_tags_query(ot,"ARTIST",0),"Someone")==0);
  }
  op_free(of);
  free(good_tag);
  free((void *)tags[5]);
  free(trunc_tag);
  free(trunc_b64);
  buf_clear(&file);
}

static void test_oversized(void){
  unsigned char  gif[32];
  size_t         gif_len;
  char          *good_tag;
  const char    *tags[6];
  char          *huge_tag;
  unsigned char *huge_data;
  size_t         huge_len;
  buf            file;
  OggOpusFile   *of;
  int            err;
  int            ntags;
  fprintf(stderr,"test_oversized...\n");
  gif_len=make_gif(gif,10,10);
  good_tag=make_picture_tag(3,"image/gif","",0,0,0,0,gif,
   (ogg_uint32_t)gif_len);
  /*Declared dimensions above the limit (16384), unknown format so the
     declared values are not overridden by the parser.*/
  tags[0]=make_picture_tag(3,"image/x-bogus","",100000,100000,24,0,
   gif,(ogg_uint32_t)gif_len);
  /*Declared color depth above the limit (64).*/
  tags[1]=make_picture_tag(3,"image/x-bogus","",10,10,256,0,
   gif,(ogg_uint32_t)gif_len);
  /*A MIME type with non-printable characters.*/
  tags[2]=make_picture_tag(3,"image/\x01png","",0,0,0,0,
   gif,(ogg_uint32_t)gif_len);
  /*A picture whose decoded data exceeds the 16 MB per-picture limit.*/
  huge_len=(16<<20)+1;
  huge_data=(unsigned char *)malloc(huge_len);
  CHECK(huge_data!=NULL);
  if(huge_data==NULL)return;
  memset(huge_data,0x55,huge_len);
  huge_tag=make_picture_tag(3,"image/x-bogus","",0,0,0,0,
   huge_data,(ogg_uint32_t)huge_len);
  free(huge_data);
  tags[3]=huge_tag;
  /*A valid picture after all the garbage: must survive.*/
  tags[4]=good_tag;
  ntags=5;
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,4444,tags,ntags,3);
  of=op_open_memory(file.data,file.len,&err);
  CHECK(of!=NULL);
  if(of==NULL)return;
  CHECK(op_picture_count(of,0)==1);
  CHECK(op_picture(of,0,0)!=NULL
   &&op_picture(of,0,0)->format==OP_PIC_FORMAT_GIF
   &&op_picture(of,0,0)->width==10&&op_picture(of,0,0)->height==10);
  /*The rejected tags are still visible as plain comments.*/
  CHECK(op_tags(of,0)!=NULL
   &&opus_tags_query_count(op_tags(of,0),"METADATA_BLOCK_PICTURE")==5);
  op_free(of);
  buf_clear(&file);
  /*More pictures than the per-link count limit (128).*/
  {
    enum{NPICS=130};
    const char **many_tags;
    int          pi;
    many_tags=(const char **)malloc(sizeof(*many_tags)*(NPICS+1));
    for(pi=0;pi<NPICS;pi++){
      ((char **)many_tags)[pi]=make_picture_tag(3,"image/gif","",0,0,0,0,
       gif,(ogg_uint32_t)gif_len);
    }
    many_tags[NPICS]=good_tag;
    file.data=NULL;
    file.len=file.cap=0;
    build_link(&file,5555,many_tags,NPICS+1,3);
    of=op_open_memory(file.data,file.len,&err);
    CHECK(of!=NULL);
    if(of!=NULL){
      /*The count limit is enforced, and the stream still opens.*/
      CHECK(op_picture_count(of,0)==128);
      op_free(of);
    }
    for(pi=0;pi<NPICS;pi++)free((void *)many_tags[pi]);
    free(many_tags);
    buf_clear(&file);
  }
  free(good_tag);
  free(huge_tag);
  free((void *)tags[0]);
  free((void *)tags[1]);
  free((void *)tags[2]);
}

static void test_chained(void){
  unsigned char  png[64];
  unsigned char  jpg[64];
  size_t         png_len;
  size_t         jpg_len;
  const char    *tags0[2];
  const char    *tags1[3];
  buf            file;
  OggOpusFile   *of;
  opus_int16     pcm[960*2];
  int            ret;
  int            err;
  fprintf(stderr,"test_chained...\n");
  png_len=make_png(png,32,32);
  jpg_len=make_jpeg(jpg,64,48);
  tags0[0]="TITLE=First";
  tags0[1]=make_picture_tag(3,"image/png","Link 0",0,0,0,0,png,
   (ogg_uint32_t)png_len);
  tags1[0]="TITLE=Second";
  tags1[1]=make_picture_tag(3,"image/jpeg","Link 1 front",0,0,0,0,jpg,
   (ogg_uint32_t)jpg_len);
  tags1[2]=make_picture_tag(4,"image/gif","Link 1 back",0,0,0,0,
   png,(ogg_uint32_t)png_len);
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,6666,tags0,2,3);
  build_link(&file,7777,tags1,3,2);
  of=op_open_memory(file.data,file.len,&err);
  CHECK(of!=NULL);
  if(of==NULL)return;
  CHECK(op_link_count(of)==2);
  /*Each link exposes its own pictures.*/
  CHECK(op_picture_count(of,0)==1);
  CHECK(op_picture_count(of,1)==2);
  CHECK(op_picture(of,0,0)!=NULL
   &&strcmp(op_picture(of,0,0)->description,"Link 0")==0);
  CHECK(op_picture(of,1,0)!=NULL
   &&op_picture(of,1,0)->format==OP_PIC_FORMAT_JPEG);
  CHECK(op_picture(of,1,1)!=NULL
   &&op_picture(of,1,1)->type==4);
  /*A negative link index resolves to the current link.*/
  CHECK(op_picture_count(of,-1)==1);
  /*Tags still work per link.*/
  CHECK(op_tags(of,1)!=NULL
   &&strcmp(opus_tags_query(op_tags(of,1),"TITLE",0),"Second")==0);
  /*Seeking still works and updates the current link.*/
  CHECK(op_pcm_total(of,0)==3*960);
  CHECK(op_pcm_total(of,1)==2*960);
  ret=op_pcm_seek(of,3*960+100);
  CHECK(ret==0);
  CHECK(op_current_link(of)==1);
  CHECK(op_picture_count(of,-1)==2);
  /*Decoding still works after the seek.*/
  ret=op_read(of,pcm,960*2,NULL);
  CHECK(ret>0);
  /*Seek back to the first link: its pictures are still cached.*/
  ret=op_pcm_seek(of,0);
  CHECK(ret==0);
  CHECK(op_picture_count(of,-1)==1);
  ret=op_read(of,pcm,960*2,NULL);
  CHECK(ret>0);
  op_free(of);
  free((void *)tags0[1]);
  free((void *)tags1[1]);
  free((void *)tags1[2]);
  buf_clear(&file);
}

static void test_unseekable(void){
  unsigned char  png[64];
  size_t         png_len;
  char          *pic_tag;
  const char    *tags[1];
  buf            file;
  mem_stream     ms;
  OpusFileCallbacks cb;
  OggOpusFile   *of;
  int            err;
  fprintf(stderr,"test_unseekable...\n");
  png_len=make_png(png,32,32);
  pic_tag=make_picture_tag(3,"image/png","Stream",0,0,0,0,png,
   (ogg_uint32_t)png_len);
  tags[0]=pic_tag;
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,8888,tags,1,3);
  ms.data=file.data;
  ms.size=file.len;
  ms.pos=0;
  memset(&cb,0,sizeof(cb));
  cb.read=mem_read;
  /*No seek callback: an unseekable stream.*/
  of=op_open_callbacks(&ms,&cb,NULL,0,&err);
  CHECK(of!=NULL);
  if(of==NULL)return;
  CHECK(op_picture_count(of,-1)==1);
  CHECK(op_picture(of,-1,0)!=NULL
   &&strcmp(op_picture(of,-1,0)->description,"Stream")==0);
  CHECK(op_picture(of,0,0)==op_picture(of,-1,0));
  op_free(of);
  free(pic_tag);
  buf_clear(&file);
}

static void test_errors_unchanged(void){
  unsigned char garbage[1024];
  unsigned char png[64];
  size_t        png_len;
  char         *pic_tag;
  const char   *tags[1];
  buf           file;
  OggOpusFile  *of;
  int           err;
  fprintf(stderr,"test_errors_unchanged...\n");
  /*Not an Ogg stream at all: same error as before.*/
  memset(garbage,0xA5,sizeof(garbage));
  of=op_open_memory(garbage,sizeof(garbage),&err);
  CHECK(of==NULL);
  CHECK(err==OP_ENOTFORMAT);
  /*A truncated stream: same error as before.*/
  png_len=make_png(png,32,32);
  pic_tag=make_picture_tag(3,"image/png","",0,0,0,0,png,
   (ogg_uint32_t)png_len);
  tags[0]=pic_tag;
  file.data=NULL;
  file.len=file.cap=0;
  build_link(&file,9999,tags,1,3);
  of=op_open_memory(file.data,27,&err);
  CHECK(of==NULL);
  CHECK(err<0);
  op_free(of);
  free(pic_tag);
  buf_clear(&file);
}

int main(void){
  test_single_cover();
  test_multiple_pictures();
  test_bad_base64();
  test_oversized();
  test_chained();
  test_unseekable();
  test_errors_unchanged();
  fprintf(stderr,"%d checks, %d failures.\n",nchecks,nfailures);
  return nfailures==0?0:1;
}
