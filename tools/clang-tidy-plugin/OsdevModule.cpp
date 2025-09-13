//
// Created by Aaron Gill-Braun on 2025-09-8.
//

#include "FmtStringCheck.h"

#include "clang-tidy/ClangTidyModule.h"
#include "clang-tidy/ClangTidyModuleRegistry.h"

namespace clang::tidy::osdev {

class OsdevModule : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &CheckFactories) override {
    CheckFactories.registerCheck<FmtStringCheck>("osdev-fmt-string");
  }
};

static ClangTidyModuleRegistry::Add<OsdevModule> X("osdev-module", "Adds custom checks");

} // namespace clang::tidy::osdev


// Export the module for dynamic loading
extern "C" {
  clang::tidy::ClangTidyModule *clangTidyModuleAnchor() {
    static clang::tidy::osdev::OsdevModule Module;
    return &Module;
  }
}
