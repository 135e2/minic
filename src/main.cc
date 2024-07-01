/*
 * MiniASTConsumer collects identifiers in `used` and rename candidates (in the
 * main file) in `d2name`. MiniASTConsumer iterates over `d2name` and assigns
 * new names. Renamer creates clang::tooling::Replacement instances.
 * HandleTranslationUnit calls clang::tooling::applyAllReplacements.
 */

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Driver/Action.h>
#include <clang/Driver/Compilation.h>
#include <clang/Driver/Driver.h>
#include <clang/Driver/Tool.h>
#include <clang/Format/Format.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Lexer.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Tooling/Core/Replacement.h>
#include <llvm/ADT/CachedHashString.h>
#include <llvm/ADT/DenseSet.h>
#include <llvm/ADT/MapVector.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <err.h>
#include <memory>
#include <unistd.h>
#include <vector>

#include "postProcess.h"

using namespace clang;
using namespace llvm;

typedef struct {
  std::string name;
  size_t type_hash;
} DeclMapData;

SmallVector<StringRef, 0> ignores;
MapVector<Decl *, DeclMapData> d2name;
DenseSet<CachedHashStringRef> used;
std::string newCode;

namespace {
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

struct Collector : RecursiveASTVisitor<Collector> {
  SourceManager &sm;

  Collector(ASTContext &ctx) : sm(ctx.getSourceManager()) {}

  bool VisitFunctionDecl(FunctionDecl *fd) {
    if (fd->isOverloadedOperator() || !fd->getIdentifier())
      return true;
    used.insert(CachedHashStringRef(fd->getName()));
    if (!fd->isDefined())
      return true;
    std::string name = fd->getNameAsString();
    if (sm.isWrittenInMainFile(fd->getLocation())) {
      if (!is_contained(ignores, name))
#ifndef NDEBUG

        outs() << "in VisitFunctionDecl, typeid: "
               << typeid(fd->getCanonicalDecl()).name() << "\n",
#endif
            d2name[fd->getCanonicalDecl()].type_hash =
                typeid(fd->getCanonicalDecl()).hash_code();
      for (ParmVarDecl *param : fd->parameters())
        VisitVarDecl(param);
    }
    return true;
  }
  bool VisitVarDecl(VarDecl *vd) {
    if (!vd->getIdentifier())
      return true;
    used.insert(CachedHashStringRef(vd->getName()));
    auto kind = vd->isThisDeclarationADefinition();
    if (kind != VarDecl::Definition ||
        !sm.isWrittenInMainFile(vd->getLocation()))
      return true;
#ifndef NDEBUG
    outs() << "in VisitVarDecl, typeid: "
           << typeid(vd->getCanonicalDecl()).name() << "\n",
#endif
        d2name[vd->getCanonicalDecl()].type_hash =
            typeid(vd->getCanonicalDecl()).hash_code();
    return true;
  }
  bool VisitFieldDecl(FieldDecl *fd) {
    used.insert(CachedHashStringRef(fd->getName()));
    if (!sm.isWrittenInMainFile(fd->getLocation()))
      return true;
#ifndef NDEBUG
    outs() << "in VisitFieldDecl, typeid: "
           << typeid(fd->getCanonicalDecl()).name() << "\n",
#endif
        d2name[fd->getCanonicalDecl()].type_hash =
            typeid(fd->getCanonicalDecl()).hash_code();
    return true;
  }
  bool VisitTypeDecl(TypeDecl *td) {
    used.insert(CachedHashStringRef(td->getName()));
    if (!sm.isWrittenInMainFile(td->getLocation()))
      return true;
#ifndef NDEBUG
    outs() << "in VisitTypeDecl, typeid: " << typeid(td).name() << "\n",
#endif
        // TypeDecl does not have its own getCanonicalDecl method, so calling
        // td->getCanonicalDecl() would get its base class (Decl *)this and is
        // certainly of no use.
        d2name[td->getCanonicalDecl()].type_hash = typeid(td).hash_code();
    return true;
  };
};

struct Renamer : RecursiveASTVisitor<Renamer> {
  SourceManager &sm;
  tooling::Replacements &reps;

  Renamer(ASTContext &ctx, tooling::Replacements &reps)
      : sm(ctx.getSourceManager()), reps(reps) {}
  void replace(CharSourceRange csr, StringRef newText) {
    cantFail(reps.add(tooling::Replacement(sm, csr, newText)));
  }

