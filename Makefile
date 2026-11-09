CC := gcc
LD := ld
CFLAGS := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fcf-protection=none -mno-sse -mno-mmx -Wall -Wextra -Werror -O2

.PHONY: all run test clean legacy run32 test32
all: iso

legacy: build/betaos.img

build:
	mkdir -p build

build/boot.o: boot/boot.S | build
	$(CC) -m32 -fcf-protection=none -c $< -o $@

build/boot.bin: build/boot.o boot/linker.ld
	$(LD) -m elf_i386 -T boot/linker.ld --oformat binary $< -o $@
	test "$$(wc -c < $@)" -eq 512

build/entry.o: kernel/entry.S | build
	$(CC) -m32 -fcf-protection=none -c $< -o $@

build/kernel.o: kernel/kernel.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/kernel.elf: build/entry.o build/kernel.o kernel/linker.ld
	$(LD) -m elf_i386 -T kernel/linker.ld build/entry.o build/kernel.o -o $@

build/kernel.bin: build/kernel.elf
	objcopy -O binary $< $@
	test "$$(wc -c < $@)" -le 49152

build/betaos.img: build/boot.bin build/kernel.bin
	python3 tools_image.py $^ $@

run: run64

run32: legacy
	qemu-system-i386 -drive file=build/betaos.img,format=raw,if=floppy -serial stdio -no-reboot

test: test-assembler test64

test32: legacy
	python3 tests/smoke.py

clean:
	rm -rf build

# Native x86-64 desktop, UEFI optical + USB boot. No Linux or external runtime.
CFLAGS64 := -Ibuild/64 -m64 -mcmodel=large -mno-red-zone -mgeneral-regs-only -std=gnu11 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -fno-asynchronous-unwind-tables -fcf-protection=none -Wall -Wextra -Werror -O2
PROGRAMS := $(basename $(notdir $(wildcard programs/*.S)))
BEX_FILES := $(addprefix build/programs/,$(addsuffix .bex,$(PROGRAMS)))
DESKTOP := boot gui input net program storage assembler usb fat runtime tls web
OBJECTS64 := $(addprefix build/64/,$(addsuffix .o,$(DESKTOP))) build/64/cpu.o build/64/program_blobs.o
KERNEL_LIBS := build/64/libbearssl.a
OBJECTS64 += build/64/js_runtime.o build/64/font.o build/64/font_blob.o build/64/web_js.o build/64/web_image.o
KERNEL_LIBS += build/64/libquickjs.a build/64/libmath.a $(shell $(CC) -print-libgcc-file-name)
FLOAT_CFLAGS := $(filter-out -mgeneral-regs-only -Werror,$(CFLAGS64)) -fno-math-errno -fno-strict-aliasing
JS_SRC := quickjs cutils libregexp libunicode dtoa
JS_OBJ := $(addprefix build/64/qjs/,$(addsuffix .o,$(JS_SRC)))
build/64/qjs/%.o: vendor/quickjs/%.c Makefile
	mkdir -p $(@D)
	$(CC) $(FLOAT_CFLAGS) -DBETAOS -DCONFIG_VERSION='"native"' -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/libquickjs.a: $(JS_OBJ)
	ar rcs $@ $^
MATH_SRC := $(wildcard vendor/openlibm/src/*.c)
MATH_OBJ := $(patsubst vendor/openlibm/src/%.c,build/64/math/%.o,$(MATH_SRC))
build/64/math/%.o: vendor/openlibm/src/%.c Makefile
	mkdir -p $(@D)
	$(CC) $(FLOAT_CFLAGS) -w -D__BSD_VISIBLE=1 -Ivendor/openlibm/include -Ivendor/openlibm/src -Ivendor/openlibm/amd64 -Ivendor/openlibm/ld80 -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/libmath.a: $(MATH_OBJ)
	ar rcs $@ $^
build/64/js_runtime.o: desktop/js_runtime.c desktop/os.h vendor/stb/stb_sprintf.h Makefile
	$(CC) $(FLOAT_CFLAGS) -w -Ivendor/stb -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/web_js.o: desktop/web_js.c desktop/web.h desktop/os.h vendor/quickjs/quickjs.h Makefile
	$(CC) $(FLOAT_CFLAGS) -Ivendor/quickjs -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/web_image.o: desktop/web_image.c desktop/web.h desktop/os.h vendor/stb/stb_image.h Makefile
	$(CC) $(FLOAT_CFLAGS) -w -Ivendor/stb -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/web.o: desktop/web.h desktop/web_style.inc desktop/web_layout.inc
build/64/font.o: desktop/font.c desktop/os.h vendor/stb/stb_truetype.h Makefile
	$(CC) $(FLOAT_CFLAGS) -w -Ivendor/stb -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/font_blob.S: assets/fonts/Desktop.ttf assets/fonts/Interface.ttf tools/embed_font.py | build/64
	python3 tools/embed_font.py $< $@
build/64/font_blob.o: build/64/font_blob.S
	$(CC) -m64 -c $< -o $@
OVMF ?= /usr/share/OVMF/x64/OVMF.4m.fd
BEAR_FLAGS := -Ivendor/bearssl/inc -Ivendor/bearssl/src -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0 -DBR_AES_X86NI=0 -DBR_SSE2=0 -DBR_INT128=0 -DBR_UMUL128=0
BEAR_SRC := $(shell find vendor/bearssl/src -name '*.c')
BEAR_OBJ := $(patsubst vendor/bearssl/src/%.c,build/64/bear/%.o,$(BEAR_SRC))
build/64/bear/%.o: vendor/bearssl/src/%.c
	mkdir -p $(@D)
	$(CC) $(filter-out -Werror,$(CFLAGS64)) $(BEAR_FLAGS) -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
build/64/libbearssl.a: $(BEAR_OBJ)
	ar rcs $@ $^
build/64/tls_roots.h: vendor/certs/roots.pem tools/tls_roots.py | build/64
	python3 tools/tls_roots.py $< $@
build/64/tls.o: desktop/tls.c desktop/os.h build/64/tls_roots.h Makefile
	$(CC) $(CFLAGS64) $(BEAR_FLAGS) -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@
USB_IMAGE ?=
USB_FLAGS := -device qemu-xhci,id=usb
ifneq ($(strip $(USB_IMAGE)),)
USB_FLAGS += -drive file=$(USB_IMAGE),format=raw,if=none,id=usbdata,readonly=on -device usb-storage,drive=usbdata,bus=usb.0
endif

.PHONY: iso run64 test64
iso: build/BETAOS64.iso

build/64:
	mkdir -p $@

build/64/%.o: desktop/%.c desktop/os.h Makefile | build/64
	$(CC) $(CFLAGS64) -c $< -o $@
	objcopy --remove-section .comment --remove-section .note.gnu.property $@

build/64/cpu.o: desktop/cpu.S | build/64
	$(CC) -m64 -fcf-protection=none -c $< -o $@
	objcopy --remove-section .note.gnu.property $@

build/64/BOOTX64.EFI: $(OBJECTS64) $(KERNEL_LIBS)
	$(LD) -mi386pep --subsystem 10 --image-base 0x100000 -e efi_main $^ -o $@
	objcopy --remove-section .comment --remove-section .eh_frame --remove-section .note.gnu.property $@

build/64/efi.img: build/64/BOOTX64.EFI tools/make_esp.py
	python3 tools/make_esp.py $< $@

build/BETAOS64.iso: build/64/efi.img tools/hybrid_iso.py $(BEX_FILES) Makefile THIRD_PARTY.md docs/WEB.md vendor/bearssl/LICENSE.txt vendor/quickjs/LICENSE vendor/openlibm/LICENSE.md assets/fonts/LICENSE.md assets/fonts/Interface-LICENSE.txt vendor/stb/stb_truetype.h vendor/stb/stb_sprintf.h vendor/stb/stb_image.h
	mkdir -p build/64/iso/boot build/64/iso/programs build/64/iso/licenses
	cp $< build/64/iso/boot/efi.img
	cp $(BEX_FILES) build/64/iso/programs/
	cp THIRD_PARTY.md build/64/iso/THIRD_PARTY.md
	cp docs/WEB.md build/64/iso/WEB.md
	cp vendor/bearssl/LICENSE.txt build/64/iso/licenses/BearSSL.txt
	cp vendor/quickjs/LICENSE build/64/iso/licenses/QuickJS.txt
	cp vendor/openlibm/LICENSE.md build/64/iso/licenses/OpenLibm.txt
	cp assets/fonts/LICENSE.md build/64/iso/licenses/Font-OFL.txt
	cp assets/fonts/Interface-LICENSE.txt build/64/iso/licenses/Interface-OFL.txt
	python3 -c 'from pathlib import Path; parts = [Path("vendor/stb/" + name).read_text().rsplit("ALTERNATIVE A - MIT License", 1)[1] for name in ("stb_truetype.h", "stb_sprintf.h", "stb_image.h")]; Path("build/64/iso/licenses/stb.txt").write_text("\n\n".join(parts))'
	genisoimage -quiet -R -J -V BETAOS64 -eltorito-platform efi -b boot/efi.img -no-emul-boot -o $@ build/64/iso
	python3 tools/hybrid_iso.py $@ build/64/efi.img
	sha256sum $@ > $@.sha256

run64: iso disks/BETAOS.img
	qemu-system-x86_64 -machine q35 -cpu max -m 512 -bios $(OVMF) -cdrom build/BETAOS64.iso -drive file=disks/BETAOS.img,format=raw,if=none,id=system -device ide-hd,drive=system,bus=ide.0 -vga std -nic user,model=rtl8139 $(USB_FLAGS) -serial stdio -no-reboot

test64: iso
	python3 tests/desktop.py


build/programs:
	mkdir -p $@

build/programs/%.o: programs/%.S | build/programs
	as --64 $< -o $@

build/programs/%.bex: build/programs/%.o programs/linker.ld
	$(LD) -m elf_x86_64 -T programs/linker.ld --oformat binary $< -o $@

build/64/program_blobs.S build/64/program_catalog.h &: $(BEX_FILES) tools/embed_programs.py | build/64
	python3 tools/embed_programs.py build/64 $(BEX_FILES)

build/64/program.o: build/64/program_catalog.h

build/64/program_blobs.o: build/64/program_blobs.S
	as --64 $< -o $@
	objcopy --remove-section .note.gnu.property $@

.PHONY: test-assembler test-install
build/assembler-test: tests/assembler_test.c desktop/assembler.c desktop/os.h | build
	$(CC) -std=gnu11 -Wall -Wextra -Werror -Idesktop tests/assembler_test.c desktop/assembler.c -o $@

test-assembler: build/assembler-test
	./build/assembler-test

test-install: iso
	python3 tests/desktop.py --install
	python3 tests/disk_image.py
	python3 tests/desktop.py --installed

# User data survives make clean; test disks remain disposable under build/.
disks/BETAOS.img:
	mkdir -p disks
	python3 -c 'from pathlib import Path; disk = Path("disks/BETAOS.img").open("xb"); disk.truncate(128 * 1024 * 1024); disk.close()'

.PHONY: run-installed
run-installed: disks/BETAOS.img
	qemu-system-x86_64 -machine q35 -cpu max -m 512 -bios $(OVMF) -drive file=disks/BETAOS.img,format=raw,if=none,id=system -device ide-hd,drive=system,bus=ide.0 -vga std -nic user,model=rtl8139 $(USB_FLAGS) -serial stdio -no-reboot

.PHONY: test-input
build/input-test: tests/input_test.c desktop/input.c desktop/os.h | build
	$(CC) -std=gnu11 -Wall -Wextra -Werror -Idesktop tests/input_test.c desktop/input.c -o $@
test-input: build/input-test
	./build/input-test

.PHONY: test-native-sanitizers
build/program-sanitized: tests/program_test.c desktop/program.c desktop/os.h build/64/program_catalog.h build/64/program_blobs.o | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop -Ibuild/64 tests/program_test.c desktop/program.c build/64/program_blobs.o -o $@
build/assembler-fuzz: tests/assembler_fuzz.c desktop/assembler.c desktop/os.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop tests/assembler_fuzz.c desktop/assembler.c -o $@
test-native-sanitizers: build/program-sanitized build/assembler-fuzz
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/program-sanitized
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/assembler-fuzz

build/net-parser-sanitized: tests/net_parser_test.c desktop/net.c desktop/os.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop tests/net_parser_test.c -o $@
.PHONY: test-net-parser
test-net-parser: build/net-parser-sanitized
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/net-parser-sanitized

build/gui-render-sanitized: tests/gui_render_test.c desktop/gui.c desktop/os.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop tests/gui_render_test.c -o $@
.PHONY: test-render-sanitizers
test-render-sanitizers: build/gui-render-sanitized
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/gui-render-sanitized 800 600 0
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/gui-render-sanitized 1024 768 1
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/gui-render-sanitized 1920 1080 0

.PHONY: qa test-usability
qa:
	python3 tools/run_qa.py
test-usability: iso
	python3 tests/usability.py
	python3 tests/render_correctness.py
	python3 tests/input_burst.py

build/fat-sanitized: tests/fat_test.c desktop/fat.c desktop/os.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop tests/fat_test.c desktop/fat.c -o $@

.PHONY: test-fat
test-fat: iso build/fat-sanitized
	python3 tests/usb_fixture.py build/fat-test.img
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/fat-sanitized build/fat-test.img

.PHONY: print-kernel-link
print-kernel-link:
	@printf '%s\n' $(OBJECTS64) $(KERNEL_LIBS)

.PHONY: test-web-parser
build/web-parser-sanitized: tests/web_parser_test.c desktop/web.c desktop/web_style.inc desktop/web_layout.inc desktop/web.h desktop/os.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop tests/web_parser_test.c -o $@
test-web-parser: build/web-parser-sanitized
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/web-parser-sanitized

.PHONY: test-font
build/font-sanitized: tests/font_test.c desktop/font.c build/64/font_blob.S desktop/os.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop -Ivendor/stb tests/font_test.c desktop/font.c build/64/font_blob.S -lm -o $@
test-font: build/font-sanitized
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/font-sanitized

.PHONY: test-web-images
build/image-sanitized: tests/image_test.c desktop/web_image.c desktop/web.h desktop/os.h vendor/stb/stb_image.h | build
	$(CC) -std=gnu11 -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -Idesktop -Ivendor/stb tests/image_test.c desktop/web_image.c -o $@
test-web-images: build/image-sanitized
	python3 tests/image_fixture.py
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build/image-sanitized build/image-test.png build/image-test.jpg build/image-test.gif
