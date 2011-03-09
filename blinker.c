
void PUT32 ( unsigned int, unsigned int );
unsigned int GET32 ( unsigned int );

#define GPIOCBASE 0x40011000

int notmain ( void )
{

    volatile unsigned int ra;
    unsigned int rb;
    unsigned int rc;

    //Blue  LED on PORT C bit 8
    //Green LED on PORT C bit 9

    //output push-pull b0001
    ra=GET32(GPIOCBASE+0x04);
    ra&=(~(0xF<<16));
    ra|=0x1<<16;
    PUT32(GPIOCBASE+0x04,ra);

    rb=GET32(GPIOCBASE+0x0C);
    rc=rb|(3<<8);
    rb&=(~(3<<8));
    while(1)
    {
        PUT32(GPIOCBASE+0x0C,rb);
        for(ra=0;ra<1000000;ra++) continue;
        PUT32(GPIOCBASE+0x0C,rc);
        for(ra=0;ra<1000000;ra++) continue;
    }
    return(0);
}
