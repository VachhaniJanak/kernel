



# x86_64 Hobby Kernel

A hobby operating system kernel developed from scratch for x86_64 architecture, using C and Assembly.

## Features

- x86_64 architecture and Limine boot protocol
- Physical and virtual memory management
- Paging and virtual memory areas (VMA)
- ELF executable loading
- System call interface
- Process and thread management
- Preemptive multitasking and scheduling
- Copy-on-Write (COW) fork implementation
- Interrupt and exception handling
- ACPI and PCI support
- PS/2 keyboard driver

## Technologies

- **Languages:** C, x86_64 Assembly
- **Bootloader:** Limine
- **Emulator:** QEMU
- **Debugger:** GDB
- **Toolchain:** x86_64-elf GCC

## Project Demonstration

**Kernel Development & Demonstration Video**

[Watch Project Video](https://github.com/user-attachments/assets/1710563f-9fd8-4e43-bcc4-063c10a61271)

## Build and Run

```bash
git clone https://github.com/VachhaniJanak/kernel.git
cd kernel
mkdir build
cd build && cmake ..
cd ..
cmake --build build
```

Run the kernel using QEMU:

```bash
make loadkernel run
```


## Project Status

Ongoing development. New kernel subsystems and operating system features are being implemented incrementally.

