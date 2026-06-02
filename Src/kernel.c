#include "utils/printf.h"
#include <boot/boot.h>
#include <kernel.h>

#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/interrupt.h>
#include <arch/x86_64/tss.h>

#include <drivers/screen/screen.h>
#include <drivers/serial/serial.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <utils/log.h>
#include <utils/utils.h>

// Halt and catch fire function.
static void hcf(void) {
  for (;;)
    asm("hlt");
}

void printMemoryMap(void) {

  struct MemoryMapEntry_s entries[getMMapEntryCount()];

  if (!copyMMapEntry(entries)) {
    serial_printf("Failed to copy memory map entries.\n");
    return;
  }

  for (size_t i = 0; i < getMMapEntryCount(); i++) {
    uint64_t base = entries[i].base;
    uint64_t length = entries[i].length;
    uint64_t type = entries[i].type;

    switch (type) {
    case LIMINE_MEMMAP_USABLE:
      serial_printf(MARK_AS_BOLD("Usable                 : "));
      break;
    case LIMINE_MEMMAP_RESERVED:
      serial_printf(MARK_AS_BOLD("Reserved               : "));
      break;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
      serial_printf(MARK_AS_BOLD("ACPI reclaimable       : "));
      break;
    case LIMINE_MEMMAP_ACPI_NVS:
      serial_printf(MARK_AS_BOLD("ACPI NVS               : "));
      break;
    case LIMINE_MEMMAP_BAD_MEMORY:
      serial_printf(MARK_AS_BOLD("Bad                    : "));
      break;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
      serial_printf(MARK_AS_BOLD("Bootloader reclaimable : "));
      break;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
      serial_printf(MARK_AS_BOLD("Executable and modules : "));
      break;
    case LIMINE_MEMMAP_FRAMEBUFFER:
      serial_printf(MARK_AS_BOLD("Framebuffer            : "));
      break;
    case LIMINE_MEMMAP_RESERVED_MAPPED:
      serial_printf(MARK_AS_BOLD("Reserved mapped        : "));
      break;
    default:
      serial_printf(MARK_AS_BOLD("Unknown  type          : "));
      break;
    }

    serial_printf("Base: 0x%016lx, Length: %lu KB\r\n", base, length / (1024));
  }
}

void kmain(void) {
  serial_init();

  if (!isBootOk()) {
    LOG_ERROR("Boot failed");
    hcf();
  }

  DISABLE_INT;

  gdt_init();
  tss_init();
  init_idt();

  ENABLE_INT;

  // printMemoryMap();

  if (!init_screen()) {
    LOG_ERROR("Framebuffer initialization faild!");
    hcf();
  }

  clear_screen(0x00000000);

  kprintf(
      "We study the capabilities of speech processing systems trained simply "
      "to predict large amounts of transcripts of audio on the internet. When "
      "scaled to 680,000 hours of multilingual and multitask supervision, the "
      "resulting models generalize well to standard benchmarks and are often "
      "competitive with prior fully supervised results but in a zero-shot "
      "transfer setting without the need for any fine-tuning. When compared to "
      "humans, the models approach their accuracy and robustness. We are "
      "releasing models and inference code to serve as a foundation for "
      "further work on robust speech processing.\n\n");

  kprintf(
      "Progress in speech recognition has been energized by the development of "
      "unsupervised pre-training techniques exem-plified by Wav2Vec 2.0 "
      "(Baevski et al., 2020). Since these methods learn directly from raw "
      "audio without the need for human labels, they can productively use "
      "large datasets of un-labeled speech and have been quickly scaled up to "
      "1,000,000 hours of training data (Zhang et al., 2021), far more than "
      "the 1,000 or so hours typical of an academic supervised dataset.When "
      "fine-tuned on standard benchmarks, this approachhas improved the state "
      "of the art, especially in a low-data setting.\n\n");

  kprintf(
      "These pre-trained audio encoders learn high-quality repre-sentations of "
      "speech, but because they are purely unsuper-vised they lack an "
      "equivalently performant decoder mapping those representations to usable "
      "outputs, necessitating a fine-tuning stage in order to actually perform "
      "a task such as speech recognition1 . This unfortunately limits their "
      "use-fulness and impact as fine-tuning can still be a complex process "
      "requiring a skilled practitioner. There is an addi-tional risk with "
      "requiring fine-tuning. Machine learning methods are exceedingly adept "
      "at finding patterns within a training dataset which boost performance "
      "on held-out data from the same dataset. However, some of these patterns "
      "are brittle and spurious and don’t generalize to other datasets and "
      "distributions. In a particularly disturbing example, Rad-ford et al. "
      "(2021) documented a 9.2% increase in object classification accuracy "
      "when fine-tuning a computer vision model on the ImageNet dataset "
      "(Russakovsky et al., 2015) without observing any improvement in average "
      "accuracy when classifying the same objects on seven other natural image "
      "datasets. A model that achieves “superhuman” per- formance when trained "
      "on a dataset can still make many basic errors when evaluated on "
      "another, possibly precisely because it is exploiting those "
      "dataset-specific quirks that humans are oblivious to (Geirhos et al., "
      "2020).");

  size_t count = 0;

  while (1) {
    kprintf("Hello, from frame buffer screen and count: %lu\n", count);
    //   clr_screen(0x00ff0000);
    //   for (volatile int i = 0; i < 100000000; i++) {
    //   }
    //   clr_screen(0x0000ff00);
    //   for (volatile int i = 0; i < 100000000; i++) {
    //   }
    //   clr_screen(0x000000ff);
    // for (volatile int i = 0; i < 100000000; i++) {
    // }
    count++;
  }

  while (1)
    ;
}
