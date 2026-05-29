ENTRY(_start)

SECTIONS {
  . = 4M;
  _kernel_phy_st = .;

  .startuptext : {
    KEEP(*(.multiboot))
    *(.boot.text)
  }                                     

  .startuprodata : {
    *(.boot.rodata)
  }

  .startupdata : {
    *(.boot.data)
  }

  .startupbss : {
    *(.boot.bss)
  }
  
  . = ALIGN(0x1000);
  _startup_ed = .;

  . += 0xFFFFFFFF80000000;
  _kernel_vir_st = .;

  .text : AT(_startup_ed) {
    *(.text)
  }
  . = ALIGN(0x1000);
  
  .rodata : AT((LOADADDR(.text) + SIZEOF(.text) + 0xFFF) & 0xFFFFFFFFFFFFF000){
    *(.rodata)
  }
  . = ALIGN(0x1000);

  .data : AT((LOADADDR(.rodata) + SIZEOF(.rodata) + 0xFFF) & 0xFFFFFFFFFFFFF000){
    *(.data)
  }
  . = ALIGN(0x1000);

  .bss : AT((LOADADDR(.data) + SIZEOF(.data) + 0xFFF) & 0xFFFFFFFFFFFFF000){
    *(COMMON)
    *(.bss)
  }
  . = ALIGN(0x1000);

  _kernel_vir_ed = .;
  _kernel_phy_ed = (LOADADDR (.bss) + SIZEOF (.bss) + 0xFFF) & 0xFFFFFFFFFFFFF000;
}
