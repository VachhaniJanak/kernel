#include <boot/boot.h>
#include <boot/limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/utils.h>

#define LIMINE_REQUESTS_START \
  __attribute__((used, section(".limine_requests_start")))

#define LIMINE_REQUESTS __attribute__((used, section(".limine_requests")))

#define LIMINE_REQUESTS_END \
  __attribute__((used, section(".limine_requests_end")))

LIMINE_REQUESTS_START static volatile uint64_t limine_requests_start_marker[] =
    LIMINE_REQUESTS_START_MARKER;

LIMINE_REQUESTS static volatile uint64_t limine_base_revision[] =
    LIMINE_BASE_REVISION(6);

LIMINE_REQUESTS static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

LIMINE_REQUESTS static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

LIMINE_REQUESTS static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0};

LIMINE_REQUESTS static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0};

LIMINE_REQUESTS static volatile struct limine_executable_file_request
    exec_file_request = {.id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
                         .revision = 0};

LIMINE_REQUESTS_END static volatile uint64_t limine_requests_end_marker[] =
    LIMINE_REQUESTS_END_MARKER;

bool isBootOk(void) {
  return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

void getFramebufferAddr(struct FrameBuffer_s* framebuffer) {
  if (framebuffer_request.response == NULL ||
      framebuffer_request.response->framebuffer_count < 1) {
    framebuffer->address = NULL;
    return;
  }

  framebuffer->address = framebuffer_request.response->framebuffers[0]->address;
  framebuffer->width = framebuffer_request.response->framebuffers[0]->width;
  framebuffer->height = framebuffer_request.response->framebuffers[0]->height;
  framebuffer->pitch = framebuffer_request.response->framebuffers[0]->pitch;
  framebuffer->bpp = framebuffer_request.response->framebuffers[0]->bpp;

  framebuffer->red_mask_size =
      framebuffer_request.response->framebuffers[0]->red_mask_size;
  framebuffer->blue_mask_size =
      framebuffer_request.response->framebuffers[0]->blue_mask_size;
  framebuffer->green_mask_size =
      framebuffer_request.response->framebuffers[0]->green_mask_size;

  framebuffer->red_mask_shift =
      framebuffer_request.response->framebuffers[0]->red_mask_shift;
  framebuffer->blue_mask_shift =
      framebuffer_request.response->framebuffers[0]->blue_mask_shift;
  framebuffer->green_mask_shift =
      framebuffer_request.response->framebuffers[0]->green_mask_shift;
}

uintptr_t getHHDMOffset(void) {
  if (hhdm_request.response == NULL) return 0;

  return hhdm_request.response->offset;
}

void* getRSDT(void) {
  if (rsdp_request.response == NULL) return NULL;
  return rsdp_request.response->address;
}

bool getBootVolumeInfo(struct boot_volume_info* volume) {
  if (exec_file_request.response == NULL) {
    return false;
  }

  struct limine_executable_file_response* exec_file_response =
      exec_file_request.response;

  if (exec_file_response->executable_file->mbr_disk_id != 0) {
    volume->type = BOOT_VOLUME_TYPE_MBR;
    return false;
  }

  volume->type = BOOT_VOLUME_TYPE_GPT;
  volume->partition_index =
      exec_file_response->executable_file->partition_index;

  kmemcpy(&volume->gpt_disk_uuid,
          &exec_file_response->executable_file->gpt_disk_uuid,
          sizeof(struct boot_uuid));

  kmemcpy(&volume->gpt_partition_uuid,
          &exec_file_response->executable_file->gpt_part_uuid,
          sizeof(struct boot_uuid));
  return true;
}

int boot_iterate_mmap_entries(size_t* saved_index, void* context,
                              int (*callback)(void* context,
                                              struct MemoryMapEntry_s* entry)) {
  if (!callback || !memmap_request.response ||
      memmap_request.response->entry_count < 1) {
    return -1;
  }

  size_t start_index = 0;

  if (saved_index) {
    start_index = *saved_index;
  }

  struct MemoryMapEntry_s temp = {0};

  for (size_t i = start_index; i < memmap_request.response->entry_count; i++) {
    struct limine_memmap_entry* entry = memmap_request.response->entries[i];

    temp.base = entry->base;
    temp.length = entry->length;
    temp.type = entry->type;

    if (callback(context, &temp) > 0) {
      if (saved_index) {
        *saved_index = i + 1;
      }
      return 1;
    }
  }

  if (saved_index) {
    *saved_index = memmap_request.response->entry_count;
  }
  return 0;
}

int boot_get_mmap_entry(size_t index, struct MemoryMapEntry_s* entry) {
  if (memmap_request.response == NULL ||
      index >= memmap_request.response->entry_count) {
    return -1;
  }

  struct limine_memmap_entry* target = memmap_request.response->entries[index];

  entry->base = target->base;
  entry->length = target->length;
  entry->type = target->type;

  return 0;
}

char* boot_get_memory_type_string(size_t type) {
  switch (type) {
    case MEMMAP_USABLE:
      return "Usable";
    case MEMMAP_RESERVED:
      return "Reserved";
    case MEMMAP_ACPI_RECLAIMABLE:
      return "ACPI Reclaimable";
    case MEMMAP_ACPI_NVS:
      return "ACPI NVS";
    case MEMMAP_BAD_MEMORY:
      return "Bad Memory";
    case MEMMAP_BOOTLOADER_RECLAIMABLE:
      return "Bootloader Reclaimable";
    case MEMMAP_EXECUTABLE_AND_MODULES:
      return "Executable and Modules";
    case MEMMAP_FRAMEBUFFER:
      return "Framebuffer";
    case MEMMAP_RESERVED_MAPPED:
      return "Reserved Mapped";
    default:
      return "Unknown";
  }
}
