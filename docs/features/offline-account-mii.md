# Offline account avatars and shared saves

The offline account now returns a generated, valid 96-byte Mii store record from
`nn::act::GetMii` and `GetMiiEx`. It uses the existing account name, WEMU, a stable
creator ID, little-endian packed fields and names, and a big-endian CRC-16 checksum.
The record is generated from format fields; it does not contain console-dump assets.

`SAVEGetSharedSaveDataPath` returns a path containing the complete requested title
ID. The filesystem backs this path under the existing game's save directory:

```text
<game>/wemu_save/shared/<16-digit-title-id>/<relative-path>
```

This follows WEMU's existing per-game save storage. Different shared title IDs have
separate directories. Normal FS open/read/write/stat/directory operations use the
same mount. Missing files still report not-found; writable opens create the parent
directories and files through the normal filesystem implementation.

## Why these services matter for menu transitions

The original MK8 Grand Prix reproduction paused a race, selected Quit, and confirmed
Yes. Returning to the menu crashed at guest PC `0x029D43D8`, reading
`0xD7581F54`, in both normal and synchronous rendering runs.

The shared-save path had no backing mount, so the Mii library could not create its
database and its initialization returned error `0x13`. Menu rendering objects then
retained their previous contents. Fresh zeroed memory concealed this on startup;
after a race, reused texture-resource records became an invalid rendering index.

Backing the database alone exposed another HLE error: account queries returned
success with an all-zero avatar. The library rejected that record and left a menu
object partially initialized. Both service corrections are needed. Neither change
patches guest code or checks a game identity, guest PC, or rendering address.

## Validation

- The account regression checks both exported getters, stability, record bounds,
  name agreement, field constraints, and an independently calculated CRC remainder.
- The filesystem regression creates a missing shared database, writes and closes
  it, reopens and reads it, and checks separation between shared title IDs.
- Manual regression: start a Grand Prix race, pause, choose Quit, confirm Yes,
  return to the menu, then start another race and repeat. Test both existing and
  newly created shared-save storage.

Recorded validation: 1,048 tests passed with Vulkan enabled. The fresh-storage
replay returned to Single Player and loaded another Grand Prix, completing 12,000
presents without an exception. The desktop-built core also returned to Single
Player with existing storage and completed 6,500 presents. The original build
failed at present 5,302 with the same input sequence.

Format references: [FFL packed record layout](https://github.com/ariankordi/ffl/blob/master/include/nn/ffl/FFLiMiiDataCore.h),
[FFL checksum](https://github.com/ariankordi/ffl/blob/master/src/detail/FFLiCrc.cpp),
[Cafe SAVE API](https://github.com/devkitPro/wut/blob/master/include/nn/save/save.h).
