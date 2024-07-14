### 这是什么

是一个用来修复因 Mach-O 格式限制导致大于 4GB 就会损坏无法正常读取的 DWARF 符号文件的程序。

### 为什么符号文件会损坏

Mach-O 文件的 section 有如下格式：
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

注意到`offset`属性是`uint32_t`格式的，因此当偏移大于 4GB 时就会溢出，变成了一个错误的文件偏移。

例如：
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

显然在`__debug_info`节之后所有偏移都被截断了，`__debug_ranges`的偏移应为 0x210C7226B，而非 0x10C7226B。

### 修复原理

一般来说符号文件内最大的节是`__debug_info`，而其余部分加起来也不会超过 4GB，因此将它放到最后就可以避免出现偏移溢出的问题。

真实的偏移可以通过累加的方式获得，`addr`字段里也记录了映射地址，可以解析出正确的偏移。

调整后的文件布局：
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

### 受影响的程序

目前测试来看 lldb、llvm-dwarfdump、libdwarf 都受到影响无法读取大于 4GB 的符号文件。

也许要提交补丁用`addr`代替`offset`。
