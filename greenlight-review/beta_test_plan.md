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
Feature ID | User role | Feature name                   | Short description
-- | -- |--------------------------------| --
F1 | Contributor | Build & Launch                 | As a player or a contributor, I can build the emulator on a standard Linux system and launch it without manual patching or complex configuration
F2 | Player | Browse the title library       | As a player, I want to see my homebrew titles displayed with their name, publisher and version, so that I can browse my library visually.
F3 | Player | Load an RPX file               | As a player, I want the emulator to open and parse a main.rpx file, so that a title is recognized and prepared for execution without me worrying about file formats or entrypoint.
F4 | Player | Execute PowerPC code           | As a player, I want to see visible graphic output when the emulator runs a PowerPC program, so that I can play real games.
F5 | Player | Bind a USB controller          | As a player, I want to map a modern USB controller (Xbox One) to Wii U GamePad buttons with visual input feedback, so that I can play comfortably with hardware I already own.
F6 | Contributor | Extend the CPU instruction set | As a new contributor, I can follow the documentation to add a minimal PowerPC instruction, so that I can grow the emulator's CPU pipeline without needing insider knowledge.


---

## **4. Success Criteria**
Feature ID | Key success criteria | Indicator / metric | Result
-- | -- | -- | --
F1 | Emulator builds and starts on a clean Linux environment | Full build and launch in less than 7 minutes on a blank VM | Result
F2 | Game metadata is correctly read and displayed | 3 different dumped titles shown with correct name and publisher | Result
F3 | RPX file is recognized and parsed without crash | 2 RPX files loaded with valid entry point detection | Result
F4 | PowerPC code executes with observable behavior | A minimal RPX displays a graphical output (color change or moving shape) for at least 5 seconds | Result
F5 | Controller inputs are detected and mapped | 3 connection attempts with correct button feedback | Result
F6 | A contributor can extend CPU instruction set |  Following documentation, one new PPC instruction can be added, it is correctly executed and update corresponding registers | Result

---
