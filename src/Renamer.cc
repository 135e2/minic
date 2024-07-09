#include "Renamer.h"

bool Renamer::VisitFunctionDecl(FunctionDecl *fd) {
  if (!sm.isWrittenInMainFile(fd->getLocation()))
    return true;
  auto *canon = fd->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(fd->getLocation()), dmd.name);
  });
  return true;
}

// CXXConstructorDecl is a special kind of FunctionDecl/CXXMethodDecl that
// needs to be renamed to its parent class
bool Renamer::VisitCXXConstructorDecl(CXXConstructorDecl *ccd) {
  if (!sm.isWrittenInMainFile(ccd->getLocation()))
    return true;
  // the canon decl should be the same as its class's (in other words,
  // its parent's)
  auto *canon = ccd->getParent()->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(ccd->getLocation()), dmd.name);
    for (CXXCtorInitializer *cci : ccd->inits())
      VisitCXXCtorInitializer(cci);
    for (ParmVarDecl *param : ccd->parameters())
      VisitVarDecl(param);
  });

  return true;
}

// And constructor leads to another oddity: C++ base/member initializer
bool Renamer::VisitCXXCtorInitializer(CXXCtorInitializer *cci) {
  if (!sm.isWrittenInMainFile(cci->getSourceLocation()))
    return true;
  auto *canon = cci->getMember()->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(cci->getSourceLocation()), dmd.name);
  });
  return true;
}

bool Renamer::VisitMemberExpr(MemberExpr *me) {
  if (!sm.isWrittenInMainFile(me->getExprLoc()))
    return true;

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
      errs() << "\nFound parent is specialization.\n";
#endif
      auto *act_cd = td->getTemplatedDecl();
      for (auto *d : act_cd->decls()) {
        ValueDecl *act_md = dynamic_cast<ValueDecl *>(d);
        if (!act_md)
          continue;
        if (IdentifierInfo *act_id = act_md->getIdentifier();
            act_id == md->getIdentifier()) {
#ifndef NDEBUG
          errs() << "Found corresponding member:\n", act_md->dumpColor();
#endif
          md = act_md;
          break;
        }
      }
    }

  auto *canon = md->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(me->getExprLoc()), dmd.name);
  });
  return true;
}

bool Renamer::VisitVarDecl(VarDecl *vd) {
  if (!sm.isWrittenInMainFile(vd->getLocation()))
    return true;
  auto *canon = vd->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(vd->getLocation()), dmd.name);
  });
  return true;
}

bool Renamer::VisitDeclRefExpr(DeclRefExpr *dre) {
  Decl *d = dre->getDecl();
  if (!sm.isWrittenInMainFile(d->getLocation()))
    return true;
  if (!(isa<FunctionDecl>(d) || isa<VarDecl>(d) || isa<FieldDecl>(d) ||
        isa<TypeDecl>(d) || isa<EnumConstantDecl>(d)))
    return true;
  auto *canon = d->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(dre->getSourceRange()), dmd.name);
  });

  if (FunctionDecl *fd = d->getAsFunction()) {
    if (auto *tipfd = fd->getTemplateInstantiationPattern()) {
#ifndef NDEBUG
      errs() << "\nIn VisitDeclRefExpr, found template func:\n",
          tipfd->dumpColor();
#endif
      canon = tipfd->getCanonicalDecl();
      lookup(canon, [&](DeclMapData &dmd) {
        // only replace the function template name
        replace(CharSourceRange::getTokenRange(
                    SourceRange(dre->getBeginLoc(),
                                dre->getLAngleLoc().isValid()
                                    ? dre->getLAngleLoc().getLocWithOffset(-1)
                                    : dre->getEndLoc())),
                dmd.name);
      });
    }
  }
  return true;
}

bool Renamer::VisitFieldDecl(FieldDecl *fd) {
  if (!sm.isWrittenInMainFile(fd->getLocation()))
    return true;
  auto *canon = fd->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(fd->getLocation()), dmd.name);
  });
  return true;
}

bool Renamer::VisitTypeDecl(TypeDecl *td) {
  if (!sm.isWrittenInMainFile(td->getLocation()))
    return true;
  auto *canon = td->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(td->getLocation()), dmd.name);
  });
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
    errs() << "\n", tst->dump(errs(), ctx);
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
      lookup(ctd->getTemplatedDecl(), [&](DeclMapData &dmd) {
#ifndef NDEBUG
        errs() << "Found underlying name: " << dmd.name << "\n";
#endif
        // We only need to replace its template name here (w/o template args).
        replace(CharSourceRange::getTokenRange(tstl.getTemplateNameLoc()),
                dmd.name);
      });
    }
  }

  lookup(td, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(tl.getSourceRange()), dmd.name);
  });

  return true;
}

bool Renamer::VisitEnumConstantDecl(EnumConstantDecl *ecd) {
  if (!sm.isWrittenInMainFile(ecd->getLocation()))
    return true;
  auto *canon = ecd->getCanonicalDecl();
  lookup(canon, [&](DeclMapData &dmd) {
    replace(CharSourceRange::getTokenRange(ecd->getLocation()), dmd.name);
  });
  return true;
}
