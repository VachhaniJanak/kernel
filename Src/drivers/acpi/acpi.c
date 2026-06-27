#include <drivers/acpi/acpi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

static inline void parseMADT(void* addr, void* (*physToVirt)(void*)) {
  struct madt_s* madt_ptr = addr;

  LOG_PRINT("Table name: %.4s\n", madt_ptr->sdtHeader.signature);
  LOG_PRINT("Table length: %lu\n", madt_ptr->sdtHeader.length);
  LOG_PRINT("Lapic addr: 0x%x\n", madt_ptr->localApicAddr);
  LOG_PRINT("Flag: %u\n", madt_ptr->flags);

  uint8_t* end = (uint8_t*)addr + madt_ptr->sdtHeader.length;
  uint8_t* ptr = (uint8_t*)addr + sizeof(struct madt_s);

  while (ptr < end) {
    madtEntryHeader_t* t = (madtEntryHeader_t*)ptr;
    LOG_PRINT("Type: %u, Length: %u\n", t->type, t->length);
    ptr += t->length;
  }
}

static void parseSdt(void* addr, void* (*physToVirt)(void*)) {
  struct acpiSdtHeader_s* table = addr;
  table = physToVirt(table);
  char* signature = table->signature;

  if (kmemcmp(signature, "FACP", 4) == 0) {
    // LOG_PRINT("Table Name: %.4s\n", table->signature);
  } else if (kmemcmp(signature, "APIC", 4) == 0) {
    parseMADT(table, physToVirt);
  } else if (kmemcmp(signature, "HPET", 4) == 0) {
    // LOG_PRINT("Table Name: %.4s\n", table->signature);
  } else if (kmemcmp(signature, "MCFG", 4) == 0) {
    // LOG_PRINT("Table Name: %.4s\n", table->signature);
  } else if (kmemcmp(signature, "WAET", 4) == 0) {
    // LOG_PRINT("Table Name: %.4s\n", table->signature);
  } else if (kmemcmp(signature, "DSDT", 4) == 0) {
    // LOG_PRINT("Table Name: %.4s\n", table->signature);
  } else if (kmemcmp(signature, "BGRT", 4) == 0) {
    // LOG_PRINT("Table Name: %.4s\n", table->signature);
  }
}

bool initACPI(void* rsdpAddr, void* (*physToVirt)(void*)) {
  if (rsdpAddr == NULL) {
    return false;
  }

  struct rsdpDescriptor_s* ptr = rsdpAddr;

  if (ptr->xsdtAddr) {
    struct xsdt_s* xsdt = (void*)ptr->xsdtAddr;
    xsdt = physToVirt(xsdt);

    size_t noEntries = xsdt->sdtHeader.length;
    noEntries -= sizeof(struct acpiSdtHeader_s);
    noEntries /= sizeof(uint64_t);

    for (size_t i = 0; i < noEntries; i++) {
      parseSdt((void*)xsdt->sdtAddresses[i], physToVirt);
    }

    return true;
  }

  return false;
}
