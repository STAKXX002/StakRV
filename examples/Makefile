CC      = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS  = -march=rv32i -mabi=ilp32 -nostdlib -Ttext=0x80000000

EXAMPLES = sum fib memcpy bubble_sort strlen strcpy factorial popcount bitrev fizzbuzz
BINS     = $(EXAMPLES:%=%.bin)

.PHONY: all clean

all: $(BINS)

%.elf: %.s
	$(CC) $(CFLAGS) -o $@ $<

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.elf *.bin