set(LLVM_NO_RTTI 1)

add_clang_library(clangAST
  APValue.cpp
  ASTConsumer.cpp
  ASTContext.cpp
  ASTImporter.cpp
  ASTDiagnostic.cpp
  AttrImpl.cpp
  CXXInheritance.cpp
  Decl.cpp
  DeclBase.cpp
  DeclCXX.cpp
  DeclGroup.cpp
  DeclObjC.cpp
  DeclPrinter.cpp
  DeclTemplate.cpp
  DeclarationName.cpp
  Expr.cpp
  ExprCXX.cpp
  ExprConstant.cpp
  FullExpr.cpp
  InheritViz.cpp
  NestedNameSpecifier.cpp
  ParentMap.cpp
  RecordLayoutBuilder.cpp
  Stmt.cpp
  StmtDumper.cpp
  StmtIterator.cpp
  StmtPrinter.cpp
  StmtProfile.cpp
  StmtViz.cpp
  TemplateBase.cpp
  TemplateName.cpp
  Type.cpp
  TypeLoc.cpp
  TypePrinter.cpp
  )

add_dependencies(clangAST ClangDiagnosticAST)
