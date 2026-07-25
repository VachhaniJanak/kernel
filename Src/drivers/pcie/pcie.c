#include <arch/x86_64/mmu.h>
#include <drivers/acpi/acpi.h>
#include <drivers/pcie/pcie.h>
#include <mm/mm.h>
#include <stdint.h>
#include <utils/log.h>

#define PCI_CFG_SPACE_SIZE 4096

#define PCI_CFG_SPACE_BUS_SIZE 256
#define PCI_CFG_SPACE_DEVICE_SIZE 32
#define PCI_CFG_SPACE_FUNCTION_SIZE 8

// Extended Configuration Access Management
void* ecam_addr(void* base, uint8_t bus, uint8_t device, uint8_t function) {
  uintptr_t address = (uintptr_t)base;
  address += ((uint64_t)bus << 20);
  address += ((uint64_t)device << 15);
  address += ((uint64_t)function << 12);

  return (void*)address;
}

void debug_pci_config_header(pci_config_header_t* cfg) {
  LOG_NEWLINE();
  LOG_DEBUG("Vendor ID: 0x%04X\n", cfg->vendor_id);
  LOG_DEBUG("Device ID: 0x%04X\n", cfg->device_id);
  LOG_DEBUG("Command: 0x%04X\n", cfg->command);
  LOG_DEBUG("Status: 0x%04X\n", cfg->status);
  LOG_DEBUG("Revision ID: 0x%02X\n", cfg->revision_id);
  LOG_DEBUG("Programming Interface: 0x%02X\n", cfg->prog_if);
  LOG_DEBUG("Subclass: 0x%02X\n", cfg->subclass);
  LOG_DEBUG("Class Code: 0x%02X\n", cfg->class_code);
  LOG_DEBUG("Cache Line Size: %u\n", cfg->cache_line_size);
  LOG_DEBUG("Latency Timer: %u\n", cfg->latency_timer);
  LOG_DEBUG("Header Type: 0x%02X\n", cfg->header_type);
  LOG_DEBUG("BIST: 0x%02X\n", cfg->bist);
}

void init_pcie(void) {
#ifdef PCIE_DEBUG
  debugMCFG();
#endif

  struct mcfgsEntry_s* entry = getMCFGEntry(0);
  if (entry == NULL) {
    LOG_ERROR("No MCFG entry found. PCIe initialization failed.");
    return;
  }

  uint64_t baseAddress = entry->baseAddress;
  // uint8_t startBusNumber = entry->startBusNumber;
  // uint8_t endBusNumber = entry->endBusNumber;

  void* virtBaseAddress = phys_to_virt((void*)baseAddress);

  for (size_t i = 0; i < 256; i++) {
    void* virtAddr =
        (void*)((uintptr_t)virtBaseAddress + (i * PCI_CFG_SPACE_SIZE));
    if (!mmap(virtAddr, (void*)(baseAddress + (i * PCI_CFG_SPACE_SIZE)))) {
      LOG_ERROR("Failed to map PCIe configuration space for bus %zu.", i);
      return;
    }
  }

  // for (uint8_t device = 0; device < PCI_CFG_SPACE_DEVICE_SIZE; device++) {
  //   for (uint8_t function = 0; function < PCI_CFG_SPACE_FUNCTION_SIZE;
  //        function++) {
  //     pci_config_header_t* cfgSpace =
  //         ecam_addr(virtBaseAddress, 0, device, function);

  //     uint16_t vendorID = cfgSpace->vendor_id;

  //     if (vendorID == 0xFFFF) {
  //       continue;
  //     }

  //     debug_pci_config_header(cfgSpace);
  //   }
  // }
}

