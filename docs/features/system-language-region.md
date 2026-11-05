# Game language and console region

The desktop Settings page exposes **Game language** and **Console region**.
English is the default language. The default region is **Automatic (game
region)**, read from the launched title's `meta/meta.xml`. Regional overrides
describe the console; the title's region continues to describe the dump.
Selections persist in QSettings and apply to the next game launch. They do not
change an already running session.

The guest receives its configured Cafe language ID through `UCReadSysConfig`
for the exact `cafe.language` key. `MCP_GetSysProdSettings` returns the console's
region at byte `0x03` and the title's region at byte `0x0B` in its 0x46-byte
structure. These were previously left zero, which can make games choose a
Japanese fallback even when the language query returns English. Settings belong
to each Interpreter rather than a process-global language cache.

Games choose their own text, subtitle and font assets from these ordinary
system queries. Available languages depend on the dump; this feature does not
translate text, rewrite paths, replace movie subtitles or patch a title. Region
and language are independent: selecting Europe alone does not request French.
The launcher defaults to Europe when region metadata is missing or malformed.
For metadata supporting several regions, it prefers Europe, then North America,
Japan, China, Korea and Taiwan. Unknown region bit 0x08 is not offered.

For direct core launches, `WEMU_SYSTEM_LANGUAGE` accepts Cafe IDs 0–11 or the
lowercase codes `ja`, `en`, `fr`, `de`, `it`, `es`, `zh`, `ko`, `nl`, `pt`, `ru`,
and `tw`. `WEMU_SYSTEM_REGION` and `WEMU_TITLE_REGION` accept decimal region flags:
Japan=1, North America=2, Europe=4, China=16, Korea=32, Taiwan=64. Direct launches
default to English/Europe; set the title region explicitly for other dumps.
Invalid language values fall back to English; invalid console regions fall
back to the configured title region. Desktop launches replace inherited values
with their saved settings and parsed title metadata.

`WEMU_SYSTEM_CONFIG_TRACE=1` traces UC/MCP queries for direct diagnostic runs.
Normal startup logs the resolved language and both region flags. Tests cover
language/region validation, exact guest byte layout, independent title/console
regions, UC output bounds, metadata parsing, persistence and environment
isolation.

Primary references: [Cemu's MCP/UC handlers](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/coreinit/coreinit_MCP.cpp),
[MCP structure layout](https://github.com/cemu-project/Cemu/blob/main/src/Cafe/OS/libs/coreinit/coreinit_MCP.h),
and [Decaf's region flags](https://github.com/decaf-emu/decaf-emu/blob/master/src/libdecaf/src/ios/mcp/ios_mcp_enum.h).
