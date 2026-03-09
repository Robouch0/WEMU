---
name: "[ISA] Implement instruction"
about: Template for implementing a single PowerPC instruction.
title: "[ISA][<CATEGORY>] Implement <INSTRUCTION>"
labels: isa, enhancement
---

## ISA Reference
[PowerPC ISA Book I v2.01](https://math-atlas.sourceforge.net/devel/assembly/ppc_isa.pdf) — §X.X, p. XX

**Form:** <!-- XO-form / D-form / X-form / ... -->

**Syntax:**
```
<!-- copy variants from PDF -->
```

**Encoding:**
<!-- drag & drop a screenshot of the bit diagram from the PDF here -->

**Operation:**
```
<!-- copy pseudocode from PDF -->
```

**Special Registers Altered:**
```
<!-- copy from PDF -->
```

**Extended Mnemonics:**
```
<!-- copy from PDF, or delete this section if none -->
```

## Tasks
- [ ] Add entry in `core/src/cpu/tables/cpu_instructions.anh`
- [ ] Implement in `core/src/cpu/instructions/`
- [ ] Add a unit test in `core/tests/`

## Links
- Parent: #<!-- issue number (e.g. #85) -->
