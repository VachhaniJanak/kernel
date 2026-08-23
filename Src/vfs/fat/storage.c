#include <drivers/ahci/ahci.h>
#include <mm/vmm/kheap.h>
#include <stdint.h>
#include <utils/log.h>

#include "gpt.h"
#include "utils.h"

static gpt_primary_header_t header;
static gpt_partition_entry_t entry;

int init_disk(void) {
  if (!get_gpt_header(&header)) {
    LOG_ERROR("Failed to get GPT header");
    return 1;
  }

  if (!get_gpt_partition_entry(&header, &entry, 1)) {
    LOG_ERROR("Failed to get GPT partition entry");
    return 1;
  }

  return 0;
}

void read_dev_disk(uint8_t* buff, size_t sector, size_t count) {
  // log_info("Reading from disk: sector=%zu, count=%zu", sector, count);
  buff = kmalloc_phys_addr(buff);
  ahci_read_disk(entry.start_LBA + sector, count, buff);
}

void write_dev_disk(const uint8_t* buff, size_t sector, size_t count) {
  // log_info("Writing to disk: sector=%zu, count=%zu", sector, count);
  buff = kmalloc_phys_addr(buff);
  ahci_write_disk(entry.start_LBA + sector, count, buff);
}
