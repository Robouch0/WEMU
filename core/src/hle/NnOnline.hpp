/*
** EPITECH PROJECT, 2026
** core
** File description:
** NnOnline -- HLE for the Wii U online/account service libraries MK8 initialises at boot
** (nn::ac internet connection, nn::fp friend-presence, nn::olv Miiverse, nn::boss background
** tasks, nn::ec e-commerce, AOC DLC). No real networking: we report a coherent offline state and
** deliver the async completion callbacks (e.g. nn::fp::LoginAsync) that the title waits on.
*/

#pragma once

void RegisterNnOnlineFunctions();
