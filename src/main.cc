#include <err.h>
#include <memory>
#include <unistd.h>

#include "Collector.h"
#include "Renamer.h"
#include "common.h"
#include "postProcess.h"

SmallVector<StringRef, 0> ignores;
MapVector<Decl *, DeclMapData> d2name;
MapVector<const CompoundStmt *, CompoundStmtMapData> c2d;
DenseSet<CachedHashStringRef> used;

std::string newCode;
int LocalIDMax;

namespace clang {
std::unique_ptr<CompilerInvocation>
buildCompilerInvocation(ArrayRef<const char *> args) {
  IntrusiveRefCntPtr<DiagnosticsEngine> diags(
      CompilerInstance::createDiagnostics(new DiagnosticOptions,
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

struct MiniASTConsumer : ASTConsumer {
  ASTContext *ctx;
  int n_fn = 0, n_var = 0, n_fld = 0, n_type = 0, n_enumconst = 0;

  void Initialize(ASTContext &ctx) override { this->ctx = &ctx; }
  static std::string getName(StringRef origName, StringRef prefix, int &id) {
    static const char digits[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string newName;
    int old_n = id;
    for (;;) {
      newName = std::string(1, prefix[id % prefix.size()]);
      if (int i = id / prefix.size())
        while (newName += digits[i % 62], i /= 62)
          ;
      id++;
      if (!used.contains(CachedHashStringRef(newName)))
        break;
    }
    if (newName.size() >= origName.size()) {
      newName = origName;
      id = old_n;
    }
    return newName;
  }
  bool HandleTopLevelDecl(DeclGroupRef dgr) override {
    for (auto s : {"j0", "j1", "jn", "j0f", "j1f", "jnf", "j0l", "j1l", "jnl"})
      used.insert(CachedHashStringRef(s));
    for (auto s : {"y0", "y1", "yn", "y0f", "y1f", "ynf", "y0l", "y1l", "ynl"})
      used.insert(CachedHashStringRef(s));
    return true;
  }
  void HandleTranslationUnit(ASTContext &ctx) override {
    Collector c(ctx);
    c.TraverseDecl(ctx.getTranslationUnitDecl());
    for (auto &[d, v] : d2name) {
      std::string vName =
          dynamic_cast<NamedDecl *>(d)->getDeclName().getAsString();
#ifndef NDEBUG
      outs() << "type_hash: " << v.type_hash << ", renaming from " << vName;
#endif
      if (v.type_hash == typeid(FunctionDecl *).hash_code())
        v.name = getName(vName, "abcdefghijklm", n_fn);
      else if (v.type_hash == typeid(VarDecl *).hash_code()) {
        if (auto it = c2d.find(v.c); it != c2d.end()) {
          int &id = it->second.id;
          v.name = getName(vName, "rstuvwxyz", id);
        } else
          // global variables, should not share w/ local
          v.name = getName(vName, "nopq", n_var);
      } else if (v.type_hash == typeid(FieldDecl *).hash_code()) {
        v.name = getName(vName, "nopqrstuvwxyz", n_fld);
      } else if (v.type_hash == typeid(TypeDecl *).hash_code()) {
        v.name = getName(vName, "ABCDEFGHIJKLM", n_type);
      } else if (v.type_hash == typeid(EnumConstantDecl *).hash_code()) {
        v.name = getName(vName, "NOPQRSTUVWXYZ", n_enumconst);
      }
#ifndef NDEBUG
      outs() << " to " << v.name << "\n";
#endif
    }
    tooling::Replacements reps;
    Renamer r(ctx, reps);
    r.TraverseDecl(ctx.getTranslationUnitDecl());
    auto &sm = ctx.getSourceManager();
    StringRef code = sm.getBufferData(sm.getMainFileID());
    auto res = tooling::applyAllReplacements(code, reps);
    if (!res)
      errx(2, "failed to apply replacements: %s",
           toString(res.takeError()).c_str());
    newCode = *res;
  }
};

struct MiniAction : ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &ci,
                                                 StringRef inFile) override {
    return std::make_unique<MiniASTConsumer>();
  }
};

void reformat() {
  auto buf = MemoryBuffer::getMemBuffer(newCode, "", true);
  format::FormatStyle style =
      cantFail(format::getStyle("LLVM", "-", "LLVM", newCode, nullptr));
  style.ColumnLimit = 9999;
  style.IndentWidth = 0;
  style.ContinuationIndentWidth = 0;
  style.SpaceBeforeAssignmentOperators = false;
  style.SpaceBeforeParens = format::FormatStyle::SBPO_Never;
  style.AlignEscapedNewlines = format::FormatStyle::ENAS_DontAlign;

  format::FormattingAttemptStatus status;
  std::vector<tooling::Range> ranges{{0, unsigned(newCode.size())}};
  tooling::Replacements reps =
      format::reformat(style, newCode, ranges, "-", &status);
  auto res = tooling::applyAllReplacements(newCode, reps);
  if (!res)
    errx(2, "failed to apply replacements: %s",
         toString(res.takeError()).c_str());
  newCode = *res;
}
} // namespace clang

int main(int argc, char *argv[]) {
  std::vector<const char *> args{argv[0], "-fsyntax-only",
                                 "-I/usr/lib/clang/18/include"};
  bool inplace = false;
  const char *outfile = "/dev/stdout";
  const char usage[] = R"(Usage: %s [-i] [-f fun]... a.c

Options:
-i      edit a.c in place\n)";
  for (int i = 1; i < argc; i++) {
    StringRef opt(argv[i]);
    if (opt[0] != '-')
      args.push_back(argv[i]);
    else if (opt == "-h") {
      fputs(usage, stdout);
      return 0;
    } else if (opt == "-i")
      inplace = true;
    else if (opt == "-f" && i + 1 < argc)
      ignores.push_back(argv[++i]);
    else if (opt == "-o" && i + 1 < argc)
      outfile = argv[++i];
    else {
      fputs(usage, stderr);
      return 1;
    }
  }

  if (argc < 2) {
    fputs(usage, stderr);
    return 1;
  }

  ignores.push_back("main");

  auto ci = buildCompilerInvocation(args);
  if (!ci)
    errx(1, "failed to build CompilerInvocation");

  auto inst = std::make_unique<CompilerInstance>(
      std::make_shared<PCHContainerOperations>());
  IgnoringDiagConsumer dc;
  inst->setInvocation(std::move(ci));
  inst->createDiagnostics(&dc, false);
  inst->getDiagnostics().setIgnoreAllWarnings(true);
  inst->setTarget(TargetInfo::CreateTargetInfo(
      inst->getDiagnostics(), inst->getInvocation().TargetOpts));
  if (!inst->hasTarget())
    errx(1, "hasTarget returns false");
  inst->createFileManager(llvm::vfs::getRealFileSystem());
  inst->setSourceManager(
      new SourceManager(inst->getDiagnostics(), inst->getFileManager(), true));

  MiniAction action;
  if (!action.BeginSourceFile(*inst, inst->getFrontendOpts().Inputs[0]))
    errx(2, "failed to parse");
  if (Error e = action.Execute())
    errx(2, "failed to execute");
  action.EndSourceFile();
  reformat();
  postProcess(newCode);

  std::error_code ec;
  raw_fd_ostream(inplace ? inst->getFrontendOpts().Inputs[0].getFile()
                         : outfile,
                 ec, sys::fs::OF_None)
      << newCode;
}