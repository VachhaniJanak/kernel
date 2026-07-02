#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MADT_PROCESSOR_LOCAL_APIC 0
#define MADT_I_O_APIC 1
#define MADT_I_O_APIC_INTERRUPT_SOURCE_OVERRIDE 2
#define MADT_I_O_APIC_NON_MASKABLE_INTERRUPT_SOURCE 3
#define MADT_LOCAL_APIC_NON_MASKABLE_INTERRUPT 4
#define MADT_LOCAL_APIC_ADDRESS_OVERRIDE 5
#define MADT_PROCESSOR_LOCAL_X2APIC 9

struct __attribute__((packed)) rsdpDescriptor_s {
  char signature[8];
  uint8_t checksum;
  char oemid[6];
  uint8_t revision;
  uint32_t rsdtAddr;

  uint32_t length;
  uint64_t xsdtAddr;
  uint8_t extChecksum;
  uint8_t reserved[3];
};

struct __attribute__((packed)) acpiSdtHeader_s {
  char signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  char oemid[6];
  char oem_tableid[8];
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_revision;
};

struct __attribute__((packed)) rsdt_s {
  struct acpiSdtHeader_s sdtHeader;  // signature "RSDT"
  uint32_t sdtAddresses[];
};

struct __attribute__((packed)) xsdt_s {
  struct acpiSdtHeader_s sdtHeader;  // signature "XSDT"
  uint64_t sdtAddresses[];
};

struct __attribute__((packed)) madt_s {
  struct acpiSdtHeader_s sdtHeader;
  uint32_t localApicAddr;
  uint32_t flags;
  void* entries[];
};

typedef struct __attribute__((packed)) madtEntryHeader_s {
  uint8_t type;
  uint8_t length;
} madtEntryHeader_t;

// Processor Local APIC
struct __attribute__((packed)) madtEntryType0_s {
  struct madtEntryHeader_s header;
  uint8_t acpiProcessorId;
  uint8_t apciId;
  uint32_t flags;
};

// I/O APIC
struct __attribute__((packed)) madtEntryType1_s {
  struct madtEntryHeader_s header;
  uint8_t ioApciId;
  uint8_t reserved;
  uint32_t ioApciAddr;
  uint32_t globalSystemInterruptBase;
};

// I/O APIC Interrupt Source Override
struct __attribute__((packed)) madtEntryType2_s {
  struct madtEntryHeader_s header;
  uint8_t busSource;
  uint8_t irqSource;
  uint32_t globalSystemInterrupt;
  uint16_t flags;
};

// I/O APIC Non-maskable interrupt source
struct __attribute__((packed)) madtEntryType3_s {
  struct madtEntryHeader_s header;
  uint8_t nmiSource;
  uint8_t reserved;
  uint16_t flags;
  uint32_t globalSystemInterrupt;
};

// Local APIC Non-maskable interrupts
struct __attribute__((packed)) madtEntryType4_s {
  struct madtEntryHeader_s header;
  uint8_t acpiProcessorId;
  uint16_t flags;
  uint8_t localApicLint;
};

// Local APIC Address Override
struct __attribute__((packed)) madtEntryType5_s {
  struct madtEntryHeader_s header;
  uint16_t reserved;
  uint64_t localApicAddr;
};

// Processor Local x2APIC
struct __attribute__((packed)) madtEntryType9_s {
  struct madtEntryHeader_s header;
  uint16_t reserved;
  uint32_t x2apicId;
  uint32_t flags;
  uint32_t acpiProcessorUid;
};

struct __attribute__((packed)) fadt_s {
  struct acpiSdtHeader_s sdtHeader;
};

struct __attribute__((packed)) hpet_s {
  struct acpiSdtHeader_s sdtHeader;
  uint32_t eventTimerBlockId;
  uint32_t reserved;
  uint64_t address;
  uint8_t id;
  uint16_t minTicks;
  uint8_t pageProtection;
};

struct __attribute__((packed)) mcfg_s {
  struct acpiSdtHeader_s sdtHeader;
};

struct __attribute__((packed)) waet_s {
  struct acpiSdtHeader_s sdtHeader;
};

struct __attribute__((packed)) dsdt_s {
  struct acpiSdtHeader_s sdtHeader;
};

struct __attribute__((packed)) bgrt_s {
  struct acpiSdtHeader_s sdtHeader;
};

struct __attribute__((packed)) acpiState_s {
  struct fadt_s* fadt;
  struct madt_s* madt;
  struct hpet_s* hpet;
  struct mcfg_s* mcfg;
  struct waet_s* waet;
  struct dsdt_s* dsdt;
  struct bgrt_s* bgrt;

  void* (*physToVirt)(void*);
};

bool initACPI(void* rsdpAddr, void* (*physToVirt)(void*));

void* getLocalApicAddr(void);

uint32_t getMADTFlags(void);

size_t getMADTEntryCount(size_t type);

void* getMADTApicEntry(size_t type);

struct hpet_s* getHpet(void);
