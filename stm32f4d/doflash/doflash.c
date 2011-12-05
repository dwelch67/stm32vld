
//-------------------------------------------------------------------
//-------------------------------------------------------------------
//http://gitorious.org/~tormod/unofficial-clones/dfuse-dfu-util
//dfu-util -d 0483:df11 -c 1 -i 0 -a 0 -s 0x08000000 -D flashblinker.bin
//-------------------------------------------------------------------
void PUT32 ( unsigned int, unsigned int );
void PUT16 ( unsigned int, unsigned int );
void PUT8 ( unsigned int, unsigned int );
void PUT64X ( unsigned int, unsigned int, unsigned int );
void PUT64M ( unsigned int, unsigned int, unsigned int );
unsigned int GET32 ( unsigned int );
unsigned int GET16 ( unsigned int );
//-------------------------------------------------------------------
#define RCCBASE   0x40023800
#define RCC_CR    (RCCBASE+0x00)
#define RCC_PLLCFGR (RCCBASE+0x04)
#define RCC_CFGR  (RCCBASE+0x08)
#define RCC_AHB1ENR (RCCBASE+0x30)
#define RCC_APB1ENR (RCCBASE+0x40)
#define GPIODBASE 0x40020C00
#define TIM5BASE  0x40000C00
#define GPIOABASE 0x40020000
#define GPIOA_MODER (GPIOABASE+0x00)
#define USART2_BASE 0x40004400
#define USART2_SR  (USART2_BASE+0x00)
#define USART2_DR  (USART2_BASE+0x04)
#define USART2_BRR (USART2_BASE+0x08)
#define USART2_CR1 (USART2_BASE+0x0C)
#define USART2_CR2 (USART2_BASE+0x10)
#define USART2_CR3 (USART2_BASE+0x14)
#define USART2_GTPR (USART2_BASE+0x18)
#define GPIOA_AFRL (GPIOABASE+0x20)
#define GPIOA_OTYPER (GPIOABASE+0x04)

#define FLASH_BASE  0x40023C00
#define FLASH_ACR   (FLASH_BASE+0x00)
#define FLASH_KEYR  (FLASH_BASE+0x04)
#define FLASH_OPTKEYR  (FLASH_BASE+0x08)
#define FLASH_SR    (FLASH_BASE+0x0C)
#define FLASH_CR    (FLASH_BASE+0x10)
#define FLASH_OPTCR (FLASH_BASE+0x14)

//-------------------------------------------------------------------
//PA2 USART2_TX available
//PA3 USART2_RX available

