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
    # -m64


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
    Src/boot/multiboot2.c
    Src/arch/x86_64/boot.c

    Src/kernel.c

    Src/utils/printf.c
    Src/utils/utils.c

    Src/arch/x86_64/idt.c

    Src/drivers/serial/serial.c
)



