#pragma once
#include <iostream>
#include <string>

// --verbose: the build prints every file it processes and every tool it runs.
// Without it a build is one line when it works, and the errors when it doesn't
inline bool g_verbose = false;

// --progress: the compiler tells the CLI what it is doing, one ">> phase" line per step on
// stdout, and the CLI shows it as a status line
inline bool g_progress = false;
inline void progress(const std::string& phase)
{
    if (!g_progress) return;
    std::cout << ">> " << phase << std::endl;
}
