
#include "flashblink.bin.h"

void PUT16 ( unsigned int, unsigned int );
void PUT32 ( unsigned int, unsigned int );
unsigned int GET32 ( unsigned int );

#define RCCBASE   0x40021000
#define GPIOCBASE 0x40011000

#define FLASH_BASE 0x40022000
#define FLASH_KEYR (FLASH_BASE+0x04)
#define FLASH_SR   (FLASH_BASE+0x0C)
#define FLASH_CR   (FLASH_BASE+0x10)

void failed ( int x )
{
    unsigned int rb,rc;

    rb=GET32(GPIOCBASE+0x0C);
    rc=rb&(~(3<<8));
    rc=rb|(x<<8);
    PUT32(GPIOCBASE+0x0C,rc);
    while(1) continue;
}
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
    rb&=(~(3<<8));
    PUT32(GPIOCBASE+0x0C,rb);






    //Check to see if flash is locked
    rb=GET32(FLASH_CR);
    if(rb&0x80)
    {
        //unlock flash
        PUT32(FLASH_KEYR,0x45670123);
        PUT32(FLASH_KEYR,0xCDEF89AB);
        //see if it worked
        rb=GET32(FLASH_CR);
        if(rb&0x80)
        {
            failed(1);
        }
    }

    //if it is busy then just give up
    rb=GET32(FLASH_SR);
    if(rb&0x1)
    {
        failed(2);
    }


    //mass erase
    PUT32(FLASH_SR,0x0034);
    PUT32(FLASH_CR,0x0004);
    PUT32(FLASH_CR,0x0044);
    while(1)
    {
        rb=GET32(FLASH_SR);
        if((rb&0x21)==0x20) break;
    }
    PUT32(FLASH_SR,0x0034);
    PUT32(FLASH_CR,0x0000);

    //program
    PUT32(FLASH_SR,0x0034);
    PUT32(FLASH_CR,0x0001);

    //probably needs to be less than a page
    for(ra=0;ra<bindatalen;ra++) PUT16(0x08000000+(ra<<1),bindata[ra]);
    while(1)
    {
        rb=GET32(FLASH_SR);
        if((rb&0x21)==0x20) break;
    }
    PUT32(FLASH_SR,0x0034);
    PUT32(FLASH_CR,0x0000);





    rb=GET32(GPIOCBASE+0x0C);
    rc=rb|(2<<8);
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
