#include <kernel.h>
// #include "arch/x86_64/gdt.h"
#include <arch/x86_64/idt.h>
// #include "driver/screen.h"
#include <boot/multiboot2.h>
#include <drivers/serial/serial.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/log.h>

void PageFaultHandler(void) {
  LOG_INFO("Page Fault\n");
  while (1)
    ;
}

void InvalidTSSHandler(void) {
  LOG_INFO("TSS Fault\n");
  while (1)
    ;
}

void InvalidOpcodeHandler(void) {
  LOG_INFO("Invalid Opcode\n");
  while (1)
    ;
}

void DoubleFaultHandler(void) {
  LOG_INFO("Double Fault\n");
  while (1)
    ;
}

void GeneralProtectionFaultHandler(void) {
  LOG_INFO("General Protection Fault\n");
  while (1)
    ;
}

void kmain(struct boot_info_base *ptr) {
  serial_init();

  LOG_INFO("Kmain running......");

  if (ptr->magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
    LOG_ERROR("Invalid magic number: 0x%x\n", (unsigned)(ptr->magic));
    return;
  }

  if (ptr->mbi_ptr & 7) {
    LOG_ERROR("Unaligned mbi: 0x%x\n", ptr->mbi_ptr);
    return;
  }

  // if (!init_screen(ptr->mbi_ptr)) {
  //   serial_printf("Screen initilization faild!\n");
  //   return;
  // }

  // LOG_INFO("GDT Addr : 0x%p\n",((struct GDTRTypedef *)ptr->gdt_info)->base);
  // LOG_INFO("GDT Addr : 0x%p\n",((struct GDTRTypedef *)ptr->gdt_info)->limit);

  init_idt();
  LOG_INFO("After init_id\n");

  // struct multiboot_tag_mmap *mmap_ptr = NULL;
  // bool f =
  //     get_mb_tag(ptr->mbi_ptr, MULTIBOOT_TAG_TYPE_MMAP, (void **)&mmap_ptr);

  // if (f) {
  //   serial_printf("Tag Type      : 0x%x\n", mmap_ptr->type);
  //   serial_printf("size          : 0x%x\n", mmap_ptr->size);
  //   serial_printf("Entry size    : 0x%x\n", mmap_ptr->entry_size);
  //   serial_printf("Entry version : 0x%x\n", mmap_ptr->entry_version);
  //   serial_printf("Entries Addr  : 0x%x\n", mmap_ptr->entries);

  //   struct multiboot_mmap_entry *mmap_entries;
  //   serial_printf("\n");

  //   for (mmap_entries = (struct multiboot_mmap_entry *)(mmap_ptr->entries);
  //        (uint8_t *)mmap_entries <
  //        (uint8_t *)mmap_ptr->entries + mmap_ptr->size;
  //        mmap_entries =
  //            (struct multiboot_mmap_entry *)((uint8_t *)mmap_entries +
  //                                            mmap_ptr->entry_size)) {

  //     if (mmap_entries->type == 1) {
  //       serial_printf("Base Addr : 0x%lx\n", mmap_entries->addr);
  //       serial_printf("Length    : %lu\n", mmap_entries->len);
  //       serial_printf("Type      : 0x%x\n", mmap_entries->type);
  //       serial_printf("\n");
  //     }
  //   }

  //   serial_printf("For exit\n");
  // } else {
  //   serial_printf("Memory Map Not Found\n");
  // }

  // clr_screen(0x00000000);

  // printf("If you want, I can show:\nHow to calculate the true framebuffer
  // size "
  //        "with padding\nHow to draw pixels safely with pitch\nHow GRUB stores
  //        " "pitch in multiboot tags\nJust ask!\n");
  // printf("My Lenovo ThinkPad has a default resolution of 1366x768, and when I
  // "
  //        "enter full-screen mode the displace scaled from 1280x720 and
  //        nothing " "was shown clear. So, I followed the instructions and
  //        changed the " "resolution to 1366x768 and the display was completely
  //        messed up. It " "simply showed diagonal spikes of pixels, I couldn't
  //        figure out " "anything my screen was showing.\n");
  // printf("Integer : %d\n", 335454);

  while (1)
    ;
}
