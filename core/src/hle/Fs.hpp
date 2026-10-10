#pragma once

#include <string>

// Registers HLE for the Cafe filesystem (FSOpenFile/FSReadFile/...). Guest paths under
// "/vol/content" are served from the host content root set via SetFsContentRoot().
void RegisterFsFunctions();

// Sets the host directory that backs "/vol/content" (typically "<gamedir>/content").
void SetFsContentRoot(const std::string &hostPath);

// Registers a fallback content root (title-update layering: primary root first, then layers).
void AddFsContentLayer(const std::string &hostPath);
