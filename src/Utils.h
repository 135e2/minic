#pragma once

#include "common.h"

namespace clang {
std::unique_ptr<CompilerInvocation>
buildCompilerInvocation(ArrayRef<const char *> args);
} // namespace clang