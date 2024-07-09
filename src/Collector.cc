#include "Collector.h"

bool Collector::VisitFunctionDecl(FunctionDecl *fd) {
  if (fd->isOverloadedOperator() || !fd->getIdentifier())
    return true;
  used.insert(CachedHashStringRef(fd->getName()));
  if (!fd->isDefined())
    return true;
  std::string name = fd->getNameAsString();
  if (sm.isWrittenInMainFile(fd->getLocation())) {
    if (!is_contained(ignores, name))
#ifndef NDEBUG
      errs() << "in VisitFunctionDecl, typeid: "
             << typeid(fd->getCanonicalDecl()).name() << "\n",
#endif
          d2name[fd->getCanonicalDecl()].type_hash =
              typeid(fd->getCanonicalDecl()).hash_code();
    for (ParmVarDecl *param : fd->parameters())
      VisitVarDecl(param);
  }
  return true;
}

bool Collector::VisitVarDecl(VarDecl *vd) {
  if (!vd->getIdentifier())
    return true;
  used.insert(CachedHashStringRef(vd->getName()));
  auto kind = vd->isThisDeclarationADefinition();
  if (kind != VarDecl::Definition || !sm.isWrittenInMainFile(vd->getLocation()))
    return true;
  /* If it's an local variable, lookup its parent twice.
   * Function block level variable AST Chain:
   * FunctionDecl-->CompoundStmt-->DeclStmt-->VarDecl
   * example:
   * void foo(void) {
       int a = 0;
   * }
   *
   * Local code block level variable AST Chain:
   * Outer AST-->CompoundStmt-->CompoundStmt-->DeclStmt-->VarDecl
   * example:
   * void foo(void) {
   *   // ...code...
   *   {
         int b = 0;
   *   }
   * }
   *
   * We insert its grandparent (CompoundStmt) into the bidirectional map.
   */
  if (vd->isLocalVarDecl()) {
    const DeclStmt *ds = getParent<DeclStmt, VarDecl>(*vd);
    if (ds) {
      const CompoundStmt *cs = getParent<CompoundStmt, DeclStmt>(*ds);
      if (cs) {
        d2name[vd->getCanonicalDecl()].c = cs;
        c2d[cs].d.push_back(vd->getCanonicalDecl());
      }
    }
    /*
     * For the ParmVar, the structure looks like this:
     * FunctionDecl->CompoundStmt->DeclStmt->VarDecl
     *             ->ParmVarDecl
     * example:
     * void foo(int a) {
     *   int b;
     * }
     *
     * Insert the CompoundStmt into map.
     */
  } else if (const ParmVarDecl *pvd = dynamic_cast<ParmVarDecl *>(vd)) {
    if (const DeclContext *dc = pvd->getParentFunctionOrMethod();
        dc->isFunctionOrMethod()) {
      const FunctionDecl *fd = static_cast<const FunctionDecl *>(dc);
      // fd->dumpColor();
      if (const CompoundStmt *cs =
              static_cast<const CompoundStmt *>(fd->getBody())) {
        d2name[vd->getCanonicalDecl()].c = cs;
        c2d[cs].d.push_back(vd->getCanonicalDecl());
      }
    }
  }
#ifndef NDEBUG
  errs() << "in VisitVarDecl, typeid: " << typeid(vd->getCanonicalDecl()).name()
         << "\n",
#endif
      d2name[vd->getCanonicalDecl()].type_hash =
          typeid(vd->getCanonicalDecl()).hash_code();
  return true;
}

bool Collector::VisitFieldDecl(FieldDecl *fd) {
  used.insert(CachedHashStringRef(fd->getName()));
  if (!sm.isWrittenInMainFile(fd->getLocation()))
    return true;
#ifndef NDEBUG
  errs() << "in VisitFieldDecl, typeid: "
         << typeid(fd->getCanonicalDecl()).name() << "\n",
#endif
      d2name[fd->getCanonicalDecl()].type_hash =
          typeid(fd->getCanonicalDecl()).hash_code();
  return true;
}

bool Collector::VisitTypeDecl(TypeDecl *td) {
  used.insert(CachedHashStringRef(td->getName()));
  if (!sm.isWrittenInMainFile(td->getLocation()))
    return true;
#ifndef NDEBUG
  errs() << "in VisitTypeDecl, typeid: " << typeid(td).name() << "\n",
#endif
      // TypeDecl does not have its own getCanonicalDecl method, so calling
      // td->getCanonicalDecl() would get its base class (Decl *)this and is
      // certainly of no use.
      d2name[td->getCanonicalDecl()].type_hash = typeid(td).hash_code();
  return true;
}

bool Collector::VisitEnumConstantDecl(EnumConstantDecl *ecd) {
  used.insert(CachedHashStringRef(ecd->getName()));
  if (!sm.isWrittenInMainFile(ecd->getLocation()))
    return true;
#ifndef NDEBUG
  errs() << "in VisitEnumConstantDecl, typeid: " << typeid(ecd).name() << "\n",
#endif
      d2name[ecd->getCanonicalDecl()].type_hash =
          typeid(ecd->getCanonicalDecl()).hash_code();
  return true;
}
