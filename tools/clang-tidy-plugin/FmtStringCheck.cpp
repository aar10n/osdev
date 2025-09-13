//
// Created by Aaron Gill-Braun on 2025-09-8.
//

#include "FmtStringCheck.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/TargetInfo.h"
#include <cctype>
#include <sstream>
#include <algorithm>

using namespace clang::ast_matchers;

static inline char FrontChar(llvm::StringRef str) {
  return str.empty() ? '\0' : str.front();
}

namespace clang::tidy::osdev {

std::map<std::string, FmtStringCheck::ArgumentType> FmtStringCheck::BuiltinFormatTypes = {
  {"d", ArgumentType::Int},                 // int (decimal)
//  {"i", ArgumentType::Int},                 // int (decimal)
  {"u", ArgumentType::UnsignedInt},         // unsigned (decimal)
  {"b", ArgumentType::UnsignedInt},         // unsigned (binary)
  {"o", ArgumentType::UnsignedInt},         // unsigned (octal)
  {"u", ArgumentType::UnsignedInt},         // unsigned (decimal)
  {"x", ArgumentType::UnsignedInt},         // unsigned (hex, lowercase)
  {"X", ArgumentType::UnsignedInt},         // unsigned (hex, uppercase)
  {"f", ArgumentType::Double},              // double (floating point)
  {"F", ArgumentType::Double},              // double (floating point, uppercase)
//  {"e", ArgumentType::Double},              // floating point (scientific)
//  {"E", ArgumentType::Double},              // floating point (scientific, uppercase)
//  {"g", ArgumentType::Double},              // floating point (shortest representation)
//  {"G", ArgumentType::Double},              // floating point (shortest representation, uppercase)
//  {"a", ArgumentType::Double},              // floating point (hex, lowercase)
//  {"A", ArgumentType::Double},              // floating point (hex, uppercase)
  {"p", ArgumentType::PointerType},       // pointer
  {"s", ArgumentType::CString},           // string
  {"c", ArgumentType::Char},              // character
  {"M", ArgumentType:: SizeType},          // memory quantity

  {"hhd", ArgumentType::Char},              // signed char (decimal)
//  {"hhi", ArgumentType::Char},              // signed char (decimal)
  {"hhu", ArgumentType::UnsignedChar},      // unsigned char (decimal)
  {"hhb", ArgumentType::UnsignedChar},      // unsigned char (binary)
  {"hho", ArgumentType::UnsignedChar},      // unsigned char (octal)
  {"hhx", ArgumentType::UnsignedChar},      // unsigned char (hex, lowercase)
  {"hhX", ArgumentType::UnsignedChar},      // unsigned char (hex, uppercase)

  {"hd", ArgumentType::Short},              // signed short (decimal)
//  {"hi", ArgumentType::Short},              // signed short (decimal)
  {"hu", ArgumentType::UnsignedShort},      // unsigned short (decimal)
  {"hb", ArgumentType::UnsignedShort},      // unsigned short (binary)
  {"ho", ArgumentType::UnsignedShort},      // unsigned short (octal)
  {"hx", ArgumentType::UnsignedShort},      // unsigned short (hex, lowercase)
  {"hX", ArgumentType::UnsignedShort},      // unsigned short (hex, uppercase)

  {"ld", ArgumentType::Long},               // signed long (decimal)
//  {"li", ArgumentType::Long},               // signed long (decimal)
  {"lu", ArgumentType::UnsignedLong},       // unsigned long (decimal)
  {"lb", ArgumentType::UnsignedLong},       // unsigned long (binary)
  {"lo", ArgumentType::UnsignedLong},       // unsigned long (octal)
  {"lx", ArgumentType::UnsignedLong},       // unsigned long (hex, lowercase)
  {"lX", ArgumentType::UnsignedLong},       // unsigned long (hex, uppercase)

  {"lld", ArgumentType::LongLong},          // signed long long (decimal)
//  {"lli", ArgumentType::LongLong},          // signed long long (decimal)
  {"llu", ArgumentType::UnsignedLongLong},  // unsigned long long (decimal)
  {"llb", ArgumentType::UnsignedLongLong},  // unsigned long long (binary)
  {"llo", ArgumentType::UnsignedLongLong},  // unsigned long long (octal)
  {"llx", ArgumentType::UnsignedLongLong},  // unsigned long long (hex, lowercase)
  {"llX", ArgumentType::UnsignedLongLong},  // unsigned long long (hex, uppercase)

//  {"jd", ArgumentType::LongLong},           // intmax_t (decimal)
//  {"ji", ArgumentType::LongLong},           // intmax_t (decimal)
//  {"ju", ArgumentType::UnsignedLongLong},   // uintmax_t (decimal)
//  {"jb", ArgumentType::UnsignedLongLong},   // uintmax_t (binary)
//  {"jo", ArgumentType::UnsignedLongLong},   // uintmax_t (octal)
//  {"jx", ArgumentType::UnsignedLongLong},   // uintmax_t (hex, lowercase)
//  {"jX", ArgumentType::UnsignedLongLong},   // uintmax_t (hex, uppercase)

  {"zd", ArgumentType::SignedSizeType},     // ssize_t (decimal)
  {"zu", ArgumentType::SizeType},           // size_t (decimal)
  {"zb", ArgumentType::SizeType},           // size_t (binary)
  {"zo", ArgumentType::SizeType},           // size_t (octal)
  {"zx", ArgumentType::SizeType},           // size_t (hex, lowercase)
  {"zX", ArgumentType::SizeType},           // size_t (hex, uppercase)

  {"td", ArgumentType::SignedSizeType},     // ptrdiff_t (decimal)
//  {"ti", ArgumentType::SignedSizeType},     // ptrdiff_t (decimal)
  {"tu", ArgumentType::SizeType},           // ptrdiff_t (decimal)
  {"tb", ArgumentType::SizeType},           // ptrdiff_t (binary)
  {"to", ArgumentType::SizeType},           // ptrdiff_t (octal)
  {"tx", ArgumentType::SizeType},           // ptrdiff_t (hex, lowercase)
  {"tX", ArgumentType::SizeType},           // ptrdiff_t (hex, uppercase)

//  {"Lf", ArgumentType::Double},             // long double
//  {"LF", ArgumentType::Double},             // long double (uppercase)
//  {"Le", ArgumentType::Double},             // long double (scientific)
//  {"LE", ArgumentType::Double},             // long double (scientific, uppercase)
//  {"Lg", ArgumentType::Double},             // long double (shortest representation)
//  {"LG", ArgumentType::Double},             // long double (shortest representation, uppercase)
//  {"La", ArgumentType::Double},             // long double (hex, lowercase)
//  {"LA", ArgumentType::Double},             // long double (hex, uppercase)
};

std::map<std::string, FmtStringCheck::ArgumentType> FmtStringCheck::OptionCustomTypes = {
    {"char", ArgumentType::Char},
    {"uchar", ArgumentType::UnsignedChar},
    {"short", ArgumentType::Short},
    {"ushort", ArgumentType::UnsignedShort},
    {"int", ArgumentType::Int},
    {"uint", ArgumentType::UnsignedInt},
    {"long", ArgumentType::Long},
    {"ulong", ArgumentType::UnsignedLong},
    {"longlong", ArgumentType::LongLong},
    {"ulonglong", ArgumentType::UnsignedLongLong},
    {"size_t", ArgumentType::SizeType},
    {"ssize_t", ArgumentType::SignedSizeType},
    {"double", ArgumentType::Double},
    {"pointer", ArgumentType::PointerType},
    {"void*", ArgumentType::VoidPointer},
    {"cstring", ArgumentType::CString}
};

std::map<FmtStringCheck::ArgumentType, std::string> FmtStringCheck::TypeCustomOptions = {
    {ArgumentType::Char, "char"},
    {ArgumentType::UnsignedChar, "uchar"},
    {ArgumentType::Short, "short"},
    {ArgumentType::UnsignedShort, "ushort"},
    {ArgumentType::Int, "int"},
    {ArgumentType::UnsignedInt, "uint"},
    {ArgumentType::Long, "long"},
    {ArgumentType::UnsignedLong, "ulong"},
    {ArgumentType::LongLong, "longlong"},
    {ArgumentType::UnsignedLongLong, "ulonglong"},
    {ArgumentType::SizeType, "size_t"},
    {ArgumentType::SignedSizeType, "ssize_t"},
    {ArgumentType::Double, "double"},
    {ArgumentType::PointerType, "pointer"},
    {ArgumentType::VoidPointer, "void*"},
    {ArgumentType::CString, "cstring"}
    // Argument::Custom is not a valid option type
    // Argument::CustomStructType is handled separately
    // Argument::CustomStructPType is handled separately
    // Argument::CustomEnumType is handled separately
    // Argument::Unknown is not a valid option type
};


FmtStringCheck::FmtStringCheck(StringRef Name, ClangTidyContext *Context) : ClangTidyCheck(Name, Context) {
  // Parse any custom types specified
  // The CustomTypes string is expected in a format like:
  //   "typeFormat=type[,typeFormat=type[,...]]"
  SmallVector<StringRef, 8> CustomTypePairs;
  Options.get("CustomTypes", "").split(CustomTypePairs, ',');
  for (StringRef Pair : CustomTypePairs) {
    SmallVector<StringRef, 2> Parts;
    Pair.split(Parts, '=');
    if (Parts.size() != 2) {
      llvm::errs() << "invalid CustomTypes entry: " << Pair << "\n";
      continue;
    }

    StringRef TypeFormat = Parts[0].trim();
    StringRef TypeStr = Parts[1].trim();
    if (TypeFormat.empty() || TypeStr.empty()) {
      llvm::errs() << "invalid CustomTypes entry: " << Pair << "\n";
      continue;
    }

    CustomType CT;
    CT.typeFormat = TypeFormat.str();
    if (OptionCustomTypes.count(TypeStr.str())) {
      CT.type = OptionCustomTypes[TypeStr.str()];
      CT.name = std::nullopt;
    } else if (TypeStr.starts_with("struct ")) {
      ArgumentType ArgType = ArgumentType::CustomStructType;
      TypeStr = TypeStr.drop_front(7).trim();
      if (TypeStr.ends_with("*")) {
        ArgType = ArgumentType::CustomStructPType;
        TypeStr = TypeStr.drop_back(1).trim();
      }

      // Validate struct name
      if (std::all_of(TypeStr.begin(), TypeStr.end(), [](char c){ return std::isalnum(c) || c == '_'; }) == false) {
        llvm::errs() << "invalid struct name in CustomTypes entry: " << TypeStr << "\n";
        continue;
      }

      CT.type = ArgType;
      CT.name = TypeStr.str();
    } else if (TypeStr.starts_with("enum ")) {
      TypeStr = TypeStr.drop_front(5).trim();

      // Validate enum name
      if (std::all_of(TypeStr.begin(), TypeStr.end(), [](char c){ return std::isalnum(c) || c == '_'; }) == false) {
        llvm::errs() << "invalid enum name in CustomTypes entry: " << TypeStr << "\n";
        continue;
      }

      CT.type = ArgumentType::CustomEnumType;
      CT.name = TypeStr.str();
    } else {
      llvm::errs() << "unknown type in CustomTypes entry: " << Pair << "\n";
      continue; // Unknown type
    }

    CustomTypes[CT.typeFormat] = CT;
  }

  // Parse the list of format functions
  // The Functions string is expected in a format like:
  //   "funcName[:formatArgIndex[:firstVarArgIndex]][,funcName[:...]]"
  SmallVector<StringRef, 8> Functions;
  StringRef FunctionsStr = Options.get("Functions", "kprintf:1:2,ksprintf:2:3,ksnprintf:3:4");
  FunctionsStr.split(Functions, ',');
  for (StringRef Func : Functions) {
    // Parse function name and optional positions
    SmallVector<StringRef, 3> Parts;
    Func.split(Parts, ':');

    if (Parts.size() >= 1) {
      FormatFunction FF;
      FF.name = Parts[0].str();
      FF.formatStringArgIndex = 0; // Default to first argument
      FF.firstVarArgIndex = 1;     // Default to second argument

      if (Parts.size() >= 2) {
        int idx;
        if (!Parts[1].getAsInteger(10, idx) && idx > 0) {
          FF.formatStringArgIndex = idx - 1;  // Make it 0-based
          FF.firstVarArgIndex = idx;          // Default first var arg to next argument
        }
      }
      if (Parts.size() >= 3) {
        int idx;
        if (!Parts[2].getAsInteger(10, idx) && idx > 0) {
          FF.firstVarArgIndex = idx - 1;      // Make it 0-based
        }
      }

      FormatFunctions[FF.name] = FF;
    }
  }
}

void FmtStringCheck::storeOptions(ClangTidyOptions::OptionMap &Opts) {
  std::string CustomTypeList;
  for (const auto &[_, CustomType] : CustomTypes) {
    if (CustomType.type == ArgumentType::Unknown)
      continue;

    if (!CustomTypeList.empty()) CustomTypeList += ",";
    if (CustomType.type == ArgumentType::CustomStructType) {
      CustomTypeList += CustomType.typeFormat + "=struct " + CustomType.name.value();
    } else if (CustomType.type == ArgumentType::CustomStructPType) {
      CustomTypeList += CustomType.typeFormat + "=struct " + CustomType.name.value() + "*";
    } else if (CustomType.type == ArgumentType::CustomEnumType) {
      CustomTypeList += CustomType.typeFormat + "=enum " + CustomType.name.value();
    } else {
      CustomTypeList += CustomType.typeFormat + "=" + TypeCustomOptions[CustomType.type];
    }
  }
  Options.store(Opts, "CustomTypes", CustomTypeList);

  std::string FuncList;
  for (const auto &[_, Func] : FormatFunctions) {
    if (!FuncList.empty()) FuncList += ",";
    FuncList += Func.name + ":" + std::to_string(Func.formatStringArgIndex + 1) + ":" + std::to_string(Func.firstVarArgIndex + 1);
  }
  Options.store(Opts, "Functions", FuncList);
}

void FmtStringCheck::registerMatchers(MatchFinder *Finder) {
  // Build matcher for all functions in FormatFunctions
  if (!FormatFunctions.empty()) {
    auto iter = FormatFunctions.begin();
    auto FunctionMatcher = hasName(iter->first);
    ++iter;

    while (iter != FormatFunctions.end()) {
      FunctionMatcher = anyOf(FunctionMatcher, hasName(iter->first));
      ++iter;
    }

    // Register the matcher
    Finder->addMatcher(
      callExpr(callee(functionDecl(FunctionMatcher))).bind("call"),
      this
    );
  }

  // Match calls to functions with custom fmt_format annotations
  Finder->addMatcher(
    callExpr(callee(functionDecl(
      hasAttr(attr::Annotate)
    ))).bind("call_with_annotate"),
    this
  );
}

void FmtStringCheck::check(const MatchFinder::MatchResult &Result) {
  const CallExpr *Call = nullptr;
  const FunctionDecl *FD = nullptr;
  int FormatArgPos = -1;
  int FirstVarArgPos = -1;

  if ((Call = Result.Nodes.getNodeAs<CallExpr>("call_with_annotate"))) {
    // Check for calls with a custom `annotate` attribute: __attribute__((annotate("fmt_format:3:4")))
    FD = Call->getDirectCallee();
    if (!FD) return;

    for (const auto *Attr : FD->getAttrs()) {
      if (const auto *AnnAttr = dyn_cast<AnnotateAttr>(Attr)) {
        StringRef AnnotationStr = AnnAttr->getAnnotation();
        if (AnnotationStr.starts_with("fmt_format:")) {
          // Parse "fmt_format:3:4" format
          StringRef Params = AnnotationStr.drop_front(11); // Drop "fmt_format:"
          size_t ColonPos = Params.find(':');
          if (ColonPos != StringRef::npos) {
            StringRef FormatIdxStr = Params.take_front(ColonPos);
            StringRef FirstArgStr = Params.drop_front(ColonPos + 1);

            int FormatIdx;
            int FirstArg;
            if (!FormatIdxStr.getAsInteger(10, FormatIdx) &&
                !FirstArgStr.getAsInteger(10, FirstArg)) {
              FormatArgPos = FormatIdx - 1;  // Make it 0-based
              FirstVarArgPos = FirstArg - 1;
              break;
            }
          }
        }
      }
    }
    if (FormatArgPos == -1) {
      return; // No valid annotation found
    }
  } else if ((Call = Result.Nodes.getNodeAs<CallExpr>("call"))) {
    // Check if this is a known format function
    FD = Call->getDirectCallee();
    if (!FD) return;

    auto It = FormatFunctions.find(FD->getNameAsString());
    if (It == FormatFunctions.end())
      return;

    FormatArgPos = It->second.formatStringArgIndex;
    FirstVarArgPos = It->second.firstVarArgIndex;
  } else {
    return;
  }

  if (FormatArgPos < 0 || Call->getNumArgs() <= static_cast<unsigned>(FormatArgPos))
    return;

  // Get the format string literal and parse it
  const Expr *FormatArg = Call->getArg(static_cast<unsigned>(FormatArgPos));
  if (const auto *Literal = dyn_cast<StringLiteral>(FormatArg->IgnoreParenCasts())) {
    ParseResult Parsed = parseFormatString(Literal->getString());
    if (Parsed.hasErrors) {
      SourceLocation ErrorLoc = getLocationInStringLiteral(Literal, Parsed.errorOffset);
      diag(ErrorLoc, "invalid format string: %0") << Parsed.errorMsg;
      return;
    }

    // Validate the number of variadic arguments
    unsigned ActualArgs = Call->getNumArgs() - FormatArgPos - 1;
    if (ActualArgs < static_cast<unsigned>(Parsed.expectedArgs)) {
      // Find the first format specifier that doesn't have a corresponding argument
      for (const auto &Spec : Parsed.specifiers) {
        if (Spec.valid && Spec.argIndex >= 0 && FirstVarArgPos + Spec.argIndex >= static_cast<int>(Call->getNumArgs())) {
          SourceLocation SpecLocation = getLocationInStringLiteral(Literal, Spec.start + 1);
          diag(SpecLocation, "format specifier missing corresponding argument");
          break; // Only report the first missing argument
        }
      }
    } else if (ActualArgs > static_cast<unsigned>(Parsed.expectedArgs) && Parsed.expectedArgs > 0) {
      // Find the first extra argument
      unsigned FirstExtraArg = FormatArgPos + 1 + Parsed.expectedArgs;
      if (FirstExtraArg < Call->getNumArgs()) {
        const Expr *ExtraArg = Call->getArg(FirstExtraArg);
        diag(ExtraArg->getBeginLoc(), "format string requires %0 %plural{1:argument|:arguments}0 but %1 %plural{1:was|:were}1 provided")
          << Parsed.expectedArgs
          << ActualArgs;
      } else {
        diag(Call->getEndLoc(), "format string requires %0 %plural{1:argument|:arguments}0 but %1 %plural{1:was|:were}1 provided")
          << Parsed.expectedArgs
          << ActualArgs;
      }
    }

    // Check argument types
    validateArgumentTypes(Parsed.specifiers, Literal, Call, FormatArgPos, *Result.Context);
  } else {
    // Can't analyze non-literal format strings
    return;
  }
}

FmtStringCheck::ParseResult FmtStringCheck::parseFormatString(StringRef Format) {
  ParseResult Result;
  Result.expectedArgs = 0;
  Result.hasErrors = false;
  Result.errorOffset = 0;

  int implicitArgIndex = 0;
  int maxArgIndex = -1;

  size_t i = 0;
  while (i < Format.size()) {
    std::optional<FormatSpecifier> SpecOpt = std::nullopt;
    if (Format[i] == '{') {
      i++; // Skip the '{'
      SpecOpt = parseFmtSpecifier(Format, i, implicitArgIndex);
    } else if (Format[i] == '%') {
      i++; // Skip the '%'
      SpecOpt = parsePrintfSpecifier(Format, i, implicitArgIndex);
    } else {
      i++; // Skip regular character
      continue;
    }

    if (SpecOpt.has_value()) {
      auto Spec = SpecOpt.value();
      maxArgIndex = std::max(maxArgIndex, Spec.argIndex);
      maxArgIndex = std::max(maxArgIndex, Spec.widthArgIndex);
      maxArgIndex = std::max(maxArgIndex, Spec.precArgIndex);

      if (!Spec.valid) {
        Result.hasErrors = true;
        Result.errorMsg = Spec.errorMsg;
        Result.errorOffset = Spec.errorOffset;
      }

      Result.specifiers.push_back(Spec);
    }
  }

  Result.expectedArgs = maxArgIndex + 1;  // Convert from 0-based index to count
  return Result;
}

std::optional<FmtStringCheck::FormatSpecifier> FmtStringCheck::parseFmtSpecifier(StringRef Format, size_t &Index, int &ImplicitArgIndex) {
  // Handle fmt-style specifier: {[index]:[[$fill]align][flags][width][.precision][type]}
  //                              ^ Format[Index]
  StringRef SpecStr = Format.substr(Index);
  if (SpecStr.starts_with('{')) {
    Index++; // Escaped {
    return std::nullopt;
  }

  FormatSpecifier Spec;
  Spec.kind = FormatSpecifier::Fmt;
  Spec.start = Index - 1; // Include the { in start position
  Spec.widthArgIndex = -1;
  Spec.precArgIndex = -1;
  Spec.argIndex = -1;
  Spec.valid = true;

  // Index
  if (std::isdigit(FrontChar(SpecStr))) {
    Spec.argIndex = 0;
    while (std::isdigit(FrontChar(SpecStr))) {
      Spec.argIndex = Spec.argIndex * 10 + (SpecStr.front() - '0');
      SpecStr = SpecStr.drop_front(1);
      Index++;
    }
  }

  // Check for closing brace (simple {})
  if (FrontChar(SpecStr) == '}') {
    Spec.typeFormat = "";
    Spec.argType = ArgumentType::Unknown;
    Spec.end = Index + 1;
    if (Spec.argIndex < 0) {
      Spec.argIndex = ImplicitArgIndex++;
    }
    Index++; // Skip closing '}'
    return Spec;
  }

  // Optional ':' for format specification
  if (FrontChar(SpecStr) != ':') {
    // Invalid specifier - unclosed brace
    Spec.valid = false;
    Spec.end = Index;
    Spec.errorMsg = "invalid format specifier: unclosed '{'";
    Spec.errorOffset = Spec.start;
    Index++;
    return Spec;
  }

  SpecStr = SpecStr.drop_front(1);
  Index++;

  // Fill & Align
  if (FrontChar(SpecStr) == '$') {
    SpecStr = SpecStr.drop_front(1);
    Index++;

    if (SpecStr.empty()) {
      Spec.valid = false;
      Spec.end = Index;
      Spec.errorMsg = "invalid format specifier: missing fill character after '$'";
      Spec.errorOffset = Index;
      return Spec;
    }

    // Skip fill character
    SpecStr = SpecStr.drop_front(1);
    Index++;

    // Check for align
    if (FrontChar(SpecStr) == '<' || FrontChar(SpecStr) == '^' || FrontChar(SpecStr) == '>') {
      SpecStr = SpecStr.drop_front(1);
      Index++;
    } else {
      Spec.valid = false;
      Spec.end = Index;
      Spec.errorMsg = "invalid format specifier: missing alignment character after fill";
      Spec.errorOffset = Index;
      return Spec;
    }
  }

  // Flags
  while (FrontChar(SpecStr) == '#' || FrontChar(SpecStr) == '!' ||
         FrontChar(SpecStr) == '0' || FrontChar(SpecStr) == '+' ||
         FrontChar(SpecStr) == '-' || FrontChar(SpecStr) == ' ') {
    SpecStr = SpecStr.drop_front(1);
    Index++;
  }

  // Width
  if (FrontChar(SpecStr) == '*') {
    // Dynamic width
    SpecStr = SpecStr.drop_front(1);
    Index++;

    // An optional index may follow
    if (std::isdigit(FrontChar(SpecStr))) {
      int widthIndex = 0;
      while (std::isdigit(FrontChar(SpecStr))) {
        widthIndex = widthIndex * 10 + (SpecStr.front() - '0');
        SpecStr = SpecStr.drop_front(1);
        Index++;
      }
      Spec.widthArgIndex = widthIndex;
    } else {
      // No index, use next implicit argument
      Spec.widthArgIndex = ImplicitArgIndex++;
    }
  } else {
    // Constant width
    while (std::isdigit(FrontChar(SpecStr))) {
      SpecStr = SpecStr.drop_front(1);
      Index++;
    }
  }

  // Precision
  if (FrontChar(SpecStr) == '.') {
    SpecStr = SpecStr.drop_front(1);
    Index++;

    if (FrontChar(SpecStr) == '*') {
      // Dynamic precision
      SpecStr = SpecStr.drop_front(1);
      Index++;

      // An optional index may follow
      if (std::isdigit(FrontChar(SpecStr))) {
        int precIndex = 0;
        while (std::isdigit(FrontChar(SpecStr))) {
          precIndex = precIndex * 10 + (SpecStr.front() - '0');
          SpecStr = SpecStr.drop_front(1);
          Index++;
        }
        // Dynamic precision consumes an argument
        Spec.precArgIndex = precIndex;
      } else {
        // No index, use next implicit argument
        Spec.precArgIndex = ImplicitArgIndex++;
      }
    } else {
      // Constant precision
      while (std::isdigit(FrontChar(SpecStr))) {
        SpecStr = SpecStr.drop_front(1);
        Index++;
      }
    }
  }

  // Type
  size_t TypeStart = Index;
  while (!SpecStr.empty() && SpecStr.front() != '}') {
    SpecStr = SpecStr.drop_front(1);
    Index++;
  }

  if (FrontChar(SpecStr) != '}') {
    // Invalid specifier
    Spec.valid = false;
    Spec.end = Index;
    Spec.errorMsg = "invalid format specifier: missing closing '}'";
    Spec.errorOffset = Index;
    return Spec;
  }

  std::string TypeFormat = Format.substr(TypeStart, Index - TypeStart).str();
  ArgumentType ArgType = ArgumentType::Unknown;
  if (!TypeFormat.empty()) {
    if (CustomTypes.count(TypeFormat)) {
      ArgType = ArgumentType::Custom;
    } else if (BuiltinFormatTypes.count(TypeFormat)) {
      ArgType = BuiltinFormatTypes[TypeFormat];
    } else {
      Spec.valid = false;
      Spec.errorMsg = "invalid format specifier: unknown type '" + TypeFormat + "'";
      Spec.errorOffset = TypeStart;
      Spec.end = Index + 1; // Include the closing '}'
      return Spec;
    }
  }

  Spec.typeFormat = TypeFormat;
  Spec.argType = ArgType;
  Spec.end = Index + 1; // Include the closing '}'

  if (Spec.argIndex < 0 && !Spec.typeFormat.empty()) {
    // Specifier is implicitly indexed
    Spec.argIndex = ImplicitArgIndex++;
  }

  Index++; // Skip closing '}'
  return Spec;
}

std::optional<FmtStringCheck::FormatSpecifier> FmtStringCheck::parsePrintfSpecifier(StringRef Format, size_t &Index, int &ImplicitArgIndex) {
  // Handle printf-style specifier: %[flags][width][.precision]type
  //                                 ^ Format[Index]
  StringRef SpecStr = Format.substr(Index);
  if (SpecStr.starts_with('%')) {
    Index++; // Escaped %
    return std::nullopt;
  }

  FormatSpecifier Spec;
  Spec.kind = FormatSpecifier::Printf;
  Spec.start = Index - 1;  // Include the % in start position
  Spec.widthArgIndex = -1;
  Spec.precArgIndex = -1;
  Spec.argIndex = -1;
  Spec.valid = true;

  // Flags
  while (FrontChar(SpecStr) == '#' || FrontChar(SpecStr) == '0' ||
         FrontChar(SpecStr) == '-' || FrontChar(SpecStr) == ' ' ||
         FrontChar(SpecStr) == '+') {
    SpecStr = SpecStr.drop_front(1);
    Index++;
  }

  // Width
  while (std::isdigit(FrontChar(SpecStr))) {
    SpecStr = SpecStr.drop_front(1);
    Index++;
  }

  // Precision
  if (FrontChar(SpecStr) == '.') {
    SpecStr = SpecStr.drop_front(1);
    Index++;
    while (std::isdigit(FrontChar(SpecStr))) {
      SpecStr = SpecStr.drop_front(1);
      Index++;
    }
  }

  // Type
  if (SpecStr.empty()) {
    Spec.valid = false;
    Spec.end = Index;
    Spec.errorMsg = "invalid format specifier: incomplete";
    Spec.errorOffset = Index-1;
    return Spec;
  }

  size_t TypeStart = Index;
  size_t TypeLen = parsePrintfTypeSpec(SpecStr);
  if (TypeLen == 0) {
    Spec.valid = false;
    Spec.end = Index + 1;
    Spec.errorMsg = "invalid format specifier: unknown type '" + SpecStr.substr(0, 1).str() + "'";
    Spec.errorOffset = TypeStart;
    Index++;
    return Spec;
  }

  std::string TypeFormat = Format.substr(TypeStart, TypeLen).str();
  ArgumentType ArgType = BuiltinFormatTypes.count(TypeFormat) ? BuiltinFormatTypes[TypeFormat] : ArgumentType::Unknown;

  Spec.typeFormat = TypeFormat;
  Spec.argType = ArgType;
  Spec.end = Index + TypeLen;
  Spec.argIndex = ImplicitArgIndex++;

  Index += TypeLen;
  return Spec;
}

size_t FmtStringCheck::parsePrintfTypeSpec(StringRef TypeStr) {
  if (TypeStr.empty())
    return 0;

  // Single character types
  switch (TypeStr[0]) {
    case 'd': case 'u': case 'b': case 'o': case 'x': case 'X':
    case 'f': case 'F': case 's': case 'c': case 'p': case 'M':
      return 1;
  }

  // Multi-character types
  if (TypeStr.size() >= 2) {
    switch (TypeStr[0]) {
      case 'h':
        if (TypeStr[1] == 'h' && TypeStr.size() >= 3) {
          switch (TypeStr[2]) {
            case 'd': case 'u': case 'b': case 'o': case 'x': case 'X':
              return 3;
          }
        }
        switch (TypeStr[1]) {
          case 'd': case 'u': case 'b': case 'o': case 'x': case 'X':
            return 2;
        }
        break;
      case 'l':
        if (TypeStr[1] == 'l' && TypeStr.size() >= 3) {
          switch (TypeStr[2]) {
            case 'd': case 'u': case 'b': case 'o': case 'x': case 'X':
              return 3;
          }
        }
        switch (TypeStr[1]) {
          case 'd': case 'u': case 'b': case 'o': case 'x': case 'X':
            return 2;
        }
        break;
      case 'L': case 't': case 'v': case 'z':
        switch (TypeStr[1]) {
          case 'd': case 'u': case 'b': case 'o': case 'x': case 'X':
            return 2;
        }
        break;
    }
  }

  // Invalid type
  return 0;
}

void FmtStringCheck::validateArgumentTypes(
  const std::vector<FormatSpecifier> &Specifiers,
  const StringLiteral *FormatLiteral,
  const CallExpr *Call,
  unsigned FormatArgPos,
  const ASTContext &Context
) {
  const FunctionDecl *FD = Call->getDirectCallee();
  if (!FD)
    return;

  // Check each format specifier against its corresponding argument
  for (const auto &Spec : Specifiers) {
    if (!Spec.valid || Spec.arg_count() == 0)
      continue;

    if (Spec.argIndex >= 0) {
      auto ArgPos = Spec.argIndex + FormatArgPos + 1;
      // Skip validation if argument doesn't exist (already reported as missing)
      if (ArgPos >= Call->getNumArgs())
        continue;

      const Expr *Arg = Call->getArg(ArgPos);
      ArgumentType ActualType = getArgumentType(Arg, Context);
      ArgumentType ExpectedType = Spec.argType;
      std::string ExpectedTypeName = argumentTypeToString(Spec.argType);

      bool IsValidArg = false;
      if (ExpectedType == ArgumentType::Custom) {
        const CustomType &CT = CustomTypes[Spec.typeFormat];
        assert(CT.type != ArgumentType::Unknown && CT.type != ArgumentType::Custom);
        if (CT.type == ArgumentType::CustomStructType) {
          ExpectedTypeName = "struct " + CT.name.value();
          IsValidArg = isArgCustomStructType(Arg, CT.name.value(), /*IsPointer=*/false);
        } else if (CT.type == ArgumentType::CustomStructPType) {
          ExpectedTypeName = "struct " + CT.name.value() + " *";
          IsValidArg = isArgCustomStructType(Arg, CT.name.value(), /*IsPointer=*/true);
        } else if (CT.type == ArgumentType::CustomEnumType) {
          ExpectedTypeName = "enum " + CT.name.value();
          IsValidArg = isArgCustomEnumType(Arg, CT.name.value());
        } else {
          ExpectedTypeName = argumentTypeToString(CT.type);
          IsValidArg = isTypeCompatible(CT.type, ActualType);
        }
      } else {
        IsValidArg = isTypeCompatible(ExpectedType, ActualType);
      }

      if (!IsValidArg) {
        diag(Arg->getBeginLoc(), "format specifies type '%0', but the argument has type '%1' (%2)")
          << ExpectedTypeName
          << Arg->getType().getAsString()
          << argumentTypeToString(ActualType);
      }
    }
    if (Spec.widthArgIndex >= 0) {
      auto ArgPos = Spec.widthArgIndex + FormatArgPos + 1;
      // Skip validation if argument doesn't exist (already reported as missing)
      if (ArgPos >= Call->getNumArgs())
        continue;

      const Expr *Arg = Call->getArg(ArgPos);
      ArgumentType ExpectedType = ArgumentType::Int;
      ArgumentType ActualType = getArgumentType(Arg, Context);

      if (!isTypeCompatible(ExpectedType, ActualType)) {
        SourceLocation SpecLocation = getLocationInStringLiteral(FormatLiteral, Spec.start);
        diag(SpecLocation, "field width should have type '%0', but argument has type '%1'")
          << argumentTypeToString(ExpectedType)
          << Arg->getType().getAsString();
      }
    }
    if (Spec.precArgIndex >= 0) {
      auto ArgPos = Spec.precArgIndex + FormatArgPos + 1;
      // Skip validation if argument doesn't exist (already reported as missing)
      if (ArgPos >= Call->getNumArgs())
        continue;

      const Expr *Arg = Call->getArg(ArgPos);
      ArgumentType ExpectedType = ArgumentType::Int;
      ArgumentType ActualType = getArgumentType(Arg, Context);

      if (!isTypeCompatible(ExpectedType, ActualType)) {
        SourceLocation SpecLocation = getLocationInStringLiteral(FormatLiteral, Spec.start);
        diag(SpecLocation, "field precision should have type '%0', but argument has type '%1'")
          << argumentTypeToString(ExpectedType)
          << Arg->getType().getAsString();
      }
    }
  }
}

FmtStringCheck::ArgumentType FmtStringCheck::getArgumentType(const Expr *Arg, const ASTContext &Context) {
  QualType Type = Arg->getType();
  std::string TypeName = Type.getAsString();

  // Check for typedef'd types by name before canonicalizing
  if (TypeName == "size_t" || TypeName == "ssize_t") {
    return ArgumentType::SizeType;
  } else if (TypeName == "uintptr_t" || TypeName == "intptr_t") {
    return ArgumentType::PointerType;
  }

  // Remove qualifiers and get canonical type
  Type = Type.getCanonicalType().getUnqualifiedType();
  if (Type->isIntegerType()) {
    if (Type->isSignedIntegerType()) {
      // Signed integer types
      if (Type->isSpecificBuiltinType(BuiltinType::Char_S) ||
          Type->isSpecificBuiltinType(BuiltinType::SChar)) {
        return ArgumentType::Char;
      } else if (Type->isSpecificBuiltinType(BuiltinType::Short)) {
        return ArgumentType::Short;
      } else if (Type->isSpecificBuiltinType(BuiltinType::Int)) {
        return ArgumentType::Int;
      } else if (Type->isSpecificBuiltinType(BuiltinType::Long)) {
        return ArgumentType::Long;
      } else if (Type->isSpecificBuiltinType(BuiltinType::LongLong)) {
        return ArgumentType::LongLong;
      }

      // Fallback based on size
      uint64_t SizeBits = Context.getTypeSizeInChars(Type).getQuantity() * 8;
      if (SizeBits <= 8) return ArgumentType::Char;
      if (SizeBits <= 16) return ArgumentType::Short;
      if (SizeBits <= 32) return ArgumentType::Int;
      return ArgumentType::Long;
    } else {
      // Unsigned integer types
      if (Type->isSpecificBuiltinType(BuiltinType::Char_U) ||
          Type->isSpecificBuiltinType(BuiltinType::UChar)) {
        return ArgumentType::UnsignedChar;
      } else if (Type->isSpecificBuiltinType(BuiltinType::UShort)) {
        return ArgumentType::UnsignedShort;
      } else if (Type->isSpecificBuiltinType(BuiltinType::UInt)) {
        return ArgumentType::UnsignedInt;
      } else if (Type->isSpecificBuiltinType(BuiltinType::ULong)) {
        return ArgumentType::UnsignedLong;
      } else if (Type->isSpecificBuiltinType(BuiltinType::ULongLong)) {
        return ArgumentType::UnsignedLongLong;
      }

      // Fallback based on size
      uint64_t SizeBits = Context.getTypeSizeInChars(Type).getQuantity() * 8;
      if (SizeBits <= 8) return ArgumentType::UnsignedChar;
      if (SizeBits <= 16) return ArgumentType::UnsignedShort;
      if (SizeBits <= 32) return ArgumentType::UnsignedInt;
      return ArgumentType::UnsignedLong;
    }
  } else if (Type->isRealFloatingType()) {
    return ArgumentType::Double;
  } else if (Type->isPointerType()) {
    QualType PointeeType = Type->getPointeeType();
    if (PointeeType->isCharType()) {
      // char* or const char*
      return ArgumentType::CString;
    }

    return ArgumentType::VoidPointer;
  }

  return ArgumentType::Unknown;
}

bool FmtStringCheck::isTypeCompatible(ArgumentType expected, ArgumentType actual) {
  if (expected == ArgumentType::Unknown || actual == ArgumentType::Unknown) {
    return true; // Can't verify unknown types
  }

  if (expected == actual) {
    return true; // Exact match
  }

  // Some types are compatible with each other
  switch (expected) {
    case ArgumentType::PointerType:
      // Pointer can accept any pointer type
      return actual == ArgumentType::CString || actual == ArgumentType::VoidPointer ||
             actual == ArgumentType::Long || actual == ArgumentType::UnsignedLong ||
             actual == ArgumentType::LongLong || actual == ArgumentType::UnsignedLongLong;
    case ArgumentType::VoidPointer:
      return actual == ArgumentType::PointerType || actual == ArgumentType::CString;
    case ArgumentType::Char:
      return actual == ArgumentType::Int || actual == ArgumentType::UnsignedInt;
    case ArgumentType::Int:
      return actual == ArgumentType::UnsignedInt;
    case ArgumentType::UnsignedInt:
      return actual == ArgumentType::Int;
    case ArgumentType::UnsignedLong:
      return actual == ArgumentType::Long || actual == ArgumentType::UnsignedLongLong ||
            actual == ArgumentType::LongLong || actual == ArgumentType::SizeType ||
            actual == ArgumentType::VoidPointer || actual == ArgumentType::PointerType;
    case ArgumentType::UnsignedLongLong:
      return actual == ArgumentType::LongLong || actual == ArgumentType::UnsignedLong ||
            actual == ArgumentType::Long || actual == ArgumentType::SizeType ||
            actual == ArgumentType::VoidPointer || actual == ArgumentType::PointerType;
    case ArgumentType::SizeType:
      return actual == ArgumentType::UnsignedLong || actual == ArgumentType::UnsignedLongLong ||
             actual == ArgumentType::Long || actual == ArgumentType::LongLong ||
             actual == ArgumentType::VoidPointer || actual == ArgumentType::PointerType;
    default:
      return false;
  }
}

bool FmtStringCheck::isArgCustomStructType(const Expr *Arg, const std::string &StructName, bool IsPointer) {
  QualType Type = Arg->getType();
  if (IsPointer) {
    // It must be a pointer to a struct
    if (!Type->isPointerType())
      return false;

    // Unwrap the pointer
    Type = Type->getPointeeType();
  }

  // Get the canonical type and check if it's a record type
  QualType CanonicalPointee = Type.getCanonicalType();
  const RecordType *RT = CanonicalPointee->getAs<RecordType>();
  if (!RT)
    return false;

  // Check the record declaration
  const RecordDecl *RD = RT->getDecl();
  if (!RD)
    return false;

  return RD->isStruct() && RD->getName() == StructName;
}

bool FmtStringCheck::isArgCustomEnumType(const Expr *Arg, const std::string &EnumName) {
  QualType Type = Arg->getType();

  // Get the canonical type and check if it's a record type
  QualType CanonicalType = Type.getCanonicalType();
  const EnumType *ET = CanonicalType->getAs<EnumType>();
  if (!ET)
    return false;

  // Check the enum declaration
  const EnumDecl *ED = ET->getDecl();
  if (!ED)
    return false;

  return ED->getName() == EnumName;
}

std::string FmtStringCheck::argumentTypeToString(ArgumentType type) {
  switch (type) {
    case ArgumentType::Char: return "char";
    case ArgumentType::UnsignedChar: return "unsigned char";
    case ArgumentType::Short: return "short";
    case ArgumentType::UnsignedShort: return "unsigned short";
    case ArgumentType::Int: return "int";
    case ArgumentType::UnsignedInt: return "unsigned int";
    case ArgumentType::Long: return "long";
    case ArgumentType::UnsignedLong: return "unsigned long";
    case ArgumentType::LongLong: return "long long";
    case ArgumentType::UnsignedLongLong: return "unsigned long long";
    case ArgumentType::SizeType: return "size_t";
    case ArgumentType::SignedSizeType: return "ssize_t";
    case ArgumentType::Double: return "double";
    case ArgumentType::PointerType: return "uintptr_t";
    case ArgumentType::VoidPointer: return "void *";
    case ArgumentType::CString: return "const char *";
    case ArgumentType::Custom: return "custom";
    case ArgumentType::CustomStructType: return "struct";
    case ArgumentType::CustomStructPType: return "struct pointer";
    case ArgumentType::CustomEnumType: return "enum";
    case ArgumentType::Unknown: break;
  }
  return "unknown";
}

SourceLocation FmtStringCheck::getLocationInStringLiteral(const StringLiteral *Literal, size_t Offset) const {
  // Starting location of string literal includes the opening quote
  SourceLocation StartLoc = Literal->getBeginLoc();
  StartLoc = StartLoc.getLocWithOffset(1);

  // Get offset within the literal
  // Allow pointing one past the end for incomplete format strings
  if (Offset >= Literal->getLength()) {
    Offset = Literal->getLength();
  }
  return StartLoc.getLocWithOffset(static_cast<int>(Offset));
}

} // namespace clang::tidy::osdev