void* get_pci_cfg_space(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
  struct mcfgsEntry_s* entry = getMCFGEntry(0);

  if (entry == NULL) {
    LOG_ERROR("No MCFG entry found. Cannot retrieve PCIe configuration space.");
    return NULL;
  }

  uint64_t baseAddress = entry->baseAddress;
  void* virtBaseAddress = phys_to_virt((void*)baseAddress);

  for (uint8_t device = 0; device < PCI_CFG_SPACE_DEVICE_SIZE; device++) {
    for (uint8_t function = 0; function < PCI_CFG_SPACE_FUNCTION_SIZE;
         function++) {
      pci_config_header_t* cfgSpace =
          ecam_addr(virtBaseAddress, 0, device, function);

      if (cfgSpace->class_code == class_code &&
          cfgSpace->subclass == subclass && cfgSpace->prog_if == prog_if) {
        return cfgSpace;
      }
    }
  }

  return NULL;
}

bool set_pci_msi(pci_config_header_t* config_space, size_t vector,
                 uint64_t msi_address) {
  uint8_t cap_ptr = config_space->capabilities_ptr;

  while (cap_ptr != 0) {
    pci_cap_header_t* cap_hdr =
        (pci_cap_header_t*)((uint8_t*)config_space + cap_ptr);

    uint8_t cap_id = cap_hdr->cap_id;
    uint8_t next_cap_ptr = cap_hdr->next;

    if (cap_id == PCI_CAPABILITY_ID_MSI) {
      pci_msi_cap_hdr_t* msi_hdr = (pci_msi_cap_hdr_t*)cap_hdr;

      if (msi_hdr->msg_ctrl & (1 << 7)) {
        pci_msi64_cap_t* msi64 = (pci_msi64_cap_t*)cap_hdr;
        msi64->msg_addr_low = (uint32_t)(msi_address & 0xFFFFFFFF);
        msi64->msg_addr_high = (uint32_t)((msi_address >> 32) & 0xFFFFFFFF);
        msi64->msg_data = (uint16_t)(vector & 0xFFFF);
        msi64->msi_hdr.msg_ctrl |= (1 << 0);  // Enable MSI
        return true;
      }
      pci_msi32_cap_t* msi32 = (pci_msi32_cap_t*)cap_hdr;
      msi32->msg_addr = (uint32_t)(msi_address & 0xFFFFFFFF);
      msi32->msg_data = (uint16_t)(vector & 0xFFFF);
      msi32->msi_hdr.msg_ctrl |= (1 << 0);  // Enable MSI
      return true;
    }
    cap_ptr = next_cap_ptr;
  }
  return false;
}

void print_all_capabilities(pci_config_header_t* config_space) {
  uint8_t cap_ptr = config_space->capabilities_ptr;

  LOG_NEWLINE();
  LOG_DEBUG("PCIe Capabilities:\n");
  while (cap_ptr != 0) {
    pci_cap_header_t* cap_hdr =
        (pci_cap_header_t*)((uint8_t*)config_space + cap_ptr);

    uint8_t cap_id = cap_hdr->cap_id;
    uint8_t next_cap_ptr = cap_hdr->next;

    if (cap_id == PCI_CAPABILITY_ID_MSI) {
      pci_msi_cap_hdr_t* msi_hdr = (pci_msi_cap_hdr_t*)cap_hdr;
      LOG_DEBUG("MSI Capability found at offset 0x%02X\n", cap_ptr);
      LOG_DEBUG("Message Control: 0x%04X\n", msi_hdr->msg_ctrl);

      if (msi_hdr->msg_ctrl & (1 << 7)) {
        LOG_DEBUG("64-bit Addressing is supported.\n");
        pci_msi64_cap_t* msi64 = (pci_msi64_cap_t*)cap_hdr;
        LOG_DEBUG("Message Control: 0x%04X\n", msi64->msi_hdr.msg_ctrl);
        LOG_DEBUG("Message Address (Low): 0x%X\n", msi64->msg_addr_low);
        LOG_DEBUG("Message Address (High): 0x%X\n", msi64->msg_addr_high);
        LOG_DEBUG("Message Data: 0x%X\n", msi64->msg_data);
      } else {
        LOG_DEBUG("64-bit Addressing is NOT supported.\n");
      }
    }

    cap_ptr = next_cap_ptr;
  }
}
