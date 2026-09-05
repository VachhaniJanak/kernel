#include <arch/x86_64/apic.h>
#include <drivers/ahci/ahci.h>
#include <drivers/pcie/pcie.h>
#include <mm/mm.h>
#include <mm/vmm/kheap.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

// #define AHCI_DEBUG

#ifdef AHCI_DEBUG
#define ahci_log_print(fmt, ...) log_print(fmt, ##__VA_ARGS__)
#define ahci_log_error(fmt, ...) (log_error(fmt, ##__VA_ARGS__))
#define ahci_log_debug(fmt, ...) (log_debug(fmt, ##__VA_ARGS__))
#else
#define ahci_log_print(fmt, ...) ((void)0)
#define ahci_log_error(fmt, ...) ((void)0)
#define ahci_log_debug(fmt, ...) ((void)0)
#endif

#define AHCI_SECTOR_SIZE 512

#define AHCI_CLASS_CODE 0x01
#define AHCI_SUBCLASS 0x06
#define AHCI_PROG_IF 0x01

#define HBA_CLB_SIZE 1024
#define HBA_FIS_SIZE 256
#define HBA_CMD_TBL_SIZE 256

#define SATA_SIG_ATA 0x00000101    // SATA drive
#define SATA_SIG_ATAPI 0xEB140101  // SATAPI drive
#define SATA_SIG_SEMB 0xC33C0101   // Enclosure management bridge
#define SATA_SIG_PM 0x96690101     // Port multiplier

#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_FR 0x4000
#define HBA_PxCMD_CR 0x8000
#define HBA_PxIS_TFES (1 << 30)

#define HBA_PORT_IPM_ACTIVE 1
#define HBA_PORT_DET_PRESENT 3

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08

typedef enum {
  AHCI_DEV_NULL = 0,
  AHCI_DEV_SATA = 1,
  AHCI_DEV_SEMB = 2,
  AHCI_DEV_PM = 3,
  AHCI_DEV_SATAPI = 4
} ahci_dev_type_e;

typedef struct {
  void* clb_vaddr;
  void* clb_paddr;
  void* fis_vaddr;
  void* fis_paddr;
  void* cmd_table_vaddr;
  void* cmd_table_paddr;
} ahci_sata_state_t;

typedef struct {
  hba_mem_t* hba_mem;
  ahci_sata_state_t sata_state[32];
  int sata_port_index;
} ahci_state_t;

volatile bool is_transfer_complete = false;
static ahci_state_t ahci_state = {0};

void ahci_irq_isr_handler(void) {
  // is_transfer_complete = true;
  // ahci_state.hba_mem->interrupt_status =
  // (uint32_t)-1;  // Clear interrupt status
  lapic_eoi();
  ahci_log_debug("AHCI interrupt received, transfer complete.\n");
}

static inline size_t num_command_slots(hba_mem_t* hba_mem) {
  return ((hba_mem->capability >> 8) & 0x1F) + 1;
}

static inline size_t num_ports(hba_mem_t* hba_mem) {
  return (hba_mem->port_implemented & 0x1F) + 1;
}

static inline size_t supports_speed(hba_mem_t* hba_mem) {
  return hba_mem->capability >> 20 & 0xF;
}

static inline bool supports_staggered_spinup(hba_mem_t* hba_mem) {
  return (hba_mem->capability >> 27) & 1;
}

static inline bool supports_ncq(hba_mem_t* hba_mem) {
  return (hba_mem->capability >> 30) & 1;
}

static inline bool supports_64bit(hba_mem_t* hba_mem) {
  return (hba_mem->capability_e >> 31) & 1;
}

static inline bool ahci_host_reset(hba_mem_t* hba_mem) {
  volatile size_t spin = 0;
  hba_mem->global_host_control |= (1 << 0);  // Set the HBA reset bit
  while (hba_mem->global_host_control & (1 << 0)) {
    // Wait for the reset to complete
    spin++;

    if (spin > 100000) {
      return false;
    }
  }
  return true;
}

