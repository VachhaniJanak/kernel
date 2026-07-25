#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// PCI Configuration Space Header (First 64 bytes)
typedef struct {
  // --- 0x00-0x01: Vendor and Device Identification ---
  uint16_t vendor_id;
  uint16_t device_id;

  // --- 0x04-0x05: Command and Status ---
  uint16_t command;
  uint16_t status;

  // --- 0x08-0x0B: Classification ---
  uint8_t revision_id;
  uint8_t prog_if;
  uint8_t subclass;
  uint8_t class_code;

  // --- 0x0C-0x0F: Cache and Timer ---
  uint8_t cache_line_size;
  uint8_t latency_timer;
  uint8_t header_type;
  uint8_t bist;

  // --- 0x10-0x27: Base Address Registers (BARs) ---
  uint32_t bar0;
  uint32_t bar1;
  uint32_t bar2;
  uint32_t bar3;
  uint32_t bar4;
  uint32_t bar5;

  // --- 0x28-0x2B: CardBus and Subsystem ---
  uint32_t cardbus_cis_ptr;

  // --- 0x2C-0x2F: Subsystem Identification ---
  uint16_t subsystem_vendor_id;
  uint16_t subsystem_id;

  // --- 0x30-0x33: Expansion ROM ---
  uint32_t expansion_rom_addr;

  // --- 0x34-0x3B: Capabilities and Reserved ---
  uint8_t capabilities_ptr;
  uint8_t reserved1[3];

  // --- 0x3C-0x3F: Interrupt and Bridge ---
  uint32_t reserved2;
  uint8_t interrupt_line;
  uint8_t interrupt_pin;
  uint8_t min_grant;
  uint8_t max_latency;
} __attribute__((packed)) pci_config_header_t;

enum PCIHeaderType {
  PCI_HEADER_TYPE_NORMAL = 0x00,
  PCI_HEADER_TYPE_BRIDGE = 0x01,
  PCI_HEADER_TYPE_CARDBUS = 0x02
};

enum PCIClassCode {
  PCI_CLASS_CODE_MASS_STORAGE = 0x01,
  PCI_CLASS_CODE_NETWORK = 0x02,
  PCI_CLASS_CODE_DISPLAY = 0x03,
  PCI_CLASS_CODE_MULTIMEDIA = 0x04,
  PCI_CLASS_CODE_MEMORY = 0x05,
  PCI_CLASS_CODE_BRIDGE = 0x06,
  PCI_CLASS_CODE_SIMPLE_COMMUNICATIONS = 0x07,
  PCI_CLASS_CODE_BASE_SYSTEM_PERIPHERALS = 0x08,
  PCI_CLASS_CODE_INPUT_DEVICES = 0x09,
  PCI_CLASS_CODE_DOCKING_STATIONS = 0x0A,
  PCI_CLASS_CODE_PROCESSORS = 0x0B,
  PCI_CLASS_CODE_SERIAL_BUS_CONTROLLERS = 0x0C,
  PCI_CLASS_CODE_WIRELESS_CONTROLLERS = 0x0D,
  PCI_CLASS_CODE_INTELLIGENT_IO_CONTROLLERS = 0x0E,
  PCI_CLASS_CODE_SATELLITE_COMMUNICATIONS_CONTROLLERS = 0x0F,
  PCI_CLASS_CODE_ENCRYPTION_CONTROLLERS = 0x10,
  PCI_CLASS_CODE_SIGNAL_PROCESSING_CONTROLLERS = 0x11
};

enum PCISubclassCode {
  PCI_SUBCLASS_CODE_SCSI = 0x00,
  PCI_SUBCLASS_CODE_IDE = 0x01,
  PCI_SUBCLASS_CODE_FLOPPY = 0x02,
  PCI_SUBCLASS_CODE_IPI = 0x03,
  PCI_SUBCLASS_CODE_RAID = 0x04,
  PCI_SUBCLASS_CODE_ATA = 0x05,
  PCI_SUBCLASS_CODE_SATA = 0x06,
  PCI_SUBCLASS_CODE_SAS = 0x07,
  PCI_SUBCLASS_CODE_OTHER_STORAGE = 0x80
};

enum PCIProgIF {
  PCI_PROG_IF_NATIVE_MODE = 0x00,
  PCI_PROG_IF_COMPATIBILITY_MODE = 0x01
};

