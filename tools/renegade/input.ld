SECTIONS
{
  . = 0xfffffe0007581284;
  .text : {
    *(.text.marble_input_entry)
    *(.text*)
  }
  .rodata : { *(.rodata*) }
  /DISCARD/ : { *(.data*) *(.bss*) *(.eh_frame*) *(.comment*) }
}