static inline void ahci_enable_interrupts(hba_mem_t* hba_mem) {
  hba_mem->global_host_control |= (1 << 1);  // Enable interrupts
}

static inline void ahci_enable(hba_mem_t* hba_mem) {
  hba_mem->global_host_control |= (1 << 2);  // Enable AHCI mode
}

// Check device type
static ahci_dev_type_e check_type(hba_port_t* port) {
  uint32_t ssts = port->s_status;  // SATA status (SCR0)

  uint8_t ipm = (ssts >> 8) & 0x0F;
  uint8_t det = ssts & 0x0F;

  if (det != HBA_PORT_DET_PRESENT) {
    return AHCI_DEV_NULL;
  }

  if (ipm != HBA_PORT_IPM_ACTIVE) {
    return AHCI_DEV_NULL;
  }

  switch (port->signature) {
    case SATA_SIG_ATAPI:
      return AHCI_DEV_SATAPI;
    case SATA_SIG_SEMB:
      return AHCI_DEV_SEMB;
    case SATA_SIG_PM:
      return AHCI_DEV_PM;
    default:
      return AHCI_DEV_SATA;
  }
}

static inline size_t get_device_port(hba_mem_t* hba_mem,
                                     ahci_dev_type_e dev_type) {
  uint32_t pi = hba_mem->port_implemented;
  for (size_t i = 0; i < 32; i++) {
    if (pi & (1 << i)) {
      hba_port_t* port = &hba_mem->ports[i];
      if (check_type(port) == dev_type) {
        return i;
      }
    }
  }
  return SIZE_MAX;  // Device type not found
}

