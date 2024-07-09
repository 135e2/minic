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
  if (!sm.isWrittenInMainFile(me->getExprLoc())) {
    return true;
  }

  auto *md = me->getMemberDecl();

  /* If its parent is a specialized class, obtain renaming info from its
   * original template class.
   * Matching the member name is not really an elegant solution,
   * but we can barely learn anything from an unresolved template AST.
   */
  if (auto *ctsd =
          getParent<ClassTemplateSpecializationDecl, decltype(*md)>(*md))
    if (auto *td =
            ctsd->getInstantiatedFrom().dyn_cast<ClassTemplateDecl *>()) {
#ifndef NDEBUG
      outs() << "\nFound parent is specialization.\n";
#endif
      auto *act_cd = td->getTemplatedDecl();
      for (auto *d : act_cd->decls()) {
        ValueDecl *act_md = dynamic_cast<ValueDecl *>(d);
        if (!act_md)
          continue;
        if (IdentifierInfo *act_id = act_md->getIdentifier();
            act_id == md->getIdentifier()) {
#ifndef NDEBUG
          outs() << "Found corresponding member:\n", act_md->dumpColor();
#endif
          md = act_md;
          break;
        }
      }
    }

  auto *canon = md->getCanonicalDecl();
  auto it = d2name.find(canon);
  if (it != d2name.end()) {
    replace(CharSourceRange::getTokenRange(me->getExprLoc()), it->second.name);
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

  if (sm.isWrittenInMainFile(d->getLocation()))
    if (FunctionDecl *fd = d->getAsFunction()) {
      if (auto *tipfd = fd->getTemplateInstantiationPattern()) {
        outs() << "In VisitDeclRefExpr, found Template func:\n",
            tipfd->dumpColor();
        if (auto it = d2name.find(tipfd->getCanonicalDecl());
            it != d2name.end())
          // only replace the function template name
          replace(CharSourceRange::getTokenRange(
                      SourceRange(dre->getBeginLoc(),
                                  dre->getLAngleLoc().isValid()
                                      ? dre->getLAngleLoc().getLocWithOffset(-1)
                                      : dre->getEndLoc())),
                  it->second.name);
      }
    }
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
  if (!sm.isWrittenInMainFile(tl.getBeginLoc()))
    return true;

  TypeDecl *td = nullptr;
  if (const TagTypeLoc ttl = tl.getAs<TagTypeLoc>())
    td = ttl.getDecl();
  if (const TypedefTypeLoc tdl = tl.getAs<TypedefTypeLoc>())
    td = tdl.getTypedefNameDecl()->getCanonicalDecl();
  if (const TemplateTypeParmTypeLoc ttptl = tl.getAs<TemplateTypeParmTypeLoc>())
    td = ttptl.getDecl();
  if (const InjectedClassNameTypeLoc icntl =
          tl.getAs<InjectedClassNameTypeLoc>())
    td = icntl.getDecl();
  if (const TemplateSpecializationTypeLoc tstl =
          tl.getAs<TemplateSpecializationTypeLoc>()) {
    auto *tst = tstl.getTypePtr();
#ifndef NDEBUG
    tst->dump(outs(), ctx);
#endif
    if (const RecordType *rt = tst->getAs<RecordType>()) {
      if (!sm.isWrittenInMainFile(rt->getDecl()->getLocation()))
        return true;

      auto *ctsd =
          dynamic_cast<ClassTemplateSpecializationDecl *>(rt->getDecl());
      if (!ctsd)
        return true;

      auto *ctd = ctsd->getInstantiatedFrom().dyn_cast<ClassTemplateDecl *>();
      if (!ctd)
        return true;
      if (auto it = d2name.find(ctd->getTemplatedDecl()); it != d2name.end()) {
#ifndef NDEBUG
        outs() << "Found underlying name: " << it->second.name << "\n\n";
#endif
        // We only need to replace its template name here (w/o template args).
        replace(CharSourceRange::getTokenRange(tstl.getTemplateNameLoc()),
                it->second.name);
      }
    }
  }

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
