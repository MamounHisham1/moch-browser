#include "ui/routinedialog.hpp"

// RoutineDialog is implemented inline in routinedialog.hpp, moc-free, so the
// moch target links before this file is added to CMake (see the note there).
// Keeping the translation unit in place makes the later wiring a one-line
// change, and it is the file the standalone syntax check compiles.
