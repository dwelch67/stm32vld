
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *fpin;
FILE *fpout;

unsigned short data[1024];

unsigned int ra,rb;


int main ( int argc, char *argv[] )
{
    if(argc<2) return(1);
    fpin=fopen(argv[1],"rb");
    if(fpin==NULL) return(1);
    sprintf((char *)data,"%s.h",argv[1]);
    fpout=fopen((char *)data,"wt");
    if(fpout==NULL) return(1);

    rb=fread(data,1,sizeof(data),fpin);
    fclose(fpin);
    if(rb>0x400)
    {
        printf("more than a page\n");
        return(1);
    }
    if(rb&1) rb++;
    rb>>=1;

    fprintf(fpout,"\n");
    fprintf(fpout,"const unsigned short bindata[]=\n");
    fprintf(fpout,"{\n");
    for(ra=0;ra<rb;ra++)
    {
        fprintf(fpout,"0x%04X,\n",data[ra]);
    }
    fprintf(fpout,"};\n");
    fprintf(fpout,"unsigned int bindatalen=%u;\n",rb);
    fprintf(fpout,"\n");

    fclose(fpout);

    return(0);
}
