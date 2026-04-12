#pragma once

#include "Utils.h"

class postProcessLexer {
public:
  struct MacroCallback : PPCallbacks {
    void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                            StringRef FileName, bool IsAngled,
                            CharSourceRange FilenameRange,
                            OptionalFileEntryRef File, StringRef SearchPath,
                            StringRef RelativePath,
                            const Module *SuggestedModule, bool ModuleImported,
                            SrcMgr::CharacteristicKind FileType) override;
    void MacroDefined(const Token &MacroNameTok,
                      const MacroDirective *MD) override;
  };
  static std::string minify(const std::string &code,
                            const ArrayRef<const char *> &args);

private:
  static SourceManager *SM;
  static Preprocessor *PP;
  static void PrintMacroDefinition(const IdentifierInfo &II,
                                   const MacroInfo &MI, raw_ostream *OS);
};