void probe_port(hba_mem_t* abar) {
  // Search disk in implemented ports
  uint32_t pi = abar->port_implemented;
  int i = 0;
  while (i < 32) {
    if (pi & 1) {
      ahci_dev_type_e dt = check_type(&abar->ports[i]);
      if (dt == AHCI_DEV_SATA) {
        LOG_DEBUG("SATA drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_SATAPI) {
        LOG_DEBUG("SATAPI drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_SEMB) {
        LOG_DEBUG("SEMB drive found at port %d\n", i);
      } else if (dt == AHCI_DEV_PM) {
        LOG_DEBUG("PM drive found at port %d\n", i);
      } else {
        LOG_DEBUG("No drive found at port %d\n", i);
      }
    }

    pi >>= 1;
    i++;
  }
}

void start_cmd(hba_port_t* port) {
  // Wait until CR (bit15) is cleared
  while (port->command & HBA_PxCMD_CR) {
  }

  // Set FRE (bit4) and ST (bit0)
  port->command |= HBA_PxCMD_FRE;
  port->command |= HBA_PxCMD_ST;
}

void stop_cmd(hba_port_t* port) {
  // Clear ST (bit0)
  port->command &= ~HBA_PxCMD_ST;

  // Clear FRE (bit4)
  port->command &= ~HBA_PxCMD_FRE;

  // Wait until FR (bit14), CR (bit15) are cleared
  while (1) {
    if (port->command & HBA_PxCMD_FR) {
      continue;
    }

    if (port->command & HBA_PxCMD_CR) {
      continue;
    }

    break;
  }
}

static inline bool is_port_busy(hba_port_t* port) {
  return (port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) != 0;
}

static inline bool is_port_ready(hba_port_t* port) {
  return (port->s_status & 0x0F) == HBA_PORT_IPM_ACTIVE;
}

ahci_result_t sata_init(size_t sata_port_index, size_t num_slots) {
  hba_port_t* port = &ahci_state.hba_mem->ports[sata_port_index];
  ahci_sata_state_t* sata_state = &ahci_state.sata_state[sata_port_index];

  ahci_log_print("Initializing SATA port: %zu\n", sata_port_index);
  ahci_log_print("  Port command register: 0x%08X\n", port->command);
  ahci_log_print("  Port interrupt status: 0x%08X\n", port->interrupt_status);
  ahci_log_print("  Port signature: 0x%08X\n", port->signature);
  ahci_log_print("  Port SATA status: 0x%08X\n", port->s_status);
  ahci_log_print("  Port SATA control: 0x%08X\n", port->s_control);
  ahci_log_print("  Port SATA error: 0x%08X\n", port->s_error);
  ahci_log_print("  Port SATA active: 0x%08X\n", port->s_active);
  ahci_log_print("  Port command issue: 0x%08X\n", port->command_issue);
  ahci_log_print("  Port SATA notification: 0x%08X\n", port->s_notification);
  ahci_log_print("  Port FIS-based switch control: 0x%08X\n", port->fbs);

  stop_cmd(port);

  sata_state->clb_vaddr = kmalloc(HBA_CLB_SIZE);

  if (sata_state->clb_vaddr == NULL) {
    ahci_log_error("Failed to allocate memory for command list buffer.");
    return AHCI_ERR_OUT_OF_MEMORY;
  }

  mm_result_t mm_result;
  mm_result = mm_get_current_mapping(sata_state->clb_vaddr,
                                     (uintptr_t*)&sata_state->clb_paddr);

  if (mm_result != MM_SUCCESS) {
    ahci_log_error("Failed to get phys address for clb, result: %d", mm_result);
    return AHCI_ERR_MEMORY_MAPPING;
  }

  sata_state->fis_vaddr = kmalloc(HBA_FIS_SIZE);

  if (sata_state->fis_vaddr == NULL) {
    ahci_log_error("Failed to allocate memory for FIS buffer.");
    return AHCI_ERR_OUT_OF_MEMORY;
  }

  mm_result = mm_get_current_mapping(sata_state->fis_vaddr,
                                     (uintptr_t*)&sata_state->fis_paddr);

  if (mm_result != MM_SUCCESS) {
    ahci_log_error("Failed to get phys address for FIS buffer, result: %d",
                   mm_result);
    return AHCI_ERR_MEMORY_MAPPING;
  }

  sata_state->cmd_table_vaddr = kmalloc(HBA_CMD_TBL_SIZE);

  if (sata_state->cmd_table_vaddr == NULL) {
    ahci_log_error("Failed to allocate memory for command table.");
    return AHCI_ERR_OUT_OF_MEMORY;
  }

  mm_result = mm_get_current_mapping(sata_state->cmd_table_vaddr,
                                     (uintptr_t*)&sata_state->cmd_table_paddr);

  if (mm_result != MM_SUCCESS) {
    ahci_log_error("Failed to get phys addr for command table, result: %d",
                   mm_result);
    return AHCI_ERR_MEMORY_MAPPING;
  }

  kmemset(sata_state->clb_vaddr, 0, HBA_CLB_SIZE);
  kmemset(sata_state->fis_vaddr, 0, HBA_FIS_SIZE);
  kmemset(sata_state->cmd_table_vaddr, 0, HBA_CMD_TBL_SIZE);

#ifdef AHCI_DEBUG
  ahci_log_print("  Command list buffer virtual address: 0x%p\n",
                 sata_state->clb_vaddr);
  ahci_log_print("  Command list buffer physical address: 0x%p\n",
                 (void*)sata_state->clb_paddr);
  ahci_log_print("  FIS buffer virtual address: 0x%p\n", sata_state->fis_vaddr);
  ahci_log_print("  FIS buffer physical address: 0x%p\n",
                 (void*)sata_state->fis_paddr);
  ahci_log_print("  Command table virtual address: 0x%p\n",
                 sata_state->cmd_table_vaddr);
  ahci_log_print("  Command table physical address: 0x%p\n",
                 (void*)sata_state->cmd_table_paddr);
  ahci_log_print("  Number of command slots: %zu\n", num_slots);
#endif

  port->clb = (uint32_t)(uintptr_t)sata_state->clb_paddr;
  port->clbu = (uint32_t)((uintptr_t)sata_state->clb_paddr >> 32);
  port->fb = (uint32_t)(uintptr_t)sata_state->fis_paddr;
  port->fbu = (uint32_t)((uintptr_t)sata_state->fis_paddr >> 32);

  hba_cmd_header_t* cmd_header = sata_state->clb_vaddr;

  for (size_t i = 0; i < num_slots; i++) {
    hba_cmd_header_t* t_cmd_header = &cmd_header[i];
    t_cmd_header->prdtl = 8;  // Set the PRDT length to 8 entries
    t_cmd_header->ctba = (uint32_t)(uintptr_t)sata_state->cmd_table_paddr;
    t_cmd_header->ctbau =
        (uint32_t)((uintptr_t)sata_state->cmd_table_paddr >> 32);
  }

  start_cmd(port);

  return AHCI_SUCCESS;
}

ahci_result_t ahci_init(void) {
  ahci_log_print("Initializing AHCI controller: \n");

  pci_config_header_t* config_space = NULL;
  config_space =
      get_pci_cfg_space(AHCI_CLASS_CODE, AHCI_SUBCLASS, AHCI_PROG_IF);

  if (config_space == NULL) {
    ahci_log_error("No AHCI controller found.");
    return AHCI_ERR_NO_DEVICE;
  }

  uint32_t bar5 = config_space->bar5;

  ahci_log_print("  BAR5: 0x%08X\n", bar5);

  if (is_pci_bar_io_space(bar5)) {
    ahci_log_error("BAR5 is I/O space.\n");
    return AHCI_ERR_BAR;
  }

  if (is_pci_bar_64bit(bar5)) {
    ahci_log_error("BAR5 is 64-bit.\n");
    return AHCI_ERR_BAR;
  }

  void* bar5_addr = (void*)(uintptr_t)get_pci_bar_address(bar5);
  config_space->bar5 = 0xFFFFFFFF;

  uint32_t bar5_size = config_space->bar5;
  config_space->bar5 = bar5;

  bar5_size &= 0xFFFFFFF0;
  bar5_size = ~bar5_size + 1;  // Two's complement to get size

  ahci_log_print("  BAR5 size: 0x%08X\n", bar5_size);

  pci_mem_space_enable(config_space);
  pci_bus_master_enable(config_space);

  uintptr_t hba_ptr = 0;
  mm_result_t result = mm_map_io_address(&hba_ptr, bar5_addr);

  if (result != MM_SUCCESS) {
    ahci_log_error("Failed to map BAR5, result: %d", result);
    return AHCI_ERR_MEMORY_MAPPING;
  }

  ahci_log_print("  BAR5 mapped to virtual address: 0x%p\n", (void*)hba_ptr);
  ahci_log_print("  HBA memory address: 0x%p\n", (void*)hba_ptr);
  set_pci_msi(config_space, 40, 0xFEE00000);

  hba_mem_t* hba_mem = (hba_mem_t*)hba_ptr;

  // Reset the AHCI controller
  ahci_host_reset(hba_mem);
  ahci_log_print("  Controller reset completed.\n");

  ahci_enable_interrupts(hba_mem);
  ahci_log_print("  Interrupts enabled.\n");

  ahci_enable(hba_mem);
  ahci_log_print("  Controller enabled.\n");

  size_t sata_port_index = get_device_port(hba_mem, AHCI_DEV_SATA);

  if (sata_port_index != SIZE_MAX) {
    ahci_log_print("  SATA device found at port %zu\n", sata_port_index);
  } else {
    ahci_log_error("No SATA device found.\n");
    return AHCI_ERR_SATA_NOT_FOUND;
  }

  ahci_state.hba_mem = hba_mem;
  ahci_state.sata_port_index = sata_port_index;

  ahci_log_print("  Initialization completed.\n");

  const size_t num_slots = num_command_slots(hba_mem);
  ahci_result_t sata_init_result = sata_init(sata_port_index, num_slots);

  if (sata_init_result != AHCI_SUCCESS) {
    ahci_log_error("Failed to initialize SATA port, result: %d",
                   sata_init_result);
    return sata_init_result;
  }

  return AHCI_SUCCESS;
}

static inline bool _ahci_read_disk(size_t sata_port_index, uint64_t start_lba,
                                   uint32_t sector_count, void* buffer) {
  hba_port_t* sata_port = &ahci_state.hba_mem->ports[sata_port_index];
  ahci_sata_state_t* sata_state = &ahci_state.sata_state[sata_port_index];

  size_t slot = 0;
  sata_port->interrupt_status = (uint32_t)-1;

  hba_cmd_header_t* cmd_header_ptr = sata_state->clb_vaddr;
  hba_cmd_header_t* first_cmd_header = &cmd_header_ptr[slot];

  first_cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
  first_cmd_header->w = 0;      // Read operation
  first_cmd_header->prdtl = 1;  // One PRDT entry

  hba_cmd_tbl_t* cmd_table_ptr = (hba_cmd_tbl_t*)sata_state->cmd_table_vaddr;

  cmd_table_ptr->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
  cmd_table_ptr->prdt_entry[0].dbau = (uint32_t)((uintptr_t)buffer >> 32);

  // each sector is 512 bytes, so the total bytes to read is sector_count *
  // 512
  size_t bytes_to_read = sector_count * AHCI_SECTOR_SIZE;
  cmd_table_ptr->prdt_entry[0].dbc = bytes_to_read - 1;
  cmd_table_ptr->prdt_entry[0].i = 1;  // Interrupt on completion

  fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)(&cmd_table_ptr->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;  // FIS_TYPE_REG_H2D
  cmdfis->c = 1;                        // Command
  cmdfis->command = ATA_CMD_READ_DMA_EX;

  cmdfis->lba0 = start_lba & 0xFF;          // LBA low register, 7:0
  cmdfis->lba1 = (start_lba >> 8) & 0xFF;   // LBA mid register, 15:8
  cmdfis->lba2 = (start_lba >> 16) & 0xFF;  // LBA high register, 23:16
  cmdfis->lba3 = (start_lba >> 24) & 0xFF;  // LBA register, 31:24
  cmdfis->lba4 = (start_lba >> 32) & 0xFF;  // LBA register, 39:32
  cmdfis->lba5 = (start_lba >> 40) & 0xFF;  // LBA register, 47:40

  cmdfis->device = 1 << 6;  // LBA mode

  cmdfis->countl = sector_count & 0xFF;         // Count register, 7:0
  cmdfis->counth = (sector_count >> 8) & 0xFF;  // Count register, 15:8

  ahci_log_debug("Issuing read command to SATA device...\n");

  // The below loop waits until the port is no longer busy before issuing a
  // new command
  size_t spin = 0;
  while ((sata_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }

  if (spin == 1000000) {
    ahci_log_error("Port is hung\n");
    return false;
  }

  ahci_log_debug("Port is ready, issuing command...\n");

  sata_port->interrupt_enable = (1 << slot);  // Enable interrupt on completion
  sata_port->command_issue = (1 << slot);     // Issue command
  sata_port->command_issue = (1 << slot);     // Issue command

  ahci_log_debug("Command issued, waiting for completion...\n");

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((sata_port->command_issue & (1 << slot)) == 0) {
      break;
    }

    if (sata_port->interrupt_status & HBA_PxIS_TFES) {
      ahci_log_error("Read disk error\n");
      return false;
    }
  }

  ahci_log_debug("Read command completed successfully.\n");

  // Check again
  if (sata_port->interrupt_status & HBA_PxIS_TFES) {
    ahci_log_error("Read disk error\n");
    return false;
  }

  return true;
}

static inline bool _ahci_write_disk(size_t sata_port_index, uint64_t start_lba,
                                    uint32_t sector_count, void* buffer) {
  hba_port_t* sata_port = &ahci_state.hba_mem->ports[sata_port_index];
  ahci_sata_state_t* sata_state = &ahci_state.sata_state[sata_port_index];

  size_t slot = 0;
  sata_port->interrupt_status = (uint32_t)-1;

  hba_cmd_header_t* cmd_header_ptr = sata_state->clb_vaddr;
  hba_cmd_header_t* first_cmd_header = &cmd_header_ptr[slot];

  first_cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
  first_cmd_header->w = 1;      // Write operation
  first_cmd_header->prdtl = 1;  // One PRDT entry

  hba_cmd_tbl_t* cmd_table_ptr = (hba_cmd_tbl_t*)sata_state->cmd_table_vaddr;

  cmd_table_ptr->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
  cmd_table_ptr->prdt_entry[0].dbau = (uint32_t)((uintptr_t)buffer >> 32);

  // each sector is 512 bytes, so the total bytes to read is sector_count *
  // 512
  size_t bytes_to_read = sector_count * AHCI_SECTOR_SIZE;
  cmd_table_ptr->prdt_entry[0].dbc = bytes_to_read - 1;
  cmd_table_ptr->prdt_entry[0].i = 1;  // Interrupt on completion

  fis_reg_h2d_t* cmdfis = (fis_reg_h2d_t*)(&cmd_table_ptr->cfis);

  cmdfis->fis_type = FIS_TYPE_REG_H2D;  // FIS_TYPE_REG_H2D
  cmdfis->c = 1;                        // Command
  cmdfis->command = ATA_CMD_WRITE_DMA_EX;

  cmdfis->lba0 = start_lba & 0xFF;          // LBA low register, 7:0
  cmdfis->lba1 = (start_lba >> 8) & 0xFF;   // LBA mid register, 15:8
  cmdfis->lba2 = (start_lba >> 16) & 0xFF;  // LBA high register, 23:16
  cmdfis->lba3 = (start_lba >> 24) & 0xFF;  // LBA register, 31:24
  cmdfis->lba4 = (start_lba >> 32) & 0xFF;  // LBA register, 39:32
  cmdfis->lba5 = (start_lba >> 40) & 0xFF;  // LBA register, 47:40

  cmdfis->device = 1 << 6;  // LBA mode

  cmdfis->countl = sector_count & 0xFF;         // Count register, 7:0
  cmdfis->counth = (sector_count >> 8) & 0xFF;  // Count register, 15:8

  ahci_log_debug("Issuing write command to SATA device...\n");

  // The below loop waits until the port is no longer busy before issuing a
  // new command
  size_t spin = 0;
  while ((sata_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }

  if (spin == 1000000) {
    ahci_log_error("Port is hung\n");
    return false;
  }

  ahci_log_debug("Port is ready, issuing command...\n");

  sata_port->interrupt_enable = (1 << slot);  // Enable interrupt on completion
  sata_port->command_issue = (1 << slot);     // Issue command
  sata_port->command_issue = (1 << slot);     // Issue command

  ahci_log_debug("Command issued, waiting for completion...\n");

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((sata_port->command_issue & (1 << slot)) == 0) {
      break;
    }

    if (sata_port->interrupt_status & HBA_PxIS_TFES) {
      ahci_log_error("Write disk error\n");
      return false;
    }
  }

  ahci_log_debug("Write command completed successfully.\n");

  // Check again
  if (sata_port->interrupt_status & HBA_PxIS_TFES) {
    ahci_log_error("Write disk error\n");
    return false;
  }

  return true;  // Placeholder for future implementation
}

bool ahci_read_disk(uint64_t start_lba, uint32_t sector_count, void* buffer) {
  const size_t index = ahci_state.sata_port_index;
  const bool result = _ahci_read_disk(index, start_lba, sector_count, buffer);
  return result;
}

bool ahci_write_disk(uint64_t start_lba, uint32_t sector_count, void* buffer) {
  const size_t index = ahci_state.sata_port_index;
  const bool result = _ahci_write_disk(index, start_lba, sector_count, buffer);
  return result;
}
