OUTPUT_FORMAT("elf64-littleaarch64")
ENTRY(_start)
SECTIONS {
    . = 0x40200000;
    .text : { KEEP(*(.text.start)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    ASSERT(. <= 0x40210000, "s2perm code/data overlaps probe pages")
    . = 0x40210000;
    .probe_ro : { KEEP(*(.probe_ro)) }
    ASSERT(SIZEOF(.probe_ro) <= 0x1000, "RO probe exceeds page")
    . = 0x40211000;
    .probe_xn : { KEEP(*(.probe_xn)) }
    ASSERT(SIZEOF(.probe_xn) <= 0x1000, "XN probe exceeds page")
    . = 0x40212000;
    .probe_store : { KEEP(*(.probe_store)) }
    ASSERT(SIZEOF(.probe_store) <= 0x1000, "store probe exceeds page")
    ASSERT(. <= 0x403f0000, "s2perm probe overlaps reserved stack")
    .stack 0x403f0000 (NOLOAD) : { . += 0x10000; }
    ASSERT(ADDR(.stack) + SIZEOF(.stack) == 0x40400000, "stack top differs from vm reset")
}
