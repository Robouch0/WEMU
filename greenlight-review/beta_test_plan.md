### **BETA TEST PLAN – TEMPLATE**
*This file serves as a structured example of what features we expect for our Beta Test in july 2026.*

## **1. Project context**
Wemu is a wii U Emulator designed to work on Linux, our ambition is to be able to process instructions the same way the Wii U does to be able to run small PowerPc Applications, and potentially Nintendo Games in the future. Our way of making the difference from other existing emulator like CEMU, (that can already emulate almost perfectly every Wii U games) is to also developp a side application a part from the main core Wemu Emulator. This application will permit our user to use their phone as the wii u Gamepad. It will work by having an output video from the emulator running on computer and sent to the web app, to display data that should normally go on the Gamepad, and sent from the phone to our emulator the input, a button touch on the HUD of the phone for example.

## **2. User role**
[The following roles will be involved in beta testing]

| **Role Name**  | **Description** |
|--------|----------------------|
| Player | Any user playing with our Emulator |
| Contributor      | Any developper wanting to contribute on Wemu |


---

## **3. Feature table**
Feature ID | User role | Feature name | Short description
-- | -- | -- | --
F1 | Contributor | Build & Launch | User can build the emulator on a standard Linux system and launch it without manual patching or complex configuration
F2 | Player | Title Library | Emulator scans a directory containing dumped Wii U titles and displays name, publisher, version, and icon extracted from meta.xml
F3 | Player | RPX Loader | The emulator is able to load a main.rpx file, parse its structure, and prepare it for execution
F4 | Player | PowerPC Execution | Minimal PowerPC Program Execution
F5 | Player | Controller Binding | A modern USB controller (for example Xbox One Controller) can be mapped to Wii U GamePad buttons with visual input feedback
F6 | Contributor | Documentation &  instruction extension | A new contributor can follow the documentation to   understand the CPU pipeline, and add a minimal PowerPC instruction


---

## **4. Success Criteria**
Feature ID | Key success criteria | Indicator / metric | Result
-- | -- | -- | --
F1 | Emulator builds and starts on a clean Linux environment | Full build and launch in less than 7 minutes on a blank VM | Result
F2 | Game metadata is correctly read and displayed | 3 different dumped titles shown with correct name and publisher | Result
F3 | RPX file is recognized and parsed without crash | 2 RPX files loaded with valid entry point detection | Result
F4 | PowerPC code executes with observable behavior | A minimal RPX displays a graphical output (color change or moving shape) for at least 5 seconds | Result
F5 | Controller inputs are detected and mapped | 3 connection attempts with correct button feedback | Result
F6 | A contributor can extend CPU instruction set |  Following documentation, one new PPC instruction can be added, it is correctly executed and update corresponding registers

---
