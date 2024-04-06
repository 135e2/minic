#include <err.h>
#include <regex>
#include <string>
#include <vector>

#include "postProcess.h"

static const std::vector<std::string> OPS = {
    "+",  "-", "*",  "/",  "%",  "++",   "--",  "+=", "-=", "*=", "/=",
    "%=", "=", "==", "!=", "&&", "||",   "!",   "&",  "|",  "^",  "<<",
    ">>", "<", ">",  "<=", ">=", "<<=",  ">>=", "&=", "|=", "^=", ",",
    "(",  ")", "{",  "}",  ";",  "else", ":",   "::", "?"};

static const std::vector<std::string> ESC_OPS = {
    "\\+",  "\\-", "\\*",  "/",    "%",    "\\+\\+", "\\-\\-", "\\+=",   "\\-=",
    "\\*=", "/=",  "%=",   "=",    "==",   "!=",     "\\&\\&", "\\|\\|", "!",
    "\\&",  "\\|", "\\^",  "<<",   ">>",   "<",      ">",      "<=",     ">=",
    "<<=",  ">>=", "\\&=", "\\|=", "\\^=", ",",      "\\(",    "\\)",    "\\{",
    "\\}",  ";",   "else", ":",    "::",   "\\?"};

static void removeComments(std::string &input) {
  std::string::size_type m = 0, n = 0;

  while ((m = input.find("/*", n)) != std::string::npos) {
    if ((n = input.find("*/", m)) == std::string::npos)
      errx(2, "failed to parse comments");
    input.replace(m, n + 2 - m, "");
  }

  m = 0, n = 0;

  while ((m = input.find("//", n)) != std::string::npos) {
    if ((n = input.find("\n", m)) == std::string::npos)
      errx(2, "failed to parse comments");
    input.replace(m, n - m, "");
  }
}

static void minifyOps(std::string &input) {
  for (int i = 0; i < OPS.size(); i++) {
    // Construct the regex pattern to strip away spaces around the operator
    std::string toCompile = " *" + ESC_OPS[i] + " *";
    std::regex regexPattern(toCompile);

    // Prepare replacement string
    std::string repl = OPS[i];

    // Lambda function to apply the regex and replacement
    input = std::regex_replace(input, regexPattern, repl);

    // Handle special "else if"
    input = std::regex_replace(input, std::regex("elseif"), "else if");
  }
}

static void stripNewLine(std::string &input) {
  std::istringstream iss(input);
  std::string line;
  std::string result;
  // this flag is needed to handle trailing backslashes in macros.
  bool lastLineIsMacro = false;

  while (std::getline(iss, line)) {
    if (line.empty())
      // Skip empty lines
      continue;
    if (line.back() == '\\') {
      // Remove trailing backslash
      // DO NOT start a newline
      line.pop_back();
      result += line;
      if (!lastLineIsMacro)
        lastLineIsMacro = (line.front() == '#');
      continue;
    }
    if (line.front() == '#') {
      // Keep preprocessor's '\n'
      result += line + '\n';
      continue;
    }
    if (lastLineIsMacro) {
      // We are in a macro and this line ends w/o a trailing backslash,
      // which means now it's the end of the macro
      result += line + '\n';
      lastLineIsMacro = false;
      continue;
    }

    // Not special at all
    // Remove '\n' from other regular lines
    result += line;
  }

  input = result;
}

void postProcess(std::string &code) {
  removeComments(code);
  minifyOps(code);
  stripNewLine(code);
}