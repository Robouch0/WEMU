# Ce qui a été ajouté dans la branche 78

Ce document résume de façon lisible tout ce qui a été implémenté dans la branche
`78-hle-blocking-layers`, pour permettre de comprendre et reproduire le travail.

---

## 1. Instructions CPU ajoutées

### Branches (`Branch.cpp`)

| Instruction | Description |
|---|---|
| `B` | Saut relatif inconditionnel |
| `BA` | Saut absolu inconditionnel |
| `BL` | Saut relatif + sauvegarde adresse retour dans LR |
| `BLA` | Saut absolu + sauvegarde dans LR |
| `BC` | Saut conditionnel relatif (champs BO/BI/BD) |
| `BCA` | Saut conditionnel absolu |
| `BCL` | Saut conditionnel relatif + sauvegarde LR |
| `BCLA` | Saut conditionnel absolu + sauvegarde LR |
| `BLR` | Retour de fonction (saute à l'adresse dans LR) |
| `BCCTR` | Saut conditionnel vers l'adresse dans CTR |

**Logique commune :**
- Offset B-form : `li` (24 bits) décalé de 2 puis sign-étendu sur 26 bits.
- Offset BC-form : `bd` (14 bits) décalé de 2 puis casté en `int16_t`.
- Condition : champ `BO` (5 bits) contrôle l'utilisation du CTR et du bit CR[BI].
  - `BO=20` : brancher toujours (ignore CTR et CR).
  - `BO=18` : brancher si CR[BI] == 1.
  - `BO=4` : décrémente CTR, brancher si CTR != 0.
- `BCCTR` ne décrémente jamais CTR.

### Mémoire (`Memory.cpp`)

| Instruction | Description |
|---|---|
| `LWZ` | Charge un mot (32 bits) zero-étendu depuis la mémoire |
| `LWZU` | Idem + met à jour RA avec l'adresse effective |
| `LBZ` | Charge un octet zero-étendu |
| `LBZU` | Idem + met à jour RA |
| `LHZ` | Charge un demi-mot (16 bits) zero-étendu |
| `STW` | Stocke un mot (32 bits) en mémoire |
| `STWU` | Idem + met à jour RA |
| `STB` | Stocke un octet (bits 0-7 du registre source) |
| `STBU` | Idem + met à jour RA |
| `STH` | Stocke un demi-mot (bits 0-15 du registre source) |

**Points clés :**
- La mémoire PowerPC est big-endian. Tous les accès utilisent `std::byteswap` pour
  convertir vers/depuis l'ordre du host (little-endian sur x86).
- L'adresse effective est `EA = (RA==0 ? 0 : GPR[RA]) + sign_extend(SI)`.
- Les instructions "with update" (suffixe U) écrivent toujours EA dans GPR[RA],
  même si l'accès mémoire échoue (adresse invalide).

### Appel système HLE (`Sc.cpp`)

| Instruction | Description |
|---|---|
| `SC` | Déclenche un appel HLE via l'index du symbole dans `instr.bd` |

**Fonctionnement :**
- `instr.bd` contient l'index du symbole dans `binary.symbols`.
- Le nom du symbole est cherché dans `Core::syscallHandler.syscallTable`.
- Si un handler est enregistré, il est appelé avec l'`Interpreter&`.
- Sinon, un message d'erreur est affiché sur stderr (pas de crash).

---

## 2. Infrastructure mémoire (`Memory.hpp`)

Une classe `Core::Memory` a été ajoutée dans `core/src/cpu/memory/Memory.hpp`.

**Ce qu'elle fait :**
- Alloue un vecteur d'octets représentant la RAM guest (typiquement 8 Mo pour les tests,
  1 Go pour l'émulation réelle).
- Mappe les adresses virtuelles PowerPC vers des pointeurs host via `translate()`.
  Les plages valides sont : `ApplicationCode` (0x02000000), `ApplicationData` (0x10000000),
  `GraphicsResources` (0xF4000000).
- Fournit un allocateur bump (`heapAlloc`) pour les allocations HLE.
- Fournit `write32(vaddr, value)` pour écrire un mot 32 bits big-endian directement.

**Intégration :**
- `Core::Binary` contient désormais un champ `Core::Memory m_memory {}`.
- Les instructions mémoire appellent `cpu.m_binary.m_memory.translate(ea)` pour obtenir
  un pointeur host.

---

## 3. Infrastructure HLE (`SyscallHandler.hpp`)

Un gestionnaire de syscalls HLE a été ajouté dans `core/src/cpu/interpreter/SyscallHandler.hpp`.

**Ce qu'il fait :**
- Maintient une `std::map<std::string, Handler>` associant un nom de symbole à une
  fonction C++ à appeler.
- `registerSyscall(name, handler)` enregistre un handler.
- `get(name)` retourne le handler.
- La macro `REGISTER_SYSCALL(func)` enregistre automatiquement une fonction C++ en
  lisant les arguments depuis les GPRs (r3, r4, …) et en écrivant le retour dans r3.
- Support des fonctions prenant `Core::Interpreter&` en premier paramètre (injection cpu).
- Instance globale inline : `Core::syscallHandler`.

---

## 4. Bibliothèque coreinit (`core/src/lib/coreinit/`)

Les fonctions HLE suivantes ont été implémentées et enregistrées via `RegisterCoreInitFunctions()`.

### OSScreen (affichage Wii U)
| Fonction | Rôle |
|---|---|
| `OSScreenInit` | Réinitialise les états des écrans TV et DRC |
| `OSScreenGetBufferSizeEx` | Retourne la taille du framebuffer (TV: 3.5 Mo, DRC: 1.6 Mo) |
| `OSScreenSetBufferEx` | Enregistre l'adresse virtuelle du framebuffer |
| `OSScreenEnableEx` | Active/désactive un écran |
| `OSScreenClearBuffersEx` | Remplit le framebuffer avec une couleur RGBX8888 |
| `OSScreenPutFontEx` | Affiche du texte (sur stdout, sans renderer) |
| `OSScreenFlipBuffersEx` | No-op (renderer Vulkan pas encore connecté) |

### OS Memory (allocations)
| Fonction | Rôle |
|---|---|
| `OSGetForegroundBucket` | Alloue un bloc de 64 Mo pour le foreground (une seule fois) |
| `OSAllocFromSystem` | Allocation bump avec alignement |
| `OSFreeToSystem` | No-op (allocateur bump, pas de libération) |
| `MEMGetBaseHeapHandle` | Retourne un handle opaque pour MEM1/MEM2 |
| `MEMCreateExpHeapEx` | Retourne l'adresse passée comme handle (convention decaf-emu) |
| `MEMAllocFromExpHeapEx` | Délègue à `heapAlloc` |
| `MEMFreeToExpHeap` | No-op |
| `MEMAllocFromDefaultHeap` | Délègue à `heapAlloc` (alignement 4) |
| `MEMAllocFromDefaultHeapEx` | Idem avec alignement configurable |
| `MEMFreeToDefaultHeap` | No-op |

### OS Cache
| Fonction | Rôle |
|---|---|
| `DCFlushRange` | No-op (cache cohérent côté host) |
| `DCInvalidateRange` | No-op |
| `DCStoreRange` | No-op |
| `DCFlushRangeNoSync` | No-op |
| `ICInvalidateRange` | No-op |

### OS System (timing, mutexes, debug)
| Fonction | Rôle |
|---|---|
| `OSGetTick` | Compteur croissant (+62500 par appel) |
| `OSSleepTicks` | No-op (émulation mono-thread) |
| `OSInitMutex` | Zéroïse les 28 octets du struct OSMutex en guest memory |
| `OSLockMutex` | No-op |
| `OSUnlockMutex` | No-op |
| `OSReport` | Affiche un message formaté sur stderr (handler custom, variadic) |
| `OSFatal` | Affiche un message et appelle `exit(1)` |

---

## 5. Checklist "à refaire soi-même"

Dans l'ordre logique d'implémentation :

1. **`core/src/cpu/memory/Memory.hpp`** — Créer la classe `Core::Memory` avec vecteur,
   `translate()`, `heapAlloc()`, `write32()`. Ajouter `Core::Memory m_memory {}` dans
   `Core::Binary`.

2. **`core/src/cpu/instructions/Branch.cpp`** — Implémenter les 10 instructions de branche.
   Ne pas oublier : `branchOffset` sign-étend sur 26 bits, `bcBranchOffset` cast en `int16_t`,
   `evalBranchCondition` gère CTR+CR selon `BO`.

3. **`core/src/cpu/instructions/Memory.cpp`** — Implémenter LWZ/LWZU/LBZ/LBZU/LHZ +
   STW/STWU/STB/STBU/STH. Toujours `byteswap` pour les accès big-endian.

4. **`core/src/cpu/interpreter/SyscallHandler.hpp`** — Créer `Core::SyscallHandler` avec
   `syscallTable`, `registerSyscall`, `get`. Ajouter les templates `invoke<func>` et
   `REGISTER_SYSCALL`. Déclarer `inline Core::SyscallHandler syscallHandler`.

5. **`core/src/cpu/instructions/Sc.cpp`** — Implémenter `SC` : lookup `instr.bd` dans
   `binary.symbols`, appeler le handler ou logger l'absence.

6. **`core/src/lib/coreinit/`** — Implémenter OsScreen, OsMemory, OsCache, OsSystem.
   Enregistrer via `RegisterCoreInitFunctions()` appelé au démarrage de l'interpréteur.

7. **`core/src/cpu/tables/cpu_instructions.anh`** — Ajouter les entrées pour B, BA, BL,
   BLA, BC, BCA, BCL, BCLA, BLR, BCCTR, SC, LWZ, LWZU, LBZ, LBZU, LHZ, STW, STWU,
   STB, STBU, STH.

8. **Tests** — `test_branch.cpp`, `test_memory.cpp`, `test_sc.cpp` (voir `core/tests/`).
