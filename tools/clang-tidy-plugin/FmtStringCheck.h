//
// Created by Aaron Gill-Braun on 2025-09-8.
//

#ifndef OSDEV_FMT_STRING_CHECK_H
#define OSDEV_FMT_STRING_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

#include <string>
#include <vector>
#include <set>

namespace clang::tidy::osdev {

/// Checks format string arguments for functions using the custom fmt library.
///
/// This check validates:
/// - Format string syntax (both printf-style % and rust-style {})
/// - Argument count matches format specifiers
/// - Argument types match format specifiers
/// - Invalid format specifier syntax
///
/// The checker automatically detects functions with format attributes:
///   __attribute__((format(printf, i, j)))  - Standard printf checking
///   __attribute__((format(fmt, i, j)))     - Custom fmt format checking
///
/// You can also define custom format types in your code:
///   // For custom fmt library that supports both % and {} syntax
///   #define __fmt_format(string_idx, first_arg) \
///     __attribute__((format(printf, string_idx, first_arg)))
///
/// Configuration options:
///   - Functions: Comma-separated list of function names to check
///     (default: "printf,sprintf,snprintf,fmt_format,kprintf")
class FmtStringCheck : public ClangTidyCheck {
public:
  FmtStringCheck(StringRef Name, ClangTidyContext *Context);
  
  void storeOptions(ClangTidyOptions::OptionMap &Opts) override;
  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

  bool isLanguageVersionSupported(const LangOptions &LangOpts) const override {
    return LangOpts.C99 || LangOpts.CPlusPlus;
  }

private:
  enum class ArgumentType {
    Char,               // char
    UnsignedChar,       // unsigned char
    Short,              // short
    UnsignedShort,      // unsigned short
    Int,                // int
    UnsignedInt,        // unsigned int
    Long,               // long
    UnsignedLong,       // unsigned long
    LongLong,           // long long
    UnsignedLongLong,   // unsigned long long
    SizeType,           // size_t
    SignedSizeType,     // ssize_t
    Double,             // float, double
    PointerType,        // uintptr_t
    VoidPointer,        // void*, etc.
    CString,            // const char*, char *
    Custom,             // custom types
    CustomEnumType,     // enum type
    CustomStructType,   // struct type
    CustomStructPType,  // pointer to struct type
    Unknown
  };

  struct CustomType {
    std::string typeFormat;
    ArgumentType type;
    std::optional<std::string> name; // struct name if type is a struct or enum
  };

  struct FormatFunction {
    std::string name;
    int formatStringArgIndex;  // index of format string argument (0-based)
    int firstVarArgIndex;      // index of first variadic argument (0-based)
  };

  struct FormatSpecifier {
    enum Kind {
      Fmt,    // {[index]:[[$fill]align][flags][width][.precision][type]}
      Printf, // %[flags][width][.precision]type
    };
    
    Kind kind;
    size_t start;
    size_t end;
    int widthArgIndex;         // for '*' width
    int precArgIndex;          // for '*' precision
    int argIndex;
    std::string typeFormat;    // the full type format
    ArgumentType argType;      // expected argument type
    bool valid;
    std::string errorMsg;      // if !valid, contains error message
    size_t errorOffset;        // offset within format string where error occurred

    int arg_count() const {
      int count = 0;
      if (argIndex >= 0) count++;
      if (widthArgIndex >= 0) count++;
      if (precArgIndex >= 0) count++;
      return count;
    }
  };
  
  struct ParseResult {
    std::vector<FormatSpecifier> specifiers;
    int expectedArgs;
    bool hasErrors;
    std::string errorMsg;
    size_t errorOffset;  // Offset within format string where error occurred
  };

  static std::map<std::string, ArgumentType> BuiltinFormatTypes;
  static std::map<std::string, ArgumentType> OptionCustomTypes;
  static std::map<ArgumentType, std::string> TypeCustomOptions;
  std::map<std::string, CustomType> CustomTypes;
  std::map<std::string, FormatFunction> FormatFunctions;

  ParseResult parseFormatString(StringRef Format);
  std::optional<FmtStringCheck::FormatSpecifier> parseFmtSpecifier(StringRef Format, size_t &Index, int &ImplicitArgIndex);
  std::optional<FmtStringCheck::FormatSpecifier> parsePrintfSpecifier(StringRef Format, size_t &Index, int &ImplicitArgIndex);
  size_t parsePrintfTypeSpec(StringRef TypeStr);

  void validateArgumentTypes(
    const std::vector<FormatSpecifier> &Specifiers,
    const StringLiteral *FormatLiteral,
    const CallExpr *Call,
    unsigned FormatArgPos,
    const ASTContext &Context
  );

  ArgumentType getArgumentType(const Expr *Arg, const ASTContext &Context);
  bool isTypeCompatible(ArgumentType expected, ArgumentType actual);
  bool isArgCustomStructType(const Expr *Arg, const std::string &StructName, bool IsPointer);
  bool isArgCustomEnumType(const Expr *Arg, const std::string &EnumName);
  std::string argumentTypeToString(ArgumentType type);
  SourceLocation getLocationInStringLiteral(const StringLiteral *Literal, size_t Offset) const;
};

} // namespace clang::tidy::osdev

#endif // OSDEV_FMT_STRING_CHECK_H
