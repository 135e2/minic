#include <err.h>

#include "Utils.h"

namespace clang {
std::unique_ptr<CompilerInvocation>
buildCompilerInvocation(ArrayRef<const char *> args) {
  IntrusiveRefCntPtr<DiagnosticsEngine> diags(
#if LLVM_VERSION_MAJOR >= 20
      CompilerInstance::createDiagnostics(*llvm::vfs::getRealFileSystem(),
#else
      CompilerInstance::createDiagnostics(
#endif
#if LLVM_VERSION_MAJOR >= 21
                                          *(new DiagnosticOptions),
#else
          new DiagnosticOptions,
#endif
                                          new IgnoringDiagConsumer, true));

  driver::Driver d(args[0], llvm::sys::getDefaultTargetTriple(), *diags,
                   "minic", llvm::vfs::getRealFileSystem());
  d.setCheckInputsExist(false);
  std::unique_ptr<driver::Compilation> comp(d.BuildCompilation(args));
  if (!comp)
    return nullptr;
  const driver::JobList &jobs = comp->getJobs();
  if (jobs.size() != 1 || !isa<driver::Command>(*jobs.begin()))
    return nullptr;

  const driver::Command &cmd = cast<driver::Command>(*jobs.begin());
  if (StringRef(cmd.getCreator().getName()) != "clang")
    return nullptr;
  const llvm::opt::ArgStringList &cc_args = cmd.getArguments();
  auto ci = std::make_unique<CompilerInvocation>();
  if (!CompilerInvocation::CreateFromArgs(*ci, cc_args, *diags))
    return nullptr;

  ci->getDiagnosticOpts().IgnoreWarnings = true;
  ci->getFrontendOpts().DisableFree = false;
  return ci;
}
} // namespace clang