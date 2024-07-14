### What's this

This is a program to repair DWARF symbol files that are corrupted and cannot be read properly if they are larger than 4GB due to Mach-O format limitation.

### Why is the symbol file corrupted

The section of a Mach-O file has the following format:
```c
struct section_64 { /* for 64-bit architectures */
	char		sectname[16];	/* name of this section */
	char		segname[16];	/* segment this section goes in */
	uint64_t	addr;		/* memory address of this section */
	uint64_t	size;		/* size in bytes of this section */
	uint32_t	offset;		/* file offset of this section */
	uint32_t	align;		/* section alignment (power of 2) */
	uint32_t	reloff;		/* file offset of relocation entries */
	uint32_t	nreloc;		/* number of relocation entries */
	uint32_t	flags;		/* flags (section type and attributes)*/
	uint32_t	reserved1;	/* reserved (for offset or index) */
	uint32_t	reserved2;	/* reserved (for count or sizeof) */
	uint32_t	reserved3;	/* reserved */
};
```

Notice that the `offset` field is in `uint32_t` format, so when the offset is greater than 4GB, it overflows into an incorrect file offset.

For instance:
```text
section          offset   size
__debug_line     09E89000 CAB15F1
__debug_loc      1693A5F1 14EB35C6
__debug_aranges  2B7EDBB7 30AF0
__debug_info     2B81E6A7 1E5453BC4  << huge section size
__debug_ranges   10C7226B 570BAE0    << offset overflow
__debug_abbrev   1637DD4B A942
__debug_str      1638868D 1BCF0CB8
__apple_names    32079345 9D7ADDC
__apple_namespac 3BDF4121 2A934
__apple_types    3ВЕ1ЕА55 85DA71E
__apple_objc     443F9173 A434
```

Apparently all offsets are truncated after the `__debug_info` section, and the offset of `__debug_ranges` should be 0x210C7226B, not 0x10C7226B.

### Repair mechanism

Generally the largest section within a symbol file is `__debug_info`, and the rest of the file doesn't add up to more than 4GB. So putting it at the end avoids the problem of offset overflow.

The real offset can be obtained by summing up. Also the `addr` field records the mapped address, which can be resolved to the correct offset.

Adjusted file layout:
```text
section          offset   size
__debug_line     09E89000 CAB15F1
__debug_loc      1693A5F1 14EB35C6
__debug_aranges  2B7EDBB7 30AF0
__debug_ranges   2B81E6A7 570BAE0
__debug_abbrev   30F2A187 A942
__debug_str      30F34AC9 1BCF0CB8
__apple_names    4CC25781 9D7ADDC
__apple_namespac 569A055D 2A934
__apple_types    569CAE91 85DA71E
__apple_objc     5EFA55AF A434
__debug_info     5EFAF9E3 1E5453BC4
```

### Affected programs

So far testing has shown that lldb, llvm-dwarfdump, and libdwarf are all affected, and not able to read symbol files larger than 4GB.

Maybe we need to submit a patch to use `addr` instead of `offset`.