//PD8 USART3_TX available
//PD9 USART3_rX available
//-------------------------------------------------------------------
void clock_init ( void )
{
    unsigned int ra;

    //enable HSE
    ra=GET32(RCC_CR);
    ra&=~(0xF<<16);
    PUT32(RCC_CR,ra);
    ra|=1<<16;
    PUT32(RCC_CR,ra);
    while(1)
    {
        if(GET32(RCC_CR)&(1<<17)) break;
    }
    PUT32(RCC_CFGR,0x00000001);
}
//-------------------------------------------------------------------
int uart_init ( void )
{
    unsigned int ra;

    ra=GET32(RCC_AHB1ENR);
    ra|=1<<0; //enable port A
    PUT32(RCC_AHB1ENR,ra);

    ra=GET32(RCC_APB1ENR);
    ra|=1<<17;  //enable USART2
    PUT32(RCC_APB1ENR,ra);

    //PA2 USART2_TX
    //PA3 USART2_RX

    ra=GET32(GPIOA_MODER);
    ra|= (2<<4);
    ra|= (2<<6);
    PUT32(GPIOA_MODER,ra);
    ra=GET32(GPIOA_OTYPER);
    ra&=(1<<2);
    ra&=(1<<3);
    PUT32(GPIOA_OTYPER,ra);
    ra=GET32(GPIOA_AFRL);
    ra|=(7<<8);
    ra|=(7<<12);
    PUT32(GPIOA_AFRL,ra);

    //8000000/16 = 500000
    //500000/9600 = 52.08333
    PUT32(USART2_BRR,(52<<4)|(1<<0));
    PUT32(USART2_CR1,(1<<13)|(1<<3)|(1<<2));
    return(0);
}
//-------------------------------------------------------------------
void uart_putc ( unsigned int x )
{
    while (( GET32(USART2_SR) & (1<<7)) == 0) continue;
    PUT32(USART2_DR,x);
}
//-------------------------------------------------------------------
unsigned int uart_getc ( void )
{
    while (( GET32(USART2_SR) & (1<<5)) == 0) continue;
    return(GET32(USART2_DR));
}
//-------------------------------------------------------------------
void hexstring ( unsigned int d, unsigned int cr )
{
    //unsigned int ra;
    unsigned int rb;
    unsigned int rc;

    rb=32;
    while(1)
    {
        rb-=4;
        rc=(d>>rb)&0xF;
        if(rc>9) rc+=0x37; else rc+=0x30;
        uart_putc(rc);
        if(rb==0) break;
    }
    if(cr)
    {
        uart_putc(0x0D);
        uart_putc(0x0A);
    }
    else
    {
        uart_putc(0x20);
    }
}
//-------------------------------------------------------------------
void uart_string ( const char *s )
{
    for(;*s;s++)
    {
        if(*s==0x0A) uart_putc(0x0D);
        uart_putc(*s);
    }
}
//-------------------------------------------------------------------
void timdelay ( void )
{
    unsigned int ra;
    unsigned int rb;

    rb=GET32(TIM5BASE+0x24);
    while(1)
    {
        ra=GET32(TIM5BASE+0x24);
        if((ra-rb)>=((168000000*2)/8)) break;
    }
}
//-------------------------------------------------------------------
int notmain ( void )
{
    unsigned int ra;
    volatile unsigned int beg,end;

    clock_init();
    uart_init();

    ra=GET32(RCC_APB1ENR);
    ra|=1<<3; //enable TIM5
    PUT32(RCC_APB1ENR,ra);
    PUT32(TIM5BASE+0x00,0x00000000);
    PUT32(TIM5BASE+0x2C,0xFFFFFFFF);
    PUT32(TIM5BASE+0x00,0x00000001);


    uart_string("\nHello World!\n");
    for(ra=0x00;ra<0x20;ra+=4) hexstring(GET32(0x08000000+ra),1);

    //wait for busy bit?

    hexstring(GET32(FLASH_SR),1);
    ra=GET32(FLASH_CR);
    hexstring(ra,1);
    if(ra&0x80000000)
    {
        PUT32(FLASH_KEYR,0x45670123);
        PUT32(FLASH_KEYR,0xCDEF89AB);
    }
    hexstring(GET32(FLASH_CR),1);

    PUT32(FLASH_CR,0x00000002);
    PUT32(FLASH_CR,0x00010002);

    hexstring(GET32(FLASH_SR),1);

    while(1)
    {
        if((GET32(FLASH_SR)&0x00010000)==0) break;
    }

    hexstring(GET32(FLASH_SR),1);

    for(ra=0x00;ra<0x20;ra+=4) hexstring(GET32(0x08000000+ra),1);


    beg=GET32(TIM5BASE+0x24);

    PUT32(FLASH_CR,0x00000200); //why doesnt 64 bit mode work?
    PUT32(FLASH_CR,0x00000201);
    for(ra=0x0000;ra<0x4000;ra+=4)
    {
        PUT32(0x08000000+ra,ra);
if(0) //inside
{
        while(1)
        {
            if((GET32(FLASH_SR)&0x00010000)==0) break;
        }
}
    }
if(1) //or outside
{
    while(1)
    {
        if((GET32(FLASH_SR)&0x00010000)==0) break;
    }
}
    hexstring(GET32(FLASH_SR),1);

    end=GET32(TIM5BASE+0x24);
    hexstring(beg,1);
    hexstring(end,1);
    hexstring(end-beg,1);

    for(ra=0x00;ra<0x20;ra+=4) hexstring(GET32(0x08000000+ra),1);

    return(0);
}
//-------------------------------------------------------------------
//-------------------------------------------------------------------