  bool VisitFunctionDecl(FunctionDecl *fd) {
    auto *canon = fd->getCanonicalDecl();
    auto it = d2name.find(canon);
    if (it != d2name.end())
      replace(CharSourceRange::getTokenRange(fd->getLocation()),
              it->second.name);
    return true;
  }
  // CXXConstructorDecl is a special kind of FunctionDecl/CXXMethodDecl that
  // needs to be renamed to its parent class
  bool VisitCXXConstructorDecl(CXXConstructorDecl *ccd) {
    // the canon decl should be the same as its class's (in other words,
    // its parent's)
    auto *canon = ccd->getParent()->getCanonicalDecl();
    auto it = d2name.find(canon);
    if (it != d2name.end()) {
      replace(CharSourceRange::getTokenRange(ccd->getLocation()),
              it->second.name);
      for (CXXCtorInitializer *cci : ccd->inits())
        VisitCXXCtorInitializer(cci);
      for (ParmVarDecl *param : ccd->parameters())
        VisitVarDecl(param);
    }
    return true;
  }
  // And constructor leads to another oddity: C++ base/member initializer
  bool VisitCXXCtorInitializer(CXXCtorInitializer *cci) {
    auto *canon = cci->getMember()->getCanonicalDecl();
    auto it = d2name.find(canon);
    if (it != d2name.end())
      replace(CharSourceRange::getTokenRange(cci->getSourceLocation()),
              it->second.name);
    return true;
  }
  bool VisitMemberExpr(MemberExpr *me) {
    DeclarationNameInfo ni = me->getMemberNameInfo();
    std::string name = ni.getAsString();
    if (sm.isWrittenInMainFile(me->getMemberLoc())) {
      auto *canon = me->getMemberDecl()->getCanonicalDecl();
      auto it = d2name.find(canon);
      if (it != d2name.end()) {
        replace(CharSourceRange::getTokenRange(me->getMemberLoc()),
                it->second.name);
      }
    }
    return true;
  }
  bool VisitVarDecl(VarDecl *vd) {
    auto *canon = vd->getCanonicalDecl();
    auto it = d2name.find(canon);
    if (it != d2name.end())
      replace(CharSourceRange::getTokenRange(vd->getLocation()),
              it->second.name);
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *dre) {
    Decl *d = dre->getDecl();
    if (!(isa<FunctionDecl>(d) || isa<VarDecl>(d) || isa<FieldDecl>(d) ||
          isa<TypeDecl>(d))) {
      return true;
    }
    auto it = d2name.find(d->getCanonicalDecl());
    if (it != d2name.end())
      replace(
          CharSourceRange::getTokenRange(SourceRange(dre->getSourceRange())),
          it->second.name);
    return true;
  }
  bool VisitFieldDecl(FieldDecl *fd) {
    auto *canon = fd->getCanonicalDecl();
    auto it = d2name.find(canon);
    if (it != d2name.end())
      replace(CharSourceRange::getTokenRange(fd->getLocation()),
              it->second.name);
    return true;
  }
  bool VisitTypeDecl(TypeDecl *d) {
    auto *canon = d->getCanonicalDecl();
    if (auto it = d2name.find(canon); it != d2name.end())
      replace(CharSourceRange::getTokenRange(d->getLocation()),
              it->second.name);
    return true;
  }
  bool VisitTypeLoc(TypeLoc tl) {
    TypeDecl *td = nullptr;
    if (const TagTypeLoc ttl = tl.getAs<TagTypeLoc>())
      td = ttl.getDecl()->getCanonicalDecl();
    if (const TypedefTypeLoc tdl = tl.getAs<TypedefTypeLoc>())
      td = tdl.getTypedefNameDecl()->getCanonicalDecl();
    if (const TemplateTypeParmTypeLoc ttptl =
            tl.getAs<TemplateTypeParmTypeLoc>())
      td = ttptl.getDecl();
    if (auto it = d2name.find(td); it != d2name.end())
      replace(CharSourceRange::getTokenRange(tl.getSourceRange()),
              it->second.name);
    return true;
  }
};

struct MiniASTConsumer : ASTConsumer {
  ASTContext *ctx;
  int n_fn = 0, n_var = 0, n_fld = 0, n_type = 0;

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
        v.name = getName(vName, "nopqrstuvwxyz", n_var);
      } else if (v.type_hash == typeid(FieldDecl *).hash_code()) {
        v.name = getName(vName, "nopqrstuvwxyz", n_fld);
      } else if (v.type_hash == typeid(TypeDecl *).hash_code()) {
        v.name = getName(vName, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", n_type);
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
} // namespace

int main(int argc, char *argv[]) {
  std::vector<const char *> args{argv[0], "-fsyntax-only",
                                 "-I/usr/lib/clang/17/include"};
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