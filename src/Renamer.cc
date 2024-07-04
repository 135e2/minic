#include "Renamer.h"

bool Renamer::VisitFunctionDecl(FunctionDecl *fd) {
  auto *canon = fd->getCanonicalDecl();
  auto it = d2name.find(canon);
  if (it != d2name.end())
    replace(CharSourceRange::getTokenRange(fd->getLocation()), it->second.name);
  return true;
}

// CXXConstructorDecl is a special kind of FunctionDecl/CXXMethodDecl that
// needs to be renamed to its parent class
bool Renamer::VisitCXXConstructorDecl(CXXConstructorDecl *ccd) {
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
bool Renamer::VisitCXXCtorInitializer(CXXCtorInitializer *cci) {
  auto *canon = cci->getMember()->getCanonicalDecl();
  auto it = d2name.find(canon);
  if (it != d2name.end())
    replace(CharSourceRange::getTokenRange(cci->getSourceLocation()),
            it->second.name);
  return true;
}

bool Renamer::VisitMemberExpr(MemberExpr *me) {
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

bool Renamer::VisitVarDecl(VarDecl *vd) {
  auto *canon = vd->getCanonicalDecl();
  auto it = d2name.find(canon);
  if (it != d2name.end())
    replace(CharSourceRange::getTokenRange(vd->getLocation()), it->second.name);
  return true;
}

bool Renamer::VisitDeclRefExpr(DeclRefExpr *dre) {
  Decl *d = dre->getDecl();
  if (!(isa<FunctionDecl>(d) || isa<VarDecl>(d) || isa<FieldDecl>(d) ||
        isa<TypeDecl>(d) || isa<EnumConstantDecl>(d))) {
    return true;
  }
  auto it = d2name.find(d->getCanonicalDecl());
  if (it != d2name.end())
    replace(CharSourceRange::getTokenRange(SourceRange(dre->getSourceRange())),
            it->second.name);
  return true;
}

bool Renamer::VisitFieldDecl(FieldDecl *fd) {
  auto *canon = fd->getCanonicalDecl();
  auto it = d2name.find(canon);
  if (it != d2name.end())
    replace(CharSourceRange::getTokenRange(fd->getLocation()), it->second.name);
  return true;
}

bool Renamer::VisitTypeDecl(TypeDecl *d) {
  auto *canon = d->getCanonicalDecl();
  if (auto it = d2name.find(canon); it != d2name.end())
    replace(CharSourceRange::getTokenRange(d->getLocation()), it->second.name);
  return true;
}

bool Renamer::VisitTypeLoc(TypeLoc tl) {
  TypeDecl *td = nullptr;
  if (const TagTypeLoc ttl = tl.getAs<TagTypeLoc>())
    td = ttl.getDecl()->getCanonicalDecl();
  if (const TypedefTypeLoc tdl = tl.getAs<TypedefTypeLoc>())
    td = tdl.getTypedefNameDecl()->getCanonicalDecl();
  if (const TemplateTypeParmTypeLoc ttptl = tl.getAs<TemplateTypeParmTypeLoc>())
    td = ttptl.getDecl();
  if (auto it = d2name.find(td); it != d2name.end())
    replace(CharSourceRange::getTokenRange(tl.getSourceRange()),
            it->second.name);
  return true;
}

bool Renamer::VisitEnumConstantDecl(EnumConstantDecl *ecd) {
  auto *canon = ecd->getCanonicalDecl();
  if (auto it = d2name.find(canon); it != d2name.end())
    replace(CharSourceRange::getTokenRange(ecd->getLocation()),
            it->second.name);
  return true;
}
