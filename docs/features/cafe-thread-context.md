# Cafe thread quantization registers

New guest threads and the main thread start with the Cafe quantization defaults:

| Register | Load/store type | Value |
| --- | --- | --- |
| GQR2 | Unsigned byte | `0x00040004` |
| GQR3 | Unsigned halfword | `0x00050005` |
| GQR4 | Signed byte | `0x00060006` |
| GQR5 | Signed halfword | `0x00070007` |

Their initial scales are zero. GQR0, GQR1, GQR6 and GQR7 start at zero. A new thread
receives these entry defaults independently of changes made by its creator.
Subsequent changes remain part of that thread's saved CPU context.

Zeroing every GQR incorrectly selects float loads/stores where runtime code
expects quantized integers. This affects animation calculations and other paired
single routines, even though the `psq_l` and `psq_st` instructions themselves are
implemented. Tests exercise quantized instructions immediately after thread
entry and preserve modified values across thread switches.

The initialization follows [Decaf's Cafe thread contexts](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/cafe/libraries/coreinit/coreinit_thread.cpp).
