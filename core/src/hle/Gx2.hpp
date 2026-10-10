#pragma once

// Registers HLE stubs for the GX2 graphics library. For now these are no-ops (the real GX2->Vulkan
// translation is a later milestone); the only ones with real behaviour are the size/version queries
// that a title reads back to size its allocations, which must not return zero.
void RegisterGx2Functions();
