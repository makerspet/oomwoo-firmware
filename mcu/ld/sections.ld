/* Section layout shared by both MCUs. The memory regions come from the
 * chip script that INCLUDEs this file. Symbols match the WCH startup files:
 * _data_lma/_data_vma/_edata, _sbss/_ebss, _eusrstack, __global_pointer$. */
ENTRY(_start)

SECTIONS
{
  .init : {
    _sinit = .;
    KEEP(*(SORT_NONE(.init)))
    . = ALIGN(4);
  } >FLASH

  .vector : {
    *(.vector)
    . = ALIGN(64);
  } >FLASH

  .text : {
    . = ALIGN(4);
    *(.text .text.*)
    *(.rodata .rodata.*)
    *(.srodata .srodata.*)
    . = ALIGN(4);
  } >FLASH

  .init_array : {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  /* Code the CH32V003 startup copies into RAM (empty unless something is
   * placed in .highcode). */
  .highcode : ALIGN(4) {
    _highcode_vma_start = .;
    *(.highcode .highcode.*)
    . = ALIGN(4);
    _highcode_vma_end = .;
  } >RAM AT>FLASH
  _highcode_lma = LOADADDR(.highcode);

  _data_lma = LOADADDR(.data);

  .data : ALIGN(4) {
    _data_vma = .;
    *(.data .data.*)
    . = ALIGN(8);
    PROVIDE(__global_pointer$ = . + 0x800);
    *(.sdata .sdata.*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH

  .bss (NOLOAD) : ALIGN(4) {
    _sbss = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  PROVIDE(_end = _ebss);
  PROVIDE(end = _ebss);

  .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size (NOLOAD) : {
    . = ALIGN(4);
    _susrstack = .;
    . = . + __stack_size;
    _eusrstack = .;
  } >RAM

  ASSERT(_ebss <= _susrstack, "RAM overflow: .data + .bss collide with the stack")

  /DISCARD/ : { *(.eh_frame*) }
}
