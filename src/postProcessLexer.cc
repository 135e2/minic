#include <clang/Basic/IdentifierTable.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <err.h>

#include "postProcessLexer.h"

SourceManager *postProcessLexer::SM;
Preprocessor *postProcessLexer::PP;

void postProcessLexer::PrintMacroDefinition(const IdentifierInfo &II,
                                            const MacroInfo &MI,
                                            raw_ostream *OS) {
  *OS << "#define " << II.getName();

  if (MI.isFunctionLike()) {
    *OS << '(';
    if (!MI.param_empty()) {
      MacroInfo::param_iterator AI = MI.param_begin(), E = MI.param_end();
      for (; AI + 1 != E; ++AI) {
        *OS << (*AI)->getName();
        *OS << ',';
      }

      // Last argument.
      if ((*AI)->getName() == "__VA_ARGS__")
        *OS << "...";
      else
        *OS << (*AI)->getName();
    }

    if (MI.isGNUVarargs())
      *OS << "..."; // #define foo(x...)

    *OS << ')';
  }

  // GCC always emits a space, even if the macro body is empty.  However, do not
  // want to emit two spaces if the first token has a leading space.
  if (MI.tokens_empty() || !MI.tokens_begin()->hasLeadingSpace())
    *OS << ' ';

  SmallString<128> SpellingBuffer;
  for (const auto &T : MI.tokens()) {
    if (T.hasLeadingSpace())
      *OS << ' ';

    *OS << PP->getSpelling(T, SpellingBuffer) << "\n";
  }
}

void postProcessLexer::MacroCallback::InclusionDirective(
    SourceLocation HashLoc, const Token &IncludeTok, StringRef FileName,
    bool IsAngled, CharSourceRange FilenameRange, OptionalFileEntryRef File,
    StringRef SearchPath, StringRef RelativePath, const Module *SuggestedModule,
    bool ModuleImported, SrcMgr::CharacteristicKind FileType) {
  if (SM->getFileID(HashLoc) != SM->getMainFileID())
    return;
  outs() << "#include directive found: " << FileName.str()
         << (IsAngled ? " (angled)" : " (quoted)") << "\n";
};

void postProcessLexer::MacroCallback::MacroDefined(const Token &MacroNameTok,
                                                   const MacroDirective *MD) {
  const MacroInfo *MI = MD->getMacroInfo();
  if ( // Ignore __FILE__ etc.
      MI->isBuiltinMacro())
    return;

  SourceLocation DefLoc = MI->getDefinitionLoc();
  if (SM->getFileID(DefLoc) != SM->getMainFileID())
    return;

  PrintMacroDefinition(*MacroNameTok.getIdentifierInfo(), *MI, &outs());
};

// If a token is alphanumeric (e.g., keywords, identifiers, or literals).
// Used to ensure we don't accidentally merge tokens like 'int' and 'x'
// into 'intx'.
static inline bool isAlphanumeric(tok::TokenKind k) {
  return tok::isAnyIdentifier(k) || tok::isLiteral(k);
}

std::string postProcessLexer::minify(const std::string &code,
                                     const ArrayRef<const char *> &args) {
  auto ci = buildCompilerInvocation(args);
  if (!ci)
    errx(1, "failed to build CompilerInvocation");

  IgnoringDiagConsumer dc;
#if LLVM_VERSION_MAJOR >= 21
  auto inst = std::make_unique<CompilerInstance>(
      std::move(ci), std::make_shared<PCHContainerOperations>());
#else
  auto inst = std::make_unique<CompilerInstance>(
      std::make_shared<PCHContainerOperations>());
  inst->setInvocation(std::move(ci));
#endif
#if LLVM_VERSION_MAJOR == 21
  inst->createDiagnostics(*llvm::vfs::getRealFileSystem(),
#else
  inst->createDiagnostics(
#endif
                          &dc, false);
  inst->getDiagnostics().setIgnoreAllWarnings(true);
  inst->setTarget(TargetInfo::CreateTargetInfo(
      inst->getDiagnostics(), inst->getInvocation().getTargetOpts()));
  if (!inst->hasTarget())
    errx(1, "hasTarget returns false");
  inst->createFileManager(
#if LLVM_VERSION_MAJOR < 22
      llvm::vfs::getRealFileSystem()
#endif
  );
  inst->setSourceManager(
      new SourceManager(inst->getDiagnostics(), inst->getFileManager(), true));

  SourceManager &SourceMgr = inst->getSourceManager();
  std::unique_ptr<llvm::MemoryBuffer> buffer =
      llvm::MemoryBuffer::getMemBuffer(code);
  FileID FID = SourceMgr.createFileID(std::move(buffer));

  // Use a raw lexer to prevent preprocessor directives (#include, #define)
  // from being expanded and removed.
  Lexer L(FID, SourceMgr.getBufferOrFake(FID), SourceMgr, inst->getLangOpts());

  Token token;
  std::string output;
  bool inDirective = false;
  Token lastToken;
  bool hasLastToken = false;

  while (true) {
    bool isDone = L.LexFromRawLexer(token);
    if (token.is(tok::eof))
      break;

    bool isInValid = false;
    std::string text =
        L.getSpelling(token, SourceMgr, inst->getLangOpts(), &isInValid);

    std::string debugText = text;
    if (token.hasLeadingSpace())
      debugText.insert(0, " ");
    outs() << token.getName() << " " << debugText << (isInValid ? " in" : " ")
           << "valid\n";

    // strip comments
    if (token.is(tok::comment)) {
      if (isDone)
        break;
      continue;
    }

    // preserve \n for preprocessor directives
    if (token.isAtStartOfLine()) {
      if (inDirective) {
        output += "\n";
        inDirective = false;
      }
      // a hash directive
      if (token.is(tok::hash)) {
        if (!output.empty() && output.back() != '\n')
          output += "\n";
        inDirective = true;
      }
    }

    // determine if a space is required before this token
    bool needsSpace = false;
    if (token.hasLeadingSpace() && hasLastToken) {
      tok::TokenKind p = lastToken.getKind();
      tok::TokenKind c = token.getKind();
      if (isAlphanumeric(p) && isAlphanumeric(c)) {
        needsSpace = true;
      } else if (inDirective && c == tok::l_paren) {
        // preserve space to avoid turning object-like macros into function-like
        needsSpace = true;
      }
    }

    if (needsSpace && !output.empty() && output.back() != '\n' &&
        output.back() != ' ') {
      output += " ";
    }

    if (!isInValid) {
      output += text;
    }

    lastToken = token;
    hasLastToken = true;
    if (isDone)
      break;
  }

  return output;
};
