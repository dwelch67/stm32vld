
void PUT32 ( unsigned int, unsigned int );
unsigned int GET32 ( unsigned int );

#define RCCBASE   0x40021000
#define GPIOCBASE 0x40011000

int notmain ( void )
{
    volatile unsigned int ra;
    unsigned int rb;
    unsigned int rc;

    ra=GET32(RCCBASE+0x18);
    ra|=1<<4; //enable port C
    PUT32(RCCBASE+0x18,ra);

    //Blue  LED on PORT C bit 8
    //Green LED on PORT C bit 9

    //output push-pull b0001
    ra=GET32(GPIOCBASE+0x04);
    ra&=(~(0xFF<<0));
    ra|=0x11<<0;
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
