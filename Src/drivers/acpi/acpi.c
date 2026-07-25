#include <drivers/acpi/acpi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

static struct acpiState_s acpiState = {
    .fadt = NULL,
    .madt = NULL,
    .hpet = NULL,
    .mcfg = NULL,
    .waet = NULL,
    .dsdt = NULL,
    .bgrt = NULL,
    .physToVirt = NULL,
};

static inline void parseSDT(void* addr, void* (*physToVirt)(void*)) {
  struct acpiSdtHeader_s* table = addr;
  table = physToVirt(table);
  char* signature = table->signature;

  if (kmemcmp(signature, "FACP", 4) == 0) {
    acpiState.fadt = (struct fadt_s*)table;
  } else if (kmemcmp(signature, "APIC", 4) == 0) {
    acpiState.madt = (struct madt_s*)table;
  } else if (kmemcmp(signature, "HPET", 4) == 0) {
    acpiState.hpet = (struct hpet_s*)table;
  } else if (kmemcmp(signature, "MCFG", 4) == 0) {
    acpiState.mcfg = (struct mcfg_s*)table;
  } else if (kmemcmp(signature, "WAET", 4) == 0) {
    acpiState.waet = (struct waet_s*)table;
  } else if (kmemcmp(signature, "BGRT", 4) == 0) {
    acpiState.bgrt = (struct bgrt_s*)table;
  } else if (kmemcmp(signature, "DSDT", 4) == 0) {
    acpiState.dsdt = (struct dsdt_s*)table;
  } else if (kmemcmp(signature, "BGRT", 4) == 0) {
    acpiState.bgrt = (struct bgrt_s*)table;
  }
}

bool initACPI(void* rsdpAddr, void* (*physToVirt)(void*)) {
  if (rsdpAddr == NULL) {
    return false;
  }

  acpiState.physToVirt = physToVirt;
  struct rsdpDescriptor_s* ptr = rsdpAddr;

  if (ptr->xsdtAddr) {
    struct xsdt_s* xsdt = (void*)ptr->xsdtAddr;
    xsdt = physToVirt(xsdt);

    size_t noEntries = xsdt->sdtHeader.length;
    noEntries -= sizeof(struct acpiSdtHeader_s);
    noEntries /= sizeof(uint64_t);

    for (size_t i = 0; i < noEntries; i++) {
      parseSDT((void*)xsdt->sdtAddresses[i], physToVirt);
    }

    return true;
  }

  return false;
}

void* getLocalApicAddr(void) {
  if (acpiState.madt == NULL) {
    return NULL;
  }

  return (void*)(uintptr_t)acpiState.madt->localApicAddr;
}

uint32_t getMADTFlags(void) {
  if (acpiState.madt == NULL) {
    return 0;
  }

  return acpiState.madt->flags;
}

size_t getMADTEntryCount(size_t type) {
  if (acpiState.madt == NULL) {
    return 0;
  }

  size_t count = 0;
  uint8_t* end = (uint8_t*)acpiState.madt;
  end += acpiState.madt->sdtHeader.length;

  uint8_t* ptr = (uint8_t*)acpiState.madt->entries;

  while (ptr < end) {
    madtEntryHeader_t* t = (madtEntryHeader_t*)ptr;

    if (t->type == type) {
      count++;
    }

    ptr += t->length;
  }
  return count;
}

void* getMADTApicEntry(size_t type) {
  if (acpiState.madt == NULL) {
    return NULL;
  }

  uint8_t* end = (uint8_t*)acpiState.madt;
  end += acpiState.madt->sdtHeader.length;

  uint8_t* ptr = (uint8_t*)acpiState.madt->entries;

  while (ptr < end) {
    madtEntryHeader_t* t = (madtEntryHeader_t*)ptr;

    if (t->type == type) {
      return (void*)t;
    }

    ptr += t->length;
  }
  return NULL;
}

struct hpet_s* getHpet(void) {
  if (acpiState.hpet == NULL) {
    return NULL;
  }

  return acpiState.hpet;
}

size_t getMCFGNumEntries(void) {
  if (acpiState.mcfg == NULL) {
    return 0;
  }

  size_t length = acpiState.mcfg->sdtHeader.length;
  length -= sizeof(struct acpiSdtHeader_s);

  return length / sizeof(struct mcfgsEntry_s);
}

void* getMCFGEntry(size_t index) {
  if (acpiState.mcfg == NULL) {
    return NULL;
  }

  size_t numEntries = getMCFGNumEntries();
  if (index >= numEntries) {
    return NULL;
  }

  return &acpiState.mcfg->entries[index];
}

void debugMCFG(void) {
  if (acpiState.mcfg == NULL) {
    LOG_DEBUG("MCFG is NULL\n");
    return;
  }

  LOG_NEWLINE();
  LOG_DEBUG("MCFG Header:\n");
  LOG_DEBUG("Signature: %.4s\n", acpiState.mcfg->sdtHeader.signature);
  LOG_DEBUG("Length: %u\n", acpiState.mcfg->sdtHeader.length);
  LOG_DEBUG("Revision: %u\n", acpiState.mcfg->sdtHeader.revision);
  LOG_DEBUG("Checksum: %u\n", acpiState.mcfg->sdtHeader.checksum);
  LOG_DEBUG("OEM ID: %.6s\n", acpiState.mcfg->sdtHeader.oemid);
  LOG_DEBUG("OEM Table ID: %.8s\n", acpiState.mcfg->sdtHeader.oem_tableid);
  LOG_DEBUG("OEM Revision: %u\n", acpiState.mcfg->sdtHeader.oem_revision);
  LOG_DEBUG("Creator ID: %u\n", acpiState.mcfg->sdtHeader.creator_id);
  LOG_DEBUG("Creator Revision: %u\n",
            acpiState.mcfg->sdtHeader.creator_revision);
  LOG_DEBUG("Number of MCFG Entries: %zu\n", getMCFGNumEntries());

  struct mcfgsEntry_s *entry = acpiState.mcfg->entries;
  for (size_t i = 0; i < getMCFGNumEntries(); i++) {
    LOG_DEBUG("MCFG Entry %zu:\n", i + 1);
    LOG_DEBUG("Base Address: 0x%016lx\n", entry[i].baseAddress);
    LOG_DEBUG("PCI Segment Group Number: %u\n", entry[i].pciSegmentGroupNumber);
    LOG_DEBUG("Start Bus Number: %u\n", entry[i].startBusNumber);
    LOG_DEBUG("End Bus Number: %u\n", entry[i].endBusNumber);
  }
}
