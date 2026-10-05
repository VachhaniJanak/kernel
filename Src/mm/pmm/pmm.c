#include <boot/boot.h>
#include <mm/pmm/buddy.h>
#include <mm/pmm/pmm.h>
#include <mm/utils.h>
#include <stdbool.h>
#include <stdint.h>
#include <utils/log.h>

// #define PMM_DEBUG

#ifdef PMM_DEBUG
#define pmm_log_print(fmt, ...) (log_print(fmt, ##__VA_ARGS__))
#define pmm_log_error(fmt, ...) (log_error(fmt, ##__VA_ARGS__))
#define pmm_log_debug(fmt, ...) (log_debug(fmt, ##__VA_ARGS__))
#else
#define pmm_log_print(fmt, ...) ((void)0)
#define pmm_log_error(fmt, ...) ((void)0)
#define pmm_log_debug(fmt, ...) ((void)0)
#endif

struct enough_space_context_s {
  size_t required_size;
  size_t minimum_size;
  size_t base;
};

struct feed_region_context_s {
  buddy_state_t* buddy_state;
  size_t metadata_base;
  size_t metadata_length;
};

static int get_highest_address(void* context, struct MemoryMapEntry_s* entry) {
  uintptr_t* highest_addr = (uintptr_t*)context;
  uintptr_t entry_end = entry->base + entry->length;

  if (entry_end > *highest_addr) {
    *highest_addr = entry_end;
  }

  return 0;  // Continue iteration
}

static int find_enough_space(void* context, struct MemoryMapEntry_s* entry) {
  struct enough_space_context_s* ctx = (struct enough_space_context_s*)context;

  if (entry->base == 0 || entry->length == 0) {
    return 0;  // Skip invalid entries
  }

  if (entry->type == MEMMAP_USABLE && entry->length >= ctx->required_size) {
    // check if founded entry is smaller than previous found entry
    if (ctx->minimum_size == 0 || entry->length < ctx->minimum_size) {
      ctx->minimum_size = entry->length;
      ctx->base = entry->base;
    }
  }

  return 0;  // Continue iteration
}

static int find_memory_size(void* context, struct MemoryMapEntry_s* entry) {
  size_t* total_size = (size_t*)context;

  // count only which consume actual physical memory
  if (entry->type == MEMMAP_USABLE || entry->type == MEMMAP_ACPI_RECLAIMABLE ||
      entry->type == MEMMAP_BOOTLOADER_RECLAIMABLE ||
      entry->type == MEMMAP_ACPI_NVS ||
      entry->type == MEMMAP_EXECUTABLE_AND_MODULES ||
      entry->type == MEMMAP_FRAMEBUFFER) {
    *total_size += entry->length;
  }

  return 0;  // Continue iteration
}

static int feed_region(void* context, struct MemoryMapEntry_s* entry) {
  struct feed_region_context_s* ctx = (struct feed_region_context_s*)context;
  const size_t page_size = ctx->buddy_state->page_size;

  if (entry->type != MEMMAP_USABLE || entry->length < page_size) {
    return 0;  // Skip non-usable entries
  }

  if (entry->base == 0) {
    // Skip the first page (0x00000000 - 0x00000FFF) to avoid null pointer
    // dereference issues
    size_t skip_size = page_size;
    entry->base += skip_size;
    entry->length -= skip_size;
  }

  size_t start_phys = page_align_up(entry->base, page_size);
  size_t end_phys = page_align_down(entry->base + entry->length, page_size);

  // if it overlaps with the metadata region
  if (entry->base == ctx->metadata_base) {
    size_t new_base = entry->base + ctx->metadata_length;
    size_t new_length = entry->length - ctx->metadata_length;

    start_phys = page_align_up(new_base, page_size);
    end_phys = page_align_down(new_base + new_length, page_size);
  }

  while (start_phys < end_phys) {
    buddy_free(ctx->buddy_state, (void*)start_phys);
    start_phys += page_size;

    // Update buddy_state_t metadata
    ctx->buddy_state->total_usable_frames++;
  }

  return 0;  // Continue iteration
}

