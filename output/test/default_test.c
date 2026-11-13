#include <stdio.h>
#include <opusfile.h>
int main(int c,char**v){
  int e=0;OggOpusFile*of=op_open_file(v[1],&e);
  if(!of){return 1;}
  FILE*fp=fopen(v[2],"wb");
  for(;;){float p[120*48*2];int n=op_read_float(of,p,sizeof(p)/sizeof(*p),NULL);
    if(n<0)return 1;if(n==0)break;
    if(fwrite(p,sizeof(float),(size_t)n*op_channel_count(of,-1),fp)!=(size_t)n*op_channel_count(of,-1))return 1;}
  fclose(fp);op_free(of);return 0;}
