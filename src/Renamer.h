#pragma once

#include "common.h"

extern MapVector<Decl *, DeclMapData> d2name;

struct Renamer : RecursiveASTVisitor<Renamer> {
  SourceManager &sm;
  tooling::Replacements &reps;
  ASTContext &ctx;

  Renamer(ASTContext &ctx, tooling::Replacements &reps)
      : sm(ctx.getSourceManager()), reps(reps), ctx{ctx} {}
  void replace(CharSourceRange csr, StringRef newText) {
    cantFail(reps.add(tooling::Replacement(sm, csr, newText)));
  }
  template <typename T1, typename T2> const T1 *getParent(const T2 &n) {
    return ::getParent<T1, T2>(ctx, n);
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
  /* clang AST would not visit ClassTemplateSpecializationDecl by default,
   * but we need template specializations to obtain member type info.
   *
   * Ref: https://stackoverflow.com/a/59550277
   *      https://clang.llvm.org/doxygen/classclang_1_1RecursiveASTVisitor.html#a426895210c9f7c02589702fd412da81b
   */
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool VisitTypeDecl(TypeDecl *d);
  bool VisitTypeLoc(TypeLoc tl);
  bool VisitEnumConstantDecl(EnumConstantDecl *ecd);
};
