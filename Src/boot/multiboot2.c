#include <boot/multiboot2.h>
#include <stdint.h>

#define MB_TAG_ALIGN __attribute__((aligned(8)))
#define MB_SECTION __attribute__((section(".multiboot")))

struct mb2_header {
  struct multiboot_header header;
  struct multiboot_header_tag_framebuffer MB_TAG_ALIGN frame;
  struct multiboot_header_tag MB_TAG_ALIGN efi_services;
  struct multiboot_header_tag MB_TAG_ALIGN end_tag;
};

struct mb2_header mb2_header_data MB_SECTION = {
    .header = {.magic = MULTIBOOT2_HEADER_MAGIC,
               .architecture = 0,
               .header_length = sizeof(struct mb2_header),
               .checksum = (uint32_t)(-((uint32_t)MULTIBOOT2_HEADER_MAGIC +
                                        (uint32_t)MULTIBOOT_ARCHITECTURE_I386 +
                                        (uint32_t)sizeof(struct mb2_header)))},

    .frame = {.type = MULTIBOOT_HEADER_TAG_FRAMEBUFFER,
              .flags = 0,
              .size = sizeof(struct multiboot_header_tag_framebuffer),
              .width = 1280,
              .height = 800,
              .depth = 32},

    .efi_services = {.type = MULTIBOOT_HEADER_TAG_EFI_BS,
                     .flags = 0,
                     .size = sizeof(struct multiboot_header_tag)},

    .end_tag = {.type = MULTIBOOT_HEADER_TAG_END, .flags = 0, .size = 8}};
