//===--- Options.cpp - clang-cc Option Handling ---------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// This file contains "pure" option handling, it is only responsible for turning
// the options into internal *Option classes, but shouldn't have any other
// logic.

#include "Options.h"
#include "clang/Frontend/CompileOptions.h"
#include "clang/Frontend/PCHReader.h"
#include "clang/Frontend/PreprocessorOptions.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include <stdio.h>

using namespace clang;

//===----------------------------------------------------------------------===//
// Code Generation Options
//===----------------------------------------------------------------------===//

namespace codegenoptions {

static llvm::cl::opt<bool>
DisableLLVMOptimizations("disable-llvm-optzns",
                         llvm::cl::desc("Don't run LLVM optimization passes"));

static llvm::cl::opt<bool>
DisableRedZone("disable-red-zone",
               llvm::cl::desc("Do not emit code that uses the red zone."),
               llvm::cl::init(false));

static llvm::cl::opt<bool>
GenerateDebugInfo("g",
                  llvm::cl::desc("Generate source level debug information"));

static llvm::cl::opt<bool>
NoCommon("fno-common",
         llvm::cl::desc("Compile common globals like normal definitions"),
         llvm::cl::ValueDisallowed);

static llvm::cl::opt<bool>
NoImplicitFloat("no-implicit-float",
  llvm::cl::desc("Don't generate implicit floating point instructions (x86-only)"),
  llvm::cl::init(false));

static llvm::cl::opt<bool>
NoMergeConstants("fno-merge-all-constants",
                       llvm::cl::desc("Disallow merging of constants."));

// It might be nice to add bounds to the CommandLine library directly.
struct OptLevelParser : public llvm::cl::parser<unsigned> {
  bool parse(llvm::cl::Option &O, llvm::StringRef ArgName,
             llvm::StringRef Arg, unsigned &Val) {
    if (llvm::cl::parser<unsigned>::parse(O, ArgName, Arg, Val))
      return true;
    if (Val > 3)
      return O.error("'" + Arg + "' invalid optimization level!");
    return false;
  }
};
static llvm::cl::opt<unsigned, false, OptLevelParser>
OptLevel("O", llvm::cl::Prefix,
         llvm::cl::desc("Optimization level"),
         llvm::cl::init(0));

static llvm::cl::opt<bool>
OptSize("Os", llvm::cl::desc("Optimize for size"));

static llvm::cl::opt<std::string>
TargetCPU("mcpu",
         llvm::cl::desc("Target a specific cpu type (-mcpu=help for details)"));

static llvm::cl::list<std::string>
TargetFeatures("target-feature", llvm::cl::desc("Target specific attributes"));

}

//===----------------------------------------------------------------------===//
// Language Options
//===----------------------------------------------------------------------===//

namespace langoptions {

static llvm::cl::opt<bool>
AllowBuiltins("fbuiltin", llvm::cl::init(true),
             llvm::cl::desc("Disable implicit builtin knowledge of functions"));

static llvm::cl::opt<bool>
AltiVec("faltivec", llvm::cl::desc("Enable AltiVec vector initializer syntax"),
                    llvm::cl::init(false));

static llvm::cl::opt<bool>
AccessControl("faccess-control",
              llvm::cl::desc("Enable C++ access control"));

static llvm::cl::opt<bool>
CharIsSigned("fsigned-char",
    llvm::cl::desc("Force char to be a signed/unsigned type"));

static llvm::cl::opt<bool>
DollarsInIdents("fdollars-in-identifiers",
                llvm::cl::desc("Allow '$' in identifiers"));

static llvm::cl::opt<bool>
EmitAllDecls("femit-all-decls",
              llvm::cl::desc("Emit all declarations, even if unused"));

static llvm::cl::opt<bool>
EnableBlocks("fblocks", llvm::cl::desc("enable the 'blocks' language feature"));

static llvm::cl::opt<bool>
EnableHeinousExtensions("fheinous-gnu-extensions",
   llvm::cl::desc("enable GNU extensions that you really really shouldn't use"),
                        llvm::cl::ValueDisallowed, llvm::cl::Hidden);

static llvm::cl::opt<bool>
Exceptions("fexceptions",
           llvm::cl::desc("Enable support for exception handling"));

static llvm::cl::opt<bool>
Freestanding("ffreestanding",
             llvm::cl::desc("Assert that the compilation takes place in a "
                            "freestanding environment"));

static llvm::cl::opt<bool>
GNURuntime("fgnu-runtime",
            llvm::cl::desc("Generate output compatible with the standard GNU "
                           "Objective-C runtime"));

/// LangStds - Language standards we support.
enum LangStds {
  lang_unspecified,
  lang_c89, lang_c94, lang_c99,
  lang_gnu89, lang_gnu99,
  lang_cxx98, lang_gnucxx98,
  lang_cxx0x, lang_gnucxx0x
};
static llvm::cl::opt<LangStds>
LangStd("std", llvm::cl::desc("Language standard to compile for"),
        llvm::cl::init(lang_unspecified),
  llvm::cl::values(clEnumValN(lang_c89,      "c89",            "ISO C 1990"),
                   clEnumValN(lang_c89,      "c90",            "ISO C 1990"),
                   clEnumValN(lang_c89,      "iso9899:1990",   "ISO C 1990"),
                   clEnumValN(lang_c94,      "iso9899:199409",
                              "ISO C 1990 with amendment 1"),
                   clEnumValN(lang_c99,      "c99",            "ISO C 1999"),
                   clEnumValN(lang_c99,      "c9x",            "ISO C 1999"),
                   clEnumValN(lang_c99,      "iso9899:1999",   "ISO C 1999"),
                   clEnumValN(lang_c99,      "iso9899:199x",   "ISO C 1999"),
                   clEnumValN(lang_gnu89,    "gnu89",
                              "ISO C 1990 with GNU extensions"),
                   clEnumValN(lang_gnu99,    "gnu99",
                              "ISO C 1999 with GNU extensions (default for C)"),
                   clEnumValN(lang_gnu99,    "gnu9x",
                              "ISO C 1999 with GNU extensions"),
                   clEnumValN(lang_cxx98,    "c++98",
                              "ISO C++ 1998 with amendments"),
                   clEnumValN(lang_gnucxx98, "gnu++98",
                              "ISO C++ 1998 with amendments and GNU "
                              "extensions (default for C++)"),
                   clEnumValN(lang_cxx0x,    "c++0x",
                              "Upcoming ISO C++ 200x with amendments"),
                   clEnumValN(lang_gnucxx0x, "gnu++0x",
                              "Upcoming ISO C++ 200x with amendments and GNU "
                              "extensions"),
                   clEnumValEnd));

static llvm::cl::opt<bool>
MSExtensions("fms-extensions",
             llvm::cl::desc("Accept some non-standard constructs used in "
                            "Microsoft header files "));

static llvm::cl::opt<std::string>
MainFileName("main-file-name",
             llvm::cl::desc("Main file name to use for debug info"));

static llvm::cl::opt<bool>
MathErrno("fmath-errno", llvm::cl::init(true),
          llvm::cl::desc("Require math functions to respect errno"));

static llvm::cl::opt<bool>
NeXTRuntime("fnext-runtime",
            llvm::cl::desc("Generate output compatible with the NeXT "
                           "runtime"));

static llvm::cl::opt<bool>
NoElideConstructors("fno-elide-constructors",
                    llvm::cl::desc("Disable C++ copy constructor elision"));

static llvm::cl::opt<bool>
NoLaxVectorConversions("fno-lax-vector-conversions",
                       llvm::cl::desc("Disallow implicit conversions between "
                                      "vectors with a different number of "
                                      "elements or different element types"));


static llvm::cl::opt<bool>
NoOperatorNames("fno-operator-names",
                llvm::cl::desc("Do not treat C++ operator name keywords as "
                               "synonyms for operators"));

static llvm::cl::opt<std::string>
ObjCConstantStringClass("fconstant-string-class",
                llvm::cl::value_desc("class name"),
                llvm::cl::desc("Specify the class to use for constant "
                               "Objective-C string objects."));

static llvm::cl::opt<bool>
ObjCEnableGC("fobjc-gc",
             llvm::cl::desc("Enable Objective-C garbage collection"));

static llvm::cl::opt<bool>
ObjCExclusiveGC("fobjc-gc-only",
                llvm::cl::desc("Use GC exclusively for Objective-C related "
                               "memory management"));

static llvm::cl::opt<bool>
ObjCEnableGCBitmapPrint("print-ivar-layout",
             llvm::cl::desc("Enable Objective-C Ivar layout bitmap print trace"));

static llvm::cl::opt<bool>
ObjCNonFragileABI("fobjc-nonfragile-abi",
                  llvm::cl::desc("enable objective-c's nonfragile abi"));

static llvm::cl::opt<bool>
OverflowChecking("ftrapv",
                 llvm::cl::desc("Trap on integer overflow"),
                 llvm::cl::init(false));

static llvm::cl::opt<unsigned>
PICLevel("pic-level", llvm::cl::desc("Value for __PIC__"));

static llvm::cl::opt<bool>
PThread("pthread", llvm::cl::desc("Support POSIX threads in generated code"),
         llvm::cl::init(false));

static llvm::cl::opt<bool>
PascalStrings("fpascal-strings",
              llvm::cl::desc("Recognize and construct Pascal-style "
                             "string literals"));

// FIXME: Move to CompileOptions.
static llvm::cl::opt<bool>
Rtti("frtti", llvm::cl::init(true),
     llvm::cl::desc("Enable generation of rtti information"));

static llvm::cl::opt<bool>
ShortWChar("fshort-wchar",
    llvm::cl::desc("Force wchar_t to be a short unsigned int"));

static llvm::cl::opt<bool>
StaticDefine("static-define", llvm::cl::desc("Should __STATIC__ be defined"));

static llvm::cl::opt<int>
StackProtector("stack-protector",
               llvm::cl::desc("Enable stack protectors"),
               llvm::cl::init(-1));

static llvm::cl::opt<LangOptions::VisibilityMode>
SymbolVisibility("fvisibility",
                 llvm::cl::desc("Set the default symbol visibility:"),
                 llvm::cl::init(LangOptions::Default),
                 llvm::cl::values(clEnumValN(LangOptions::Default, "default",
                                             "Use default symbol visibility"),
                                  clEnumValN(LangOptions::Hidden, "hidden",
                                             "Use hidden symbol visibility"),
                                  clEnumValN(LangOptions::Protected,"protected",
                                             "Use protected symbol visibility"),
                                  clEnumValEnd));

static llvm::cl::opt<unsigned>
TemplateDepth("ftemplate-depth", llvm::cl::init(99),
              llvm::cl::desc("Maximum depth of recursive template "
                             "instantiation"));

static llvm::cl::opt<bool>
Trigraphs("trigraphs", llvm::cl::desc("Process trigraph sequences"));

static llvm::cl::opt<bool>
WritableStrings("fwritable-strings",
              llvm::cl::desc("Store string literals as writable data"));

}

//===----------------------------------------------------------------------===//
// General Preprocessor Options
//===----------------------------------------------------------------------===//

namespace preprocessoroptions {

static llvm::cl::list<std::string>
D_macros("D", llvm::cl::value_desc("macro"), llvm::cl::Prefix,
       llvm::cl::desc("Predefine the specified macro"));

static llvm::cl::list<std::string>
ImplicitIncludes("include", llvm::cl::value_desc("file"),
                 llvm::cl::desc("Include file before parsing"));
static llvm::cl::list<std::string>
ImplicitMacroIncludes("imacros", llvm::cl::value_desc("file"),
                      llvm::cl::desc("Include macros from file before parsing"));

static llvm::cl::opt<std::string>
ImplicitIncludePCH("include-pch", llvm::cl::value_desc("file"),
                   llvm::cl::desc("Include precompiled header file"));

static llvm::cl::opt<std::string>
ImplicitIncludePTH("include-pth", llvm::cl::value_desc("file"),
                   llvm::cl::desc("Include file before parsing"));

static llvm::cl::list<std::string>
U_macros("U", llvm::cl::value_desc("macro"), llvm::cl::Prefix,
         llvm::cl::desc("Undefine the specified macro"));

static llvm::cl::opt<bool>
UndefMacros("undef", llvm::cl::value_desc("macro"),
            llvm::cl::desc("undef all system defines"));

}

//===----------------------------------------------------------------------===//
// Option Object Construction
//===----------------------------------------------------------------------===//

/// ComputeTargetFeatures - Recompute the target feature list to only
/// be the list of things that are enabled, based on the target cpu
/// and feature list.
void clang::ComputeFeatureMap(TargetInfo &Target,
                              llvm::StringMap<bool> &Features) {
  using namespace codegenoptions;
  assert(Features.empty() && "invalid map");

  // Initialize the feature map based on the target.
  Target.getDefaultFeatures(TargetCPU, Features);

  // Apply the user specified deltas.
  for (llvm::cl::list<std::string>::iterator it = TargetFeatures.begin(),
         ie = TargetFeatures.end(); it != ie; ++it) {
    const char *Name = it->c_str();

    // FIXME: Don't handle errors like this.
    if (Name[0] != '-' && Name[0] != '+') {
      fprintf(stderr, "error: clang-cc: invalid target feature string: %s\n",
              Name);
      exit(1);
    }
    if (!Target.setFeatureEnabled(Features, Name + 1, (Name[0] == '+'))) {
      fprintf(stderr, "error: clang-cc: invalid target feature name: %s\n",
              Name + 1);
      exit(1);
    }
  }
}

void clang::InitializeCompileOptions(CompileOptions &Opts,
                                     const llvm::StringMap<bool> &Features) {
  using namespace codegenoptions;
  Opts.OptimizeSize = OptSize;
  Opts.DebugInfo = GenerateDebugInfo;
  Opts.DisableLLVMOpts = DisableLLVMOptimizations;

  // -Os implies -O2
  Opts.OptimizationLevel = OptSize ? 2 : OptLevel;

  // We must always run at least the always inlining pass.
  Opts.Inlining = (Opts.OptimizationLevel > 1) ? CompileOptions::NormalInlining
    : CompileOptions::OnlyAlwaysInlining;

  Opts.UnrollLoops = (Opts.OptimizationLevel > 1 && !OptSize);
  Opts.SimplifyLibCalls = 1;

#ifdef NDEBUG
  Opts.VerifyModule = 0;
#endif

  Opts.CPU = TargetCPU;
  Opts.Features.clear();
  for (llvm::StringMap<bool>::const_iterator it = Features.begin(),
         ie = Features.end(); it != ie; ++it) {
    // FIXME: If we are completely confident that we have the right set, we only
    // need to pass the minuses.
    std::string Name(it->second ? "+" : "-");
    Name += it->first();
    Opts.Features.push_back(Name);
  }

  Opts.NoCommon = NoCommon;

  Opts.DisableRedZone = DisableRedZone;
  Opts.NoImplicitFloat = NoImplicitFloat;

  Opts.MergeAllConstants = !NoMergeConstants;
}

void clang::InitializePreprocessorOptions(PreprocessorOptions &Opts) {
  using namespace preprocessoroptions;

  Opts.setImplicitPCHInclude(ImplicitIncludePCH);
  Opts.setImplicitPTHInclude(ImplicitIncludePTH);

  // Use predefines?
  Opts.setUsePredefines(!UndefMacros);

  // Add macros from the command line.
  unsigned d = 0, D = D_macros.size();
  unsigned u = 0, U = U_macros.size();
  while (d < D || u < U) {
    if (u == U || (d < D && D_macros.getPosition(d) < U_macros.getPosition(u)))
      Opts.addMacroDef(D_macros[d++]);
    else
      Opts.addMacroUndef(U_macros[u++]);
  }

  // If -imacros are specified, include them now.  These are processed before
  // any -include directives.
  for (unsigned i = 0, e = ImplicitMacroIncludes.size(); i != e; ++i)
    Opts.addMacroInclude(ImplicitMacroIncludes[i]);

  // Add the ordered list of -includes, sorting in the implicit include options
  // at the appropriate location.
  llvm::SmallVector<std::pair<unsigned, std::string*>, 8> OrderedPaths;
  std::string OriginalFile;

  if (!ImplicitIncludePTH.empty())
    OrderedPaths.push_back(std::make_pair(ImplicitIncludePTH.getPosition(),
                                          &ImplicitIncludePTH));
  if (!ImplicitIncludePCH.empty()) {
    OriginalFile = PCHReader::getOriginalSourceFile(ImplicitIncludePCH);
    // FIXME: Don't fail like this.
    if (OriginalFile.empty())
      exit(1);
    OrderedPaths.push_back(std::make_pair(ImplicitIncludePCH.getPosition(),
                                          &OriginalFile));
  }
  for (unsigned i = 0, e = ImplicitIncludes.size(); i != e; ++i)
    OrderedPaths.push_back(std::make_pair(ImplicitIncludes.getPosition(i),
                                          &ImplicitIncludes[i]));
  llvm::array_pod_sort(OrderedPaths.begin(), OrderedPaths.end());

  for (unsigned i = 0, e = OrderedPaths.size(); i != e; ++i)
    Opts.addInclude(*OrderedPaths[i].second);
}

void clang::InitializeLangOptions(LangOptions &Options, LangKind LK,
                                  TargetInfo &Target,
                                  const CompileOptions &CompileOpts,
                                  const llvm::StringMap<bool> &Features) {
  using namespace langoptions;

  bool NoPreprocess = false;

  switch (LK) {
  default: assert(0 && "Unknown language kind!");
  case langkind_asm_cpp:
    Options.AsmPreprocessor = 1;
    // FALLTHROUGH
  case langkind_c_cpp:
    NoPreprocess = true;
    // FALLTHROUGH
  case langkind_c:
    // Do nothing.
    break;
  case langkind_cxx_cpp:
    NoPreprocess = true;
    // FALLTHROUGH
  case langkind_cxx:
    Options.CPlusPlus = 1;
    break;
  case langkind_objc_cpp:
    NoPreprocess = true;
    // FALLTHROUGH
  case langkind_objc:
    Options.ObjC1 = Options.ObjC2 = 1;
    break;
  case langkind_objcxx_cpp:
    NoPreprocess = true;
    // FALLTHROUGH
  case langkind_objcxx:
    Options.ObjC1 = Options.ObjC2 = 1;
    Options.CPlusPlus = 1;
    break;
  case langkind_ocl:
    Options.OpenCL = 1;
    Options.AltiVec = 1;
    Options.CXXOperatorNames = 1;
    Options.LaxVectorConversions = 1;
    break;
  }

  if (ObjCExclusiveGC)
    Options.setGCMode(LangOptions::GCOnly);
  else if (ObjCEnableGC)
    Options.setGCMode(LangOptions::HybridGC);

  if (ObjCEnableGCBitmapPrint)
    Options.ObjCGCBitmapPrint = 1;

  if (AltiVec)
    Options.AltiVec = 1;

  if (PThread)
    Options.POSIXThreads = 1;

  Options.setVisibilityMode(SymbolVisibility);
  Options.OverflowChecking = OverflowChecking;


  // Allow the target to set the default the language options as it sees fit.
  Target.getDefaultLangOptions(Options);

  // Pass the map of target features to the target for validation and
  // processing.
  Target.HandleTargetFeatures(Features);

  if (LangStd == lang_unspecified) {
    // Based on the base language, pick one.
    switch (LK) {
    case langkind_ast: assert(0 && "Invalid call for AST inputs");
    case lang_unspecified: assert(0 && "Unknown base language");
    case langkind_ocl:
      LangStd = lang_c99;
      break;
    case langkind_c:
    case langkind_asm_cpp:
    case langkind_c_cpp:
    case langkind_objc:
    case langkind_objc_cpp:
      LangStd = lang_gnu99;
      break;
    case langkind_cxx:
    case langkind_cxx_cpp:
    case langkind_objcxx:
    case langkind_objcxx_cpp:
      LangStd = lang_gnucxx98;
      break;
    }
  }

  switch (LangStd) {
  default: assert(0 && "Unknown language standard!");

  // Fall through from newer standards to older ones.  This isn't really right.
  // FIXME: Enable specifically the right features based on the language stds.
  case lang_gnucxx0x:
  case lang_cxx0x:
    Options.CPlusPlus0x = 1;
    // FALL THROUGH
  case lang_gnucxx98:
  case lang_cxx98:
    Options.CPlusPlus = 1;
    Options.CXXOperatorNames = !NoOperatorNames;
    // FALL THROUGH.
  case lang_gnu99:
  case lang_c99:
    Options.C99 = 1;
    Options.HexFloats = 1;
    // FALL THROUGH.
  case lang_gnu89:
    Options.BCPLComment = 1;  // Only for C99/C++.
    // FALL THROUGH.
  case lang_c94:
    Options.Digraphs = 1;     // C94, C99, C++.
    // FALL THROUGH.
  case lang_c89:
    break;
  }

  // GNUMode - Set if we're in gnu99, gnu89, gnucxx98, etc.
  switch (LangStd) {
  default: assert(0 && "Unknown language standard!");
  case lang_gnucxx0x:
  case lang_gnucxx98:
  case lang_gnu99:
  case lang_gnu89:
    Options.GNUMode = 1;
    break;
  case lang_cxx0x:
  case lang_cxx98:
  case lang_c99:
  case lang_c94:
  case lang_c89:
    Options.GNUMode = 0;
    break;
  }

  if (Options.CPlusPlus) {
    Options.C99 = 0;
    Options.HexFloats = 0;
  }

  if (LangStd == lang_c89 || LangStd == lang_c94 || LangStd == lang_gnu89)
    Options.ImplicitInt = 1;
  else
    Options.ImplicitInt = 0;

  // Mimicing gcc's behavior, trigraphs are only enabled if -trigraphs
  // is specified, or -std is set to a conforming mode.
  Options.Trigraphs = !Options.GNUMode;
  if (Trigraphs.getPosition())
    Options.Trigraphs = Trigraphs;  // Command line option wins if specified.

  // If in a conformant language mode (e.g. -std=c99) Blocks defaults to off
  // even if they are normally on for the target.  In GNU modes (e.g.
  // -std=gnu99) the default for blocks depends on the target settings.
  // However, blocks are not turned off when compiling Obj-C or Obj-C++ code.
  if (!Options.ObjC1 && !Options.GNUMode)
    Options.Blocks = 0;

  // Default to not accepting '$' in identifiers when preprocessing assembler,
  // but do accept when preprocessing C.  FIXME: these defaults are right for
  // darwin, are they right everywhere?
  Options.DollarIdents = LK != langkind_asm_cpp;
  if (DollarsInIdents.getPosition())  // Explicit setting overrides default.
    Options.DollarIdents = DollarsInIdents;

  if (PascalStrings.getPosition())
    Options.PascalStrings = PascalStrings;
  if (MSExtensions.getPosition())
    Options.Microsoft = MSExtensions;
  Options.WritableStrings = WritableStrings;
  if (NoLaxVectorConversions.getPosition())
      Options.LaxVectorConversions = 0;
  Options.Exceptions = Exceptions;
  Options.Rtti = Rtti;
  if (EnableBlocks.getPosition())
    Options.Blocks = EnableBlocks;
  if (CharIsSigned.getPosition())
    Options.CharIsSigned = CharIsSigned;
  if (ShortWChar.getPosition())
    Options.ShortWChar = ShortWChar;

  if (!AllowBuiltins)
    Options.NoBuiltin = 1;
  if (Freestanding)
    Options.Freestanding = Options.NoBuiltin = 1;

  if (EnableHeinousExtensions)
    Options.HeinousExtensions = 1;

  if (AccessControl)
    Options.AccessControl = 1;

  Options.ElideConstructors = !NoElideConstructors;

  // OpenCL and C++ both have bool, true, false keywords.
  Options.Bool = Options.OpenCL | Options.CPlusPlus;

  Options.MathErrno = MathErrno;

  Options.InstantiationDepth = TemplateDepth;

  // Override the default runtime if the user requested it.
  if (NeXTRuntime)
    Options.NeXTRuntime = 1;
  else if (GNURuntime)
    Options.NeXTRuntime = 0;

  if (!ObjCConstantStringClass.empty())
    Options.ObjCConstantStringClass = ObjCConstantStringClass.c_str();

  if (ObjCNonFragileABI)
    Options.ObjCNonFragileABI = 1;

  if (EmitAllDecls)
    Options.EmitAllDecls = 1;

  // The __OPTIMIZE_SIZE__ define is tied to -Oz, which we don't support.
  Options.OptimizeSize = 0;
  Options.Optimize = !!CompileOpts.OptimizationLevel;

  assert(PICLevel <= 2 && "Invalid value for -pic-level");
  Options.PICLevel = PICLevel;

  Options.GNUInline = !Options.C99;
  // FIXME: This is affected by other options (-fno-inline).

  // This is the __NO_INLINE__ define, which just depends on things like the
  // optimization level and -fno-inline, not actually whether the backend has
  // inlining enabled.
  Options.NoInline = !CompileOpts.OptimizationLevel;

  Options.Static = StaticDefine;

  switch (StackProtector) {
  default:
    assert(StackProtector <= 2 && "Invalid value for -stack-protector");
  case -1: break;
  case 0: Options.setStackProtectorMode(LangOptions::SSPOff); break;
  case 1: Options.setStackProtectorMode(LangOptions::SSPOn);  break;
  case 2: Options.setStackProtectorMode(LangOptions::SSPReq); break;
  }

  if (MainFileName.getPosition())
    Options.setMainFileName(MainFileName.c_str());

  Target.setForcedLangOptions(Options);
}