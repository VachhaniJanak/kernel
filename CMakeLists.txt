cmake_minimum_required(VERSION 3.20)

# Setup compiler settings
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Set the project name
set(CMAKE_PROJECT_NAME kernel)

project(${CMAKE_PROJECT_NAME} C ASM)

include("cmake/x86_64_elf_gcc.cmake")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Directories
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/linker.ld)
set(BUILD_DIR ${CMAKE_BINARY_DIR})

# Kernel ELF
add_executable(${CMAKE_PROJECT_NAME}.elf)

# Compile Options
target_compile_options(${CMAKE_PROJECT_NAME}.elf PRIVATE

    # Freestanding
    -ffreestanding
    -fno-pic
    -fno-stack-protector
    -fno-stack-check
    -mno-red-zone
    -nostdlib
    -fno-builtin
    -mgeneral-regs-only
    -mcmodel=kernel
    -m64


    # Warnings
    -Wall
    -Wextra
    -Winline
    # -Wpedantic
    

    # Dependency generation
    -MMD

    # Assembler
    # -Wa,--divide

    # Optimization
    -O0 -g3
    -DDEBUG
    # -DAHCI_DEBUG
    # -DPS2_DEBUG
    # -DSCHEDULER_DEBUG
    # -DPAGE_FAULT_DEBUG
    # -O3
)


# Link Options
target_link_options(${CMAKE_PROJECT_NAME}.elf PRIVATE
    -nostdlib
    -static
    -T ${LINKER_SCRIPT}
    -Wl,-Map=${BUILD_DIR}/kernel.map
)


# Definations 
target_compile_definitions(${CMAKE_PROJECT_NAME}.elf PRIVATE

)

# Includes
target_include_directories(${CMAKE_PROJECT_NAME}.elf PRIVATE
    Inc/
)

# Sources
target_sources(${CMAKE_PROJECT_NAME}.elf PRIVATE
    Src/boot/boot.c

    Src/kernel.c

    Src/utils/printf.c
    Src/utils/utils.c
    Src/utils/panic.c

    Src/arch/x86_64/idt.c
    Src/arch/x86_64/gdt.c
    Src/arch/x86_64/tss.c
    Src/arch/x86_64/isr.c
    Src/arch/x86_64/isr.S
    Src/arch/x86_64/helper.S
    Src/arch/x86_64/apic.c
    Src/arch/x86_64/timer.c
    Src/arch/x86_64/hpet.c
    Src/arch/x86_64/syscall.c
    Src/arch/x86_64/syscall.S

    Src/drivers/serial/serial.c
    Src/drivers/screen/screen.c
    Src/drivers/acpi/acpi.c
    Src/drivers/pcie/pcie.c
    Src/drivers/ahci/ahci.c
    Src/drivers/keyboard/ps2/keyboard.c
    Src/drivers/keyboard/ps2/scancode_set2.c

    Src/mm/mm.c
    Src/mm/pmm/buddy.c
    Src/mm/pmm/pmm.c
    Src/mm/kheap/slub.c
    Src/mm/kheap/kheap.c
    Src/mm/vmm/vmm.c

    Src/consolefont/fonts/terminus28b.o
    Src/consolefont/font.c

    Src/process/scheduler.c
    Src/process/locks.c
    Src/process/elf.c
    Src/process/process.c
    Src/process/thread.c
    Src/process/cleanup.c
    Src/process/waitqueue.c

    Src/syscall/syscall.c

    Src/input/keyboard/keyboard.c
    Src/input/keyboard/layout_us_qwerty.c
    Src/input/input.c

    Src/tty/tty.c
    Src/tty/tty_input.c
    Src/tty/tty_output.c

    Src/fs/fs.c
    Src/fs/utils.c
    Src/fs/tmpfs/tmpfs.c
    Src/fs/devfs/devfs.c
    Src/fs/fat/fat32.c
    Src/fs/prt/gpt.c

    Src/libs/string.c

    Src/device/device.c
    Src/device/block.c
    Src/device/char.c
)