enum PCICommand {
  PCI_COMMAND_IO_SPACE = 0x1,
  PCI_COMMAND_MEMORY_SPACE = 0x2,
  PCI_COMMAND_BUS_MASTER = 0x4,
  PCI_COMMAND_SPECIAL_CYCLES = 0x8,
  PCI_COMMAND_MEM_WRITE_INVALIDATE = 0x10,
  PCI_COMMAND_VGA_PALETTE_SNOOP = 0x20,
  PCI_COMMAND_PARITY_ERROR_RESPONSE = 0x40,
  PCI_COMMAND_WAIT_CYCLE_CONTROL = 0x80,
  PCI_COMMAND_SERR_ENABLE = 0x100,
  PCI_COMMAND_FAST_BACK_TO_BACK_ENABLE = 0x200
};

enum PCIStatus {
  PCI_STATUS_66MHZ_CAPABLE = 0x20,
  PCI_STATUS_CAPABILITIES_LIST = 0x10,
  PCI_STATUS_INTERRUPT_STATUS = 0x08,
  PCI_STATUS_PARITY_ERROR_DETECTED = 0x04,
  PCI_STATUS_DEVSEL_TIMING = 0x03
};

enum PCICapabilities {
  PCI_CAPABILITY_ID_PM = 0x01,
  PCI_CAPABILITY_ID_MSI = 0x05,
  PCI_CAPABILITY_ID_MSIX = 0x11,
  PCI_CAPABILITY_ID_PCIE = 0x10,
  PCI_CAPABILITY_ID_VENDOR_SPECIFIC = 0x09
};

typedef struct {
  uint8_t cap_id;
  uint8_t next;
} __attribute__((packed)) pci_cap_header_t;

// Power Management Capability Structure
typedef struct {
  pci_cap_header_t header;

  uint16_t pmc;    // Power Management Capabilities
  uint16_t pmcsr;  // Power Management Control/Status
  uint8_t bridge_ext;
  uint8_t data;
} __attribute__((packed)) pci_pm_cap_t;

// MSI Capability Structure
typedef struct {
  pci_cap_header_t header;
  uint16_t msg_ctrl;
} __attribute__((packed)) pci_msi_cap_hdr_t;

typedef struct {
  pci_msi_cap_hdr_t msi_hdr;

  uint32_t msg_addr;
  uint16_t msg_data;
} __attribute__((packed)) pci_msi32_cap_t;

typedef struct {
  pci_msi_cap_hdr_t msi_hdr;

  uint32_t msg_addr_low;
  uint32_t msg_addr_high;

  uint16_t msg_data;
} __attribute__((packed)) pci_msi64_cap_t;

// Vital Product Data
typedef struct {
  pci_msi_cap_hdr_t msi_hdr;

  uint16_t vpd_addr;
  uint32_t vpd_data;
} __attribute__((packed)) pci_vpd_cap_t;

// Vendor Specific Capability
typedef struct {
  pci_cap_header_t header;
  uint8_t length;
  uint8_t vendor_data[];
} __attribute__((packed)) pci_vendor_cap_t;

// PCI Express Capability
typedef struct {
  pci_cap_header_t header;

  uint16_t pcie_caps;

  uint32_t dev_caps;
  uint16_t dev_ctrl;
  uint16_t dev_status;

  uint32_t link_caps;
  uint16_t link_ctrl;
  uint16_t link_status;

  uint32_t slot_caps;
  uint16_t slot_ctrl;
  uint16_t slot_status;

  uint16_t root_ctrl;
  uint16_t root_caps;
  uint32_t root_status;
} __attribute__((packed)) pci_pcie_cap_t;

typedef struct {
  pci_cap_header_t header;

  uint16_t msg_ctrl;
  uint32_t table;
  uint32_t pba;
} __attribute__((packed)) pci_msix_cap_t;

void init_pcie(void);

void* get_pci_cfg_space(uint8_t class_code, uint8_t subclass, uint8_t prog_if);

static inline bool is_pci_bar_io_space(uint32_t bar) {
  return (bar & 0x1) != 0;
}

static inline bool is_pci_bar_64bit(uint32_t bar) {
  return ((bar & 0x6) == 0x4);
}

static inline uint32_t get_pci_bar_address(uint32_t bar) { return bar & ~0xF; }

static inline void pci_mem_space_enable(pci_config_header_t* cfgSpace) {
  cfgSpace->command |= PCI_COMMAND_MEMORY_SPACE;
}

static inline void pci_bus_master_enable(pci_config_header_t* cfgSpace) {
  cfgSpace->command |= PCI_COMMAND_BUS_MASTER;
}

bool set_pci_msi(pci_config_header_t* config_space, size_t vector,
                 uint64_t msi_address);