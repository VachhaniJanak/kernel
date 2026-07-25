#include <arch/x86_64/apic.h>
#include <drivers/ahci/ahci.h>
#include <drivers/pcie/pcie.h>
#include <mm/mm.h>
#include <mm/vmm/kheap.h>
#include <stddef.h>
#include <stdint.h>
#include <utils/log.h>
#include <utils/utils.h>

#define AHCI_CLASS_CODE 0x01
#define AHCI_SUBCLASS 0x06
#define AHCI_PROG_IF 0x01

#define HBA_CLB_SIZE 1024
#define HBA_FIS_SIZE 256

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

static void* clb_vaddr = NULL;
static void* clb_paddr = NULL;
static void* fis_vaddr = NULL;
static void* fis_paddr = NULL;
static void* cmd_table_vaddr = NULL;
static void* cmd_table_paddr = NULL;
static hba_port_t* sata_port = NULL;

volatile bool is_transfer_complete = false;

void ahci_irq_isr_handler(void) {
  is_transfer_complete = true;
  sata_port->interrupt_status = (uint32_t)-1;  // Clear interrupt status
  lapic_eoi();
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

void sata_init(hba_port_t* port, size_t num_slots) {
  stop_cmd(port);

  clb_vaddr = kmalloc(HBA_CLB_SIZE);
  clb_paddr = kmalloc_phys_addr(clb_vaddr);
  kmemset(clb_vaddr, 0, HBA_CLB_SIZE);

  fis_vaddr = kmalloc(HBA_FIS_SIZE);
  fis_paddr = kmalloc_phys_addr(fis_vaddr);
  kmemset(fis_vaddr, 0, HBA_FIS_SIZE);

  cmd_table_vaddr = kmalloc(256);
  cmd_table_paddr = kmalloc_phys_addr(cmd_table_vaddr);
  kmemset(cmd_table_vaddr, 0, 256);

  port->clb = (uint32_t)(uintptr_t)clb_paddr;
  port->clbu = (uint32_t)((uintptr_t)clb_paddr >> 32);
  port->fb = (uint32_t)(uintptr_t)fis_paddr;
  port->fbu = (uint32_t)((uintptr_t)fis_paddr >> 32);

  hba_cmd_header_t* cmd_header = clb_vaddr;

  for (size_t i = 0; i < num_slots; i++) {
    hba_cmd_header_t* t_cmd_header = &cmd_header[i];
    t_cmd_header->prdtl = 8;  // Set the PRDT length to 8 entries
    t_cmd_header->ctba = (uint32_t)(uintptr_t)cmd_table_paddr;
    t_cmd_header->ctbau = (uint32_t)((uintptr_t)cmd_table_paddr >> 32);
  }

  start_cmd(port);
}

void init_ahci(void) {
  pci_config_header_t* config_space = NULL;
  config_space =
      get_pci_cfg_space(AHCI_CLASS_CODE, AHCI_SUBCLASS, AHCI_PROG_IF);

  if (config_space == NULL) {
    LOG_ERROR("No AHCI controller found.");
    return;
  }

  uint32_t bar5 = config_space->bar5;

  if (is_pci_bar_io_space(bar5)) {
    LOG_ERROR("BAR5 is I/O space.\n");
    return;
  }

  if (is_pci_bar_64bit(bar5)) {
    LOG_ERROR("BAR5 is 64-bit.\n");
    return;
  }

  void* bar5_addr = (void*)(uintptr_t)get_pci_bar_address(bar5);

  config_space->bar5 = 0xFFFFFFFF;
  uint32_t bar5_size = config_space->bar5;
  config_space->bar5 = bar5;

  bar5_size &= 0xFFFFFFF0;
  bar5_size = ~bar5_size + 1;  // Two's complement to get size

#ifdef AHCI_DEBUG
  LOG_DEBUG("BAR5 Address: 0x%p\n", bar5_addr);
  LOG_DEBUG("BAR5 Size: 0x%08X\n", bar5_size);
#endif

  pci_mem_space_enable(config_space);
  pci_bus_master_enable(config_space);

  void* hba_ptr = phys_to_virt(bar5_addr);

  // map one 4k page for the AHCI BAR5
  if (!mmap(hba_ptr, bar5_addr)) {
    LOG_ERROR("Failed to map AHCI BAR5.");
    return;
  }

  set_pci_msi(config_space, 40, 0xFEE00000);
  hba_mem_t* hba_mem = (hba_mem_t*)hba_ptr;

  // Reset the AHCI controller
  ahci_host_reset(hba_mem);
#ifdef AHCI_DEBUG
  LOG_DEBUG("AHCI controller reset completed.\n");
#endif
  ahci_enable_interrupts(hba_mem);
#ifdef AHCI_DEBUG
  LOG_DEBUG("AHCI interrupts enabled.\n");
#endif
  ahci_enable(hba_mem);
#ifdef AHCI_DEBUG
  LOG_DEBUG("AHCI controller enabled.\n");
#endif

  size_t sata_port_index = get_device_port(hba_mem, AHCI_DEV_SATA);

  if (sata_port_index != SIZE_MAX) {
#ifdef AHCI_DEBUG
    LOG_DEBUG("SATA device found at port %zu\n", sata_port_index);
#endif
  } else {
    LOG_ERROR("No SATA device found.\n");
    return;
  }

  sata_port = &hba_mem->ports[sata_port_index];

#ifdef AHCI_DEBUG
  LOG_DEBUG("AHCI initialization completed.\n");
#endif
  sata_init(sata_port, num_command_slots(hba_mem));
}

bool ahci_read_disk(uint64_t start_lba, uint32_t sector_count, void* buffer) {
  size_t slot = 0;
  sata_port->interrupt_status = (uint32_t)-1;

  hba_cmd_header_t* cmd_header_ptr = clb_vaddr;
  hba_cmd_header_t* first_cmd_header = &cmd_header_ptr[slot];

  first_cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
  first_cmd_header->w = 0;      // Read operation
  first_cmd_header->prdtl = 1;  // One PRDT entry

  hba_cmd_tbl_t* cmd_table_ptr = (hba_cmd_tbl_t*)cmd_table_vaddr;

  cmd_table_ptr->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
  cmd_table_ptr->prdt_entry[0].dbau = (uint32_t)((uintptr_t)buffer >> 32);

  // each sector is 512 bytes, so the total bytes to read is sector_count * 512
  size_t bytes_to_read = sector_count * 512;
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

#ifdef AHCI_DEBUG
  LOG_DEBUG("Issuing read command to SATA device...\n");
#endif

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  size_t spin = 0;
  while ((sata_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }

  if (spin == 1000000) {
    LOG_ERROR("Port is hung\n");
    return false;
  }

#ifdef AHCI_DEBUG
  LOG_DEBUG("Port is ready, issuing command...\n");
#endif

  sata_port->interrupt_enable = (1 << slot);  // Enable interrupt on completion
  sata_port->command_issue = (1 << slot);     // Issue command
  sata_port->command_issue = (1 << slot);     // Issue command

#ifdef AHCI_DEBUG
  LOG_DEBUG("Command issued, waiting for completion...\n");
#endif

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((sata_port->command_issue & (1 << slot)) == 0) {
      break;
    }

    if (sata_port->interrupt_status & HBA_PxIS_TFES) {
      LOG_ERROR("Read disk error\n");
      return false;
    }
  }

#ifdef AHCI_DEBUG
  LOG_DEBUG("Read command completed successfully.\n");
#endif

  // Check again
  if (sata_port->interrupt_status & HBA_PxIS_TFES) {
    LOG_ERROR("Read disk error\n");
    return false;
  }

  return true;  
}

bool ahci_write_disk(uint64_t start_lba, uint32_t sector_count, void* buffer) {
  size_t slot = 0;
  sata_port->interrupt_status = (uint32_t)-1;

  hba_cmd_header_t* cmd_header_ptr = clb_vaddr;
  hba_cmd_header_t* first_cmd_header = &cmd_header_ptr[slot];

  first_cmd_header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
  first_cmd_header->w = 1;      // Write operation
  first_cmd_header->prdtl = 1;  // One PRDT entry

  hba_cmd_tbl_t* cmd_table_ptr = (hba_cmd_tbl_t*)cmd_table_vaddr;

  cmd_table_ptr->prdt_entry[0].dba = (uint32_t)(uintptr_t)buffer;
  cmd_table_ptr->prdt_entry[0].dbau = (uint32_t)((uintptr_t)buffer >> 32);

  // each sector is 512 bytes, so the total bytes to read is sector_count * 512
  size_t bytes_to_read = sector_count * 512;
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

#ifdef AHCI_DEBUG
  LOG_DEBUG("Issuing write command to SATA device...\n");
#endif

  // The below loop waits until the port is no longer busy before issuing a new
  // command
  size_t spin = 0;
  while ((sata_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
    spin++;
  }

  if (spin == 1000000) {
    LOG_ERROR("Port is hung\n");
    return false;
  }

#ifdef AHCI_DEBUG
  LOG_DEBUG("Port is ready, issuing command...\n");
#endif

  sata_port->interrupt_enable = (1 << slot);  // Enable interrupt on completion
  sata_port->command_issue = (1 << slot);     // Issue command
  sata_port->command_issue = (1 << slot);     // Issue command

#ifdef AHCI_DEBUG
  LOG_DEBUG("Command issued, waiting for completion...\n");
#endif

  // Wait for completion
  while (1) {
    // In some longer duration reads, it may be helpful to spin on the DPS bit
    // in the PxIS port field as well (1 << 5)
    if ((sata_port->command_issue & (1 << slot)) == 0) {
      break;
    }

    if (sata_port->interrupt_status & HBA_PxIS_TFES) {
      LOG_ERROR("Write disk error\n");
      return false;
    }
  }

#ifdef AHCI_DEBUG
  LOG_DEBUG("Write command completed successfully.\n");
#endif

  // Check again
  if (sata_port->interrupt_status & HBA_PxIS_TFES) {
    LOG_ERROR("Write disk error\n");
    return false;
  }

  return true;  // Placeholder for future implementation
}