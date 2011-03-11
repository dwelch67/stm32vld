
ARMGNU = arm-none-linux-gnueabi

ARCH = -mcpu=cortex-m3
#ARCH = 
AOPS = --warn --fatal-warnings -mthumb $(ARCH)
COPS = -Wall -Werror -O2 -nostdlib -nostartfiles -ffreestanding -mthumb $(ARCH)


all : blinker.bin doflash.bin stlink-ramload

clean :
	rm -f *.o
	rm -f *.elf
	rm -f *.list
	rm -f *.bin
	rm -f *.bin.h
	rm -f bintoh
	rm -f stlink-ramload

vectors.o : vectors.s
	$(ARMGNU)-as $(AOPS) vectors.s -o vectors.o


novectors.o : novectors.s
	$(ARMGNU)-as $(AOPS) novectors.s -o novectors.o


blinker.elf : blinker.o novectors.o memmap
	$(ARMGNU)-ld -T memmap novectors.o blinker.o -o blinker.elf
	$(ARMGNU)-objdump -D blinker.elf > blinker.list

blinker.o : blinker.c
	$(ARMGNU)-gcc $(COPS) -c blinker.c -o blinker.o

blinker.bin : blinker.elf
	$(ARMGNU)-objcopy blinker.elf -O binary blinker.bin



flashblink.elf : flashblink.o vectors.o flashmap
	$(ARMGNU)-ld -T flashmap vectors.o flashblink.o -o flashblink.elf
	$(ARMGNU)-objdump -D flashblink.elf > flashblink.list

flashblink.o : blinker.c
	$(ARMGNU)-gcc $(COPS) -c blinker.c -o flashblink.o

flashblink.bin : flashblink.elf
	$(ARMGNU)-objcopy flashblink.elf -O binary flashblink.bin

flashblink.bin.h : bintoh.c flashblink.bin
	gcc bintoh.c -o bintoh
	./bintoh flashblink.bin



doflash.elf : doflash.o novectors.o memmap
	$(ARMGNU)-ld -T memmap novectors.o doflash.o -o doflash.elf
	$(ARMGNU)-objdump -D doflash.elf > doflash.list

doflash.o : doflash.c flashblink.bin.h
	$(ARMGNU)-gcc $(COPS) -c doflash.c -o doflash.o

doflash.bin : doflash.elf
	$(ARMGNU)-objcopy doflash.elf -O binary doflash.bin

stlink-ramload : stlink-ramload.c
	gcc stlink-ramload.c -lsgutils2 -o stlink-ramload -fmessage-length=0 -std=gnu99


