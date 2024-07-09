#pragma once

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ParentMap.h>
#include <clang/AST/ParentMapContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeLoc.h>
#include <clang/Basic/FileManager.h>
#include <clang/Basic/LLVM.h>
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
#include <llvm/Config/llvm-config.h>
#include <llvm/Support/Casting.h>
#if LLVM_VERSION_MAJOR >= 16
#include <llvm/TargetParser/Host.h>
#else
#include <llvm/Support/Host.h>
#endif
#include <llvm/Support/Path.h>

using namespace clang;
using namespace llvm;

typedef struct {
  std::string name;
  size_t type_hash;
  const CompoundStmt *c;
} DeclMapData;

typedef struct {
  SmallVector<Decl *> d;
  int id;
} CompoundStmtMapData;

/*
 * Retrive n's parent node in the ASTContext.
 * Return a pointer to the first-found parent with type T1,
 * otherwise a nullptr.
 */
template <typename T1, typename T2>
const T1 *getParent(ASTContext &ctx, const T2 &n) {
  DynTypedNode dyn = DynTypedNode::create(n);
  const auto &parents = ctx.getParents(dyn);
  for (auto &p : parents) {
    if (auto *tp = p.get<T1>())
      return tp;
  }
  return nullptr;
}