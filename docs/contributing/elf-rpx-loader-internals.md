# ELF/RPX Loader Internals

This document explains how the binary loader works, what the RPX format is, and how to extend the loader for Wii U-specific metadata.

---

## ELF and RPX: what's the difference?

Standard ELF (Executable and Linkable Format) is used on Linux and many embedded platforms. The Wii U uses **RPX** (Relocatable PowerPC eXecutable), which is a custom extension of ELF 32-bit.

The key differences:

| Field | Standard ELF executable | RPX |
|---|---|---|
| `e_type` | `0x0002` (ET_EXEC) | `0xFE01` |
| `e_machine` | varies | `0x0014` (PowerPC) |
| Compressed sections | no | yes — `SHF_DEFLATED` flag |
| Custom sections | no | `.rpl_fileinfo`, `.rpl_imports`, `.rpl_exports`, `.rpl_crcs` |

An RPL (Relocatable PowerPC Library) — the Wii U equivalent of a `.so` — uses `e_type = 0xFE02`.

The loader currently handles the ELF structure common to both formats. RPX-specific sections (`.rpl_fileinfo`, `.rpl_imports`, etc.) are parsed but their content is not yet interpreted.

---

## `BeDecoder` — the big-endian reader

All parsing goes through `Utils::BeDecoder` (`core/src/utils/BeDecoder.hpp`). Its role is to act as a cursor over a byte buffer, handling big-endian byte swapping transparently.

PowerPC is big-endian. The host machine (x86-64) is little-endian. Every multi-byte value read from the file must be byte-swapped before use.

### Construction

```cpp
// From a file path — reads the entire file into memory at once
Utils::BeDecoder decoder("path/to/binary.rpx");

// From an in-memory buffer — used when re-parsing a section's raw data
Utils::BeDecoder decoder(section.data);
```

### Reading methods

```cpp
// Read T bytes at the current offset, advance the cursor, NO byte swap.
// Use for: e_ident (raw bytes), bulk data copies.
T value = decoder.extract<T>();

// Read T bytes, advance the cursor, SWAP bytes (big-endian → little-endian).
// Use for: every numeric field in ELF headers (e_type, sh_size, st_value, …).
T value = decoder.extractSwap<T>();

// Bulk copy: copy `count` bytes into an existing buffer. No swap.
decoder.extract(buffer_ptr, count);

// Pointer variant: return a pointer directly into the internal buffer.
// Advances the cursor by sizeof(Type) * count. Zero-copy.
// Use for: passing compressed data directly to zlib.
const Bytef* ptr = decoder.extract<Bytef>(section.header.sh_size);

// Jump to an absolute byte offset. Does not advance automatically.
decoder.seek(offset);
```

### Internal layout

```
m_buffer  — vector<char>     owns the raw file bytes
m_view    — string_view      non-owning view over m_buffer (used for pointer-based reads)
m_offset  — size_t           current read position
```

The entire file is loaded into memory on construction. This is intentional: the ELF section table is not contiguous — parsing requires jumping back and forth via `seek()`.

---

## The `Binary` data structure

After loading, the result is a `Core::Binary` struct (`core/src/binary/Binary.hpp`):

```
Binary
├── Elf32_Ehdr header          — the ELF file header
├── vector<Section> sections   — all sections, in order
│     Section
│     ├── string name          — resolved from the section name string table
│     ├── Elf32_Shdr header    — section header (address, offset, size, flags, …)
│     └── vector<char> data    — the section's raw (or decompressed) bytes
└── vector<Symbol> symbols     — all symbols from .symtab
      Symbol
      ├── string name          — resolved from .strtab
      └── Elf32_Sym header     — symbol entry (value, size, type, section index)
```

To find a section by name at runtime:

```cpp
Core::Section &text = binary.findSection(".text");   // throws BinaryException if not found
```

---

## Loading sequence

The `Loader` constructor runs three phases in order:

```
Loader(filepath)
    │
    ├─ 1. loadHeader()
    │       reads the 52-byte Elf32_Ehdr field by field
    │       e_ident is extracted as raw bytes (no swap)
    │       all other fields use extractSwap<T>()
    │
    ├─ 2. loadSections()
    │       for i in 0..e_shnum:
    │           seek(e_shoff + i * e_shentsize)   ← jump to section header
    │           loadSectionHeader(section)         ← fill Elf32_Shdr
    │           seek(section.header.sh_offset)    ← jump to section data
    │           if sh_flags & SHF_DEFLATED:
    │               loadAndDecompressSectionData() ← inflate with zlib
    │           else:
    │               loadSectionData()              ← memcpy raw bytes
    │       loadSectionName()                      ← resolve names from e_shstrndx
    │
    └─ 3. loadSymbols()
            find ".symtab" section
            symAmount = sh_size / sh_entsize
            loadSymbolHeader()   ← read each Elf32_Sym with a new BeDecoder on .symtab data
            loadSymbolName()     ← resolve names from .strtab (via sh_link)
```

### Why `seek()` is needed

The ELF section headers are stored in a contiguous table at `e_shoff`. But each section's data is at its own `sh_offset`, which can be anywhere in the file. The loader jumps to the header table, reads the header, then jumps to the data offset. This random-access pattern is why `BeDecoder` loads the whole file upfront.

---

## Compressed sections: `SHF_DEFLATED`

`SHF_DEFLATED` (`0x08000000`) is a Wii U-specific ELF section flag. When a section has this flag set, its on-disk layout is:

```
[ 4 bytes, big-endian uint32 ]  ← uncompressed size
[ sh_size bytes               ]  ← zlib-deflated data
```

The loader handles this in `loadAndDecompressSectionData()`:

1. Read the 4-byte uncompressed size with `extractSwap<uint32_t>()` and pre-allocate `section.data`
2. Get a pointer into the buffer for the compressed bytes with `extract<Bytef>(sh_size)` (zero-copy)
3. Inflate with `zlib::inflate()` directly into `section.data`
4. On zlib error, `section.data` is cleared (the section will appear empty)

After decompression, `section.data` contains the plain bytes — consumers don't need to know the section was compressed.

---

## Error handling

All loader errors use `Core::Exception` subclasses, which format as `[ExceptionType]: message`:

| Class | Thrown when |
|---|---|
| `BeDecoderException` | File not found or unreadable |
| `LoaderException` | (available, not yet thrown explicitly) |
| `BinaryException` | `findSection()` called with a name that doesn't exist |

These are uncaught in `main.cpp` — any loading failure terminates the process. Future work should add a top-level catch.

---

## Extending the loader for RPX metadata

The RPX format adds several sections not present in standard ELF. Here is what each contains and what parsing them would require:

| Section | Contents | Needed for |
|---|---|---|
| `.rpl_fileinfo` | SDK version, text/data sizes, file flags | Beta goal F3 (entry point detection) |
| `.rpl_imports` | List of imported RPL libraries and symbols | Resolving external function calls |
| `.rpl_exports` | Exported symbols with their hashed names | Loading RPL libraries |
| `.rpl_crcs` | CRC32 checksums per section | Integrity verification |

### Reading RPX-specific data

These sections are already loaded as raw bytes by the existing loader (they go through the normal section loading path). To parse them, create a new `BeDecoder` from the section data and read the RPX-specific structs field by field:

```cpp
const Core::Section &fileInfo = binary.findSection(".rpl_fileinfo");
auto decoder = Utils::BeDecoder(fileInfo.data);

// Example: first field is a magic/version number
uint32_t magic = decoder.extractSwap<uint32_t>();
```

The RPX struct layouts are documented in the [WiiUBrew wiki](https://wiiubrew.org/wiki/RPX/RPL_Format).

### Detecting RPX vs standard ELF

```cpp
// After loading:
bool isRpx = (binary.header.e_type == 0xFE01);
bool isRpl = (binary.header.e_type == 0xFE02);
bool isPpc = (binary.header.e_machine == 0x0014);
```

This check is already present in `core/src/main.cpp` for display purposes.
