#include <boot/boot.h>
#include <boot/limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__attribute__((used,
               section(".limine_requests_start"))) static volatile uint64_t
    limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"))) static volatile uint64_t
    limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((
    used,
    section(
        ".limine_requests"))) static volatile struct limine_framebuffer_request
    framebuffer_request = {.id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

__attribute__((
    used,
    section(".limine_requests"))) static volatile struct limine_memmap_request
    memmap_request = {.id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

__attribute__((used, section(".limine_requests_end"))) static volatile uint64_t
    limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

bool isBootOk(void) {
  return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

void getFramebufferAddr(struct FrameBuffer_s *framebuffer) {
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

size_t getMMapEntryCount(void) {
  if (memmap_request.response == NULL)
    return 0;
  return memmap_request.response->entry_count;
}

bool copyMMapEntry(struct MemoryMapEntry_s *dest) {
  if (memmap_request.response == NULL ||
      memmap_request.response->entry_count < 1) {
    return false;
  }

  for (size_t i = 0; i < memmap_request.response->entry_count; i++) {
    struct limine_memmap_entry *entry = memmap_request.response->entries[i];

    dest[i].base = entry->base;
    dest[i].length = entry->length;
    dest[i].type = entry->type;
  }

  return true;
}
