

all : stlink2_ram stlink2_flash

stlink2_ram : stlink2.c stlink-common.h stlink-common.c stlink-usb.h stlink-usb.c uglylogging.h uglylogging.c
	gcc -Wall -Wextra -Werror -std=gnu99 -DLOAD_RAM stlink2.c stlink-common.c stlink-usb.c uglylogging.c -lusb-1.0 -o stlink2_ram

stlink2_flash : stlink2.c stlink-common.h stlink-common.c stlink-usb.h stlink-usb.c uglylogging.h uglylogging.c
	gcc -Wall -Wextra -Werror -std=gnu99 -DLOAD_FLASH stlink2.c stlink-common.c stlink-usb.c uglylogging.c -lusb-1.0 -o stlink2_flash

clean :
	rm -f stlink2_ram
	rm -f stlink2_flash