#ifdef PMM_DEBUG
static int print_mmap(void* context, struct MemoryMapEntry_s* entry) {
  log_print("Base=0x%016lx, Length=0x%016lx, Type=%s\n", entry->base,
            entry->length, boot_get_memory_type_string(entry->type));
  return 0;  // Continue iteration
}
#endif

int pmm_init(struct pmm_state_s* self, size_t page_size, size_t hhdm_offset) {
#ifdef PMM_DEBUG
  log_print("Memory Map Entries:\n");
  boot_iterate_mmap_entries(NULL, NULL, print_mmap);
#endif

  pmm_log_print("Initializing Physical Memory Manager (PMM)...\n");

  self->page_size = page_size;
  self->hhdm_offset = hhdm_offset;

  int result = 0;
  size_t memory_size = 0;

  result = boot_iterate_mmap_entries(NULL, &memory_size, find_memory_size);

  if (result < 0) {
    pmm_log_error(
        "Failed to iterate memory map entries to find total memory size.\n");
    return -1;
  }

  pmm_log_print("Total memory size: %zu bytes\n", memory_size);

  // @@@ find the highest address in the memory map @@@
  uintptr_t highest_addr = 0;

  result = boot_iterate_mmap_entries(NULL, &highest_addr, get_highest_address);

  if (result < 0) {
    pmm_log_error(
        "Failed to iterate memory map entries to find highest address.\n");
    return -1;
  }

  size_t metadata_num_frames = highest_addr / self->page_size;
  size_t raw_metadata_size = metadata_num_frames * sizeof(frame_info_t);
  size_t metadata_size = page_align_up(raw_metadata_size, self->page_size);

  pmm_log_print("Metadata size: %zu bytes (%zu frames)\n", metadata_size,
                metadata_num_frames);

  // @@@ find a usable memory region that can accommodate the metadata @@@
  struct enough_space_context_s es_context = {0};

  es_context.required_size = metadata_size;
  es_context.minimum_size = 0;
  es_context.base = 0;

  result = boot_iterate_mmap_entries(NULL, &es_context, find_enough_space);

  if (result < 0) {
    pmm_log_error(
        "Failed to iterate memory map entries to find enough space.\n");
    return -1;
  }

  if (es_context.minimum_size == 0 && es_context.base == 0) {
    // Memory map has no single chunk large enough.
    // In a real kernel, panic() here.
    pmm_log_error(
        "No usable memory region found that can accommodate the metadata.\n");
    return -1;
  }

  pmm_log_print("Metadata will be placed at physical address: 0x%zx\n",
                es_context.base);
  pmm_log_print("Metadata will occupy %zu bytes (%zu frames)\n", metadata_size,
                metadata_num_frames);

  // @@@ Initialize the buddy allocator @@@
  // Convert to virtual address
  self->metadata = (frame_info_t*)(es_context.base + self->hhdm_offset);

  buddy_init(&self->buddy_object, self->page_size, self->hhdm_offset,
             self->metadata, metadata_num_frames);

  struct feed_region_context_s fr_context = {0};

  fr_context.buddy_state = &self->buddy_object;
  fr_context.metadata_base = es_context.base;
  fr_context.metadata_length = metadata_size;

  result = boot_iterate_mmap_entries(NULL, &fr_context, feed_region);

  if (result < 0) {
    pmm_log_error(
        "Failed to iterate memory map entries to feed usable regions.\n");
    return -1;
  }

  self->total_memory_size = memory_size;
  self->usable_memory_size =
      self->buddy_object.total_usable_frames * self->page_size;

  // @@@ Print the state of the buddy allocator @@@
#ifdef PMM_DEBUG
  buddy_print_state(&self->buddy_object);

  // print pmm state
  log_print("PMM State:\n");
  log_print("  Total Memory Size: %zu bytes\n", self->total_memory_size);
  log_print("  Usable Memory Size: %zu bytes\n", self->usable_memory_size);
  log_print("  Page Size: %zu bytes\n", self->page_size);
  log_print("  HHDM Offset: 0x%zx\n", self->hhdm_offset);
  log_print("  Metadata Address: 0x%zx\n", (uintptr_t)self->metadata);
#endif

  return 0;
}
