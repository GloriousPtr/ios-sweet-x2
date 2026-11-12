cd shim
CC=aarch64-linux-gnu-gcc
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -Wall -Wextra -DBOARD_X2 -DX2_KICK_DEFAULT=9"
mkdir -p out
for f in start.S shim.c console.c mmu.c fdt.c loader.c ufs.c dpu.c; do $CC $CFLAGS -c $f -o out/${f%.*}.o; done

aarch64-linux-gnu-readelf -r out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o out/dpu.o | grep -c ABS64

$CC $CFLAGS -static -T shim.ld -Wl,--build-id=none -Wl,--no-warn-rwx-segments -Wl,-N \
    out/start.o out/shim.o out/console.o out/mmu.o out/fdt.o out/loader.o out/ufs.o out/dpu.o -o out/shim.elf
aarch64-linux-gnu-objcopy -O binary out/shim.elf out/Image
size=$(aarch64-linux-gnu-nm out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image

(cd out && rm -rf rd && mkdir rd && cd rd && find . | cpio -o -H newc --quiet | gzip -9 > ../ramdisk.bin)
cat out/Image ~/x2/dtb/*.dtb > out/Image-dtb

mkbootimg --header_version 1 --kernel out/Image-dtb --ramdisk out/ramdisk.bin \
    --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 --tags_offset 0x100 --pagesize 4096 \
    --os_version 17.0.0 --os_patch_level 2035-06 --cmdline "console=null" -o out/shim-boot-x2.img
ls -l out/shim-boot-x2.img
