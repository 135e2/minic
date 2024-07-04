#pragma once

#include "common.h"

extern SmallVector<StringRef, 0> ignores;
extern MapVector<Decl *, DeclMapData> d2name;
extern MapVector<const CompoundStmt *, CompoundStmtMapData> c2d;
extern DenseSet<CachedHashStringRef> used;

struct Collector : RecursiveASTVisitor<Collector> {
  SourceManager &sm;
  ASTContext &ctx;

  Collector(ASTContext &ctx) : sm(ctx.getSourceManager()), ctx{ctx} {}
  template <typename T1, typename T2> const T1 *getParent(const T2 &n);
  bool VisitFunctionDecl(FunctionDecl *fd);
  bool VisitVarDecl(VarDecl *vd);
  bool VisitFieldDecl(FieldDecl *fd);
  bool VisitTypeDecl(TypeDecl *td);
  bool VisitEnumConstantDecl(EnumConstantDecl *ecd);
};
