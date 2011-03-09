
ARMGNU = arm-none-linux-gnueabi

ARCH = -mcpu=cortex-m3
#ARCH = 
AOPS = --warn --fatal-warnings -mthumb $(ARCH)
COPS = -Wall -Werror -O2 -nostdlib -nostartfiles -ffreestanding -mthumb $(ARCH)


all : blinker.bin

clean :
	rm -f *.o
	rm -f *.elf
	rm -f *.list
	rm -f *.bin

blinker.elf : blinker.o vectors.o memmap
	$(ARMGNU)-ld -T memmap vectors.o blinker.o -o blinker.elf
	$(ARMGNU)-objdump -D blinker.elf > blinker.list

blinker.o : blinker.c
	$(ARMGNU)-gcc $(COPS) -c blinker.c -o blinker.o

vectors.o : vectors.s
	$(ARMGNU)-as $(AOPS) vectors.s -o vectors.o


blinker.bin : blinker.elf
	$(ARMGNU)-objcopy blinker.elf -O binary blinker.bin


