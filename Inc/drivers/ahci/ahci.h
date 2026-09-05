#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  AHCI_SUCCESS = 0,
  AHCI_ERR_NO_DEVICE = -1,
  AHCI_ERR_PCI = -2,
  AHCI_ERR_OUT_OF_MEMORY = -3,
  AHCI_ERR_MEMORY_MAPPING = -4,
  AHCI_ERR_SATA_NOT_FOUND = -5,
  AHCI_ERR_BAR = -6,
} ahci_result_t;

typedef enum {
  ATA_CMD_READ_DMA_EX = 0x25,
  ATA_CMD_WRITE_DMA_EX = 0x35,
  ATA_CMD_FLUSH_CACHE_EX = 0xEA,
  ATA_CMD_IDENTIFY_DEVICE = 0xEC,
} ata_cmd_e;

typedef enum {
  FIS_TYPE_REG_H2D = 0x27,    // Register FIS - host to device
  FIS_TYPE_REG_D2H = 0x34,    // Register FIS - device to host
  FIS_TYPE_DMA_ACT = 0x39,    // DMA activate FIS - device to host
  FIS_TYPE_DMA_SETUP = 0x41,  // DMA setup FIS - bidirectional
  FIS_TYPE_DATA = 0x46,       // Data FIS - bidirectional
  FIS_TYPE_BIST = 0x58,       // BIST activate FIS - bidirectional
  FIS_TYPE_PIO_SETUP = 0x5F,  // PIO setup FIS - device to host
  FIS_TYPE_DEV_BITS = 0xA1,   // Set device bits FIS - device to host
} fis_type_e;

typedef volatile struct __attribute__((packed)) {
  uint32_t clb;               // command list base address, 1K-byte aligned
  uint32_t clbu;              // command list base address upper 32 bits
  uint32_t fb;                // FIS base address, 256-byte aligned
  uint32_t fbu;               // FIS base address upper 32 bits
  uint32_t interrupt_status;  // interrupt status
  uint32_t interrupt_enable;  // interrupt enable
  uint32_t command;           // command and status
  uint32_t reserved0;
  uint32_t tfd;             // task file data
  uint32_t signature;       // signature
  uint32_t s_status;        // SATA status (SCR0:SStatus)
  uint32_t s_control;       // SATA control (SCR2:SControl)
  uint32_t s_error;         // SATA error (SCR1:SError)
  uint32_t s_active;        // SATA active (SCR3:SActive)
  uint32_t command_issue;   // command issue
  uint32_t s_notification;  // SATA notification (SCR4:SNotification)
  uint32_t fbs;             // FIS-based switch control
  uint32_t reserved1[11];
  uint32_t vendor[4];  // vendor specific
} hba_port_t;

typedef volatile struct __attribute__((packed)) {
  // Generic Host Control
  uint32_t capability;           // Host capability
  uint32_t global_host_control;  // Global host control
  uint32_t interrupt_status;     // Interrupt status
  uint32_t port_implemented;     // Port implemented
  uint32_t version;              // Version
  uint32_t ccc_contorl;          // Command completion coalescing control
  uint32_t ccc_ports;            // Command completion coalescing ports
  uint32_t em_location;          // Enclosure management location
  uint32_t em_control;           // Enclosure management control
  uint32_t capability_e;         // Host capabilities extended
  uint32_t bohc;                 // BIOS/OS handoff control and status

  uint8_t reserved[0xA0 - 0x2C];

  // Vendor specific registers
  uint8_t vendor[0x100 - 0xA0];

  // Port control registers
  hba_port_t ports[32];
} hba_mem_t;

typedef struct __attribute__((packed)) {
  // DW0
  uint8_t cfl : 5;  // Command FIS length in DWORDS, 2 ~ 16
  uint8_t a : 1;    // ATAPI
  uint8_t w : 1;    // Write, 1: H2D, 0: D2H
  uint8_t p : 1;    // Prefetchable

  uint8_t r : 1;     // Reset
  uint8_t b : 1;     // BIST
  uint8_t c : 1;     // Clear busy upon R_OK
  uint8_t rsv0 : 1;  // Reserved
  uint8_t pmp : 4;   // Port multiplier port

  uint16_t prdtl;  // Physical region descriptor table length in entries

  // DW1
  volatile uint32_t prdbc;  // Physical region descriptor byte count transferred

  // DW2, 3
  uint32_t ctba;   // Command table descriptor base address
  uint32_t ctbau;  // Command table descriptor base address upper 32 bits

  // DW4 - 7
  uint32_t rsv1[4];  // Reserved
} hba_cmd_header_t;

typedef struct __attribute__((packed)) {
  uint32_t dba;   // Data base address
  uint32_t dbau;  // Data base address upper 32 bits
  uint32_t rsv0;  // Reserved

  // DW3
  uint32_t dbc : 22;  // Byte count, 4M max
  uint32_t rsv1 : 9;  // Reserved
  uint32_t i : 1;     // Interrupt on completion
} hba_prdt_entry_t;

typedef struct __attribute__((packed)) {
  uint8_t cfis[64];  // Command FIS
  uint8_t acmd[16];  // ATAPI command, 12 or 16 bytes
  uint8_t rsv[48];   // Reserved
  hba_prdt_entry_t prdt_entry[];
} hba_cmd_tbl_t;

typedef struct __attribute__((packed)) {
  // DWORD 0
  uint8_t fis_type;  // FIS_TYPE_REG_H2D

  uint8_t pmport : 4;  // Port multiplier
  uint8_t rsv0 : 3;    // Reserved
  uint8_t c : 1;       // 1: Command, 0: Control

  uint8_t command;   // Command register
  uint8_t featurel;  // Feature register, 7:0

  // DWORD 1
  uint8_t lba0;    // LBA low register, 7:0
  uint8_t lba1;    // LBA mid register, 15:8
  uint8_t lba2;    // LBA high register, 23:16
  uint8_t device;  // Device register

  // DWORD 2
  uint8_t lba3;      // LBA register, 31:24
  uint8_t lba4;      // LBA register, 39:32
  uint8_t lba5;      // LBA register, 47:40
  uint8_t featureh;  // Feature register, 15:8

  // DWORD 3
  uint8_t countl;   // Count register, 7:0
  uint8_t counth;   // Count register, 15:8
  uint8_t icc;      // Isochronous command completion
  uint8_t control;  // Control register

  // DWORD 4
  uint8_t rsv1[4];  // Reserved
} fis_reg_h2d_t;

ahci_result_t ahci_init(void);

bool ahci_read_disk(uint64_t start_lba, uint32_t sector_count, void* buffer);

bool ahci_write_disk(uint64_t start_lba, uint32_t sector_count, void* buffer);
