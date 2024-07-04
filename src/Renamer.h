#pragma once

#include "common.h"

extern MapVector<Decl *, DeclMapData> d2name;

struct Renamer : RecursiveASTVisitor<Renamer> {
  SourceManager &sm;
  tooling::Replacements &reps;

  Renamer(ASTContext &ctx, tooling::Replacements &reps)
      : sm(ctx.getSourceManager()), reps(reps) {}
  void replace(CharSourceRange csr, StringRef newText) {
    cantFail(reps.add(tooling::Replacement(sm, csr, newText)));
  }

  bool VisitFunctionDecl(FunctionDecl *fd);
  // CXXConstructorDecl is a special kind of FunctionDecl/CXXMethodDecl that
  // needs to be renamed to its parent class
  bool VisitCXXConstructorDecl(CXXConstructorDecl *ccd);
  // And constructor leads to another oddity: C++ base/member initializer
  bool VisitCXXCtorInitializer(CXXCtorInitializer *cci);
  bool VisitMemberExpr(MemberExpr *me);
  bool VisitVarDecl(VarDecl *vd);
  bool VisitDeclRefExpr(DeclRefExpr *dre);
  bool VisitFieldDecl(FieldDecl *fd);
  bool VisitTypeDecl(TypeDecl *d);
  bool VisitTypeLoc(TypeLoc tl);
  bool VisitEnumConstantDecl(EnumConstantDecl *ecd);
};
