//===--- ParseDeclCXX.cpp - C++ Declaration Parsing -----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the C++ Declaration portions of the Parser interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/OperatorKinds.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Parse/Scope.h"
#include "clang/Parse/Template.h"
#include "RAIIObjectsForParser.h"
using namespace clang;

/// ParseNamespace - We know that the current token is a namespace keyword. This
/// may either be a top level namespace or a block-level namespace alias.
///
///       namespace-definition: [C++ 7.3: basic.namespace]
///         named-namespace-definition
///         unnamed-namespace-definition
///
///       unnamed-namespace-definition:
///         'namespace' attributes[opt] '{' namespace-body '}'
///
///       named-namespace-definition:
///         original-namespace-definition
///         extension-namespace-definition
///
///       original-namespace-definition:
///         'namespace' identifier attributes[opt] '{' namespace-body '}'
///
///       extension-namespace-definition:
///         'namespace' original-namespace-name '{' namespace-body '}'
///
///       namespace-alias-definition:  [C++ 7.3.2: namespace.alias]
///         'namespace' identifier '=' qualified-namespace-specifier ';'
///
Parser::DeclPtrTy Parser::ParseNamespace(unsigned Context,
                                         SourceLocation &DeclEnd) {
  assert(Tok.is(tok::kw_namespace) && "Not a namespace!");
  SourceLocation NamespaceLoc = ConsumeToken();  // eat the 'namespace'.

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteNamespaceDecl(CurScope);
    ConsumeToken();
  }
  
  SourceLocation IdentLoc;
  IdentifierInfo *Ident = 0;

  Token attrTok;

  if (Tok.is(tok::identifier)) {
    Ident = Tok.getIdentifierInfo();
    IdentLoc = ConsumeToken();  // eat the identifier.
  }

  // Read label attributes, if present.
  llvm::OwningPtr<AttributeList> AttrList;
  if (Tok.is(tok::kw___attribute)) {
    attrTok = Tok;

    // FIXME: save these somewhere.
    AttrList.reset(ParseGNUAttributes());
  }

  if (Tok.is(tok::equal)) {
    if (AttrList)
      Diag(attrTok, diag::err_unexpected_namespace_attributes_alias);

    return ParseNamespaceAlias(NamespaceLoc, IdentLoc, Ident, DeclEnd);
  }

  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, Ident ? diag::err_expected_lbrace :
         diag::err_expected_ident_lbrace);
    return DeclPtrTy();
  }

  SourceLocation LBrace = ConsumeBrace();

  // Enter a scope for the namespace.
  ParseScope NamespaceScope(this, Scope::DeclScope);

  DeclPtrTy NamespcDecl =
    Actions.ActOnStartNamespaceDef(CurScope, IdentLoc, Ident, LBrace,
                                   AttrList.get());

  PrettyStackTraceActionsDecl CrashInfo(NamespcDecl, NamespaceLoc, Actions,
                                        PP.getSourceManager(),
                                        "parsing namespace");

  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    CXX0XAttributeList Attr;
    if (getLang().CPlusPlus0x && isCXX0XAttributeSpecifier())
      Attr = ParseCXX0XAttributes();
    ParseExternalDeclaration(Attr);
  }

  // Leave the namespace scope.
  NamespaceScope.Exit();

  SourceLocation RBraceLoc = MatchRHSPunctuation(tok::r_brace, LBrace);
  Actions.ActOnFinishNamespaceDef(NamespcDecl, RBraceLoc);

  DeclEnd = RBraceLoc;
  return NamespcDecl;
}

/// ParseNamespaceAlias - Parse the part after the '=' in a namespace
/// alias definition.
///
Parser::DeclPtrTy Parser::ParseNamespaceAlias(SourceLocation NamespaceLoc,
                                              SourceLocation AliasLoc,
                                              IdentifierInfo *Alias,
                                              SourceLocation &DeclEnd) {
  assert(Tok.is(tok::equal) && "Not equal token");

  ConsumeToken(); // eat the '='.

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteNamespaceAliasDecl(CurScope);
    ConsumeToken();
  }
  
  CXXScopeSpec SS;
  // Parse (optional) nested-name-specifier.
  ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/0, false);

  if (SS.isInvalid() || Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_namespace_name);
    // Skip to end of the definition and eat the ';'.
    SkipUntil(tok::semi);
    return DeclPtrTy();
  }

  // Parse identifier.
  IdentifierInfo *Ident = Tok.getIdentifierInfo();
  SourceLocation IdentLoc = ConsumeToken();

  // Eat the ';'.
  DeclEnd = Tok.getLocation();
  ExpectAndConsume(tok::semi, diag::err_expected_semi_after_namespace_name,
                   "", tok::semi);

  return Actions.ActOnNamespaceAliasDef(CurScope, NamespaceLoc, AliasLoc, Alias,
                                        SS, IdentLoc, Ident);
}

/// ParseLinkage - We know that the current token is a string_literal
/// and just before that, that extern was seen.
///
///       linkage-specification: [C++ 7.5p2: dcl.link]
///         'extern' string-literal '{' declaration-seq[opt] '}'
///         'extern' string-literal declaration
///
Parser::DeclPtrTy Parser::ParseLinkage(ParsingDeclSpec &DS,
                                       unsigned Context) {
  assert(Tok.is(tok::string_literal) && "Not a string literal!");
  llvm::SmallVector<char, 8> LangBuffer;
  // LangBuffer is guaranteed to be big enough.
  LangBuffer.resize(Tok.getLength());
  const char *LangBufPtr = &LangBuffer[0];
  unsigned StrSize = PP.getSpelling(Tok, LangBufPtr);

  SourceLocation Loc = ConsumeStringToken();

  ParseScope LinkageScope(this, Scope::DeclScope);
  DeclPtrTy LinkageSpec
    = Actions.ActOnStartLinkageSpecification(CurScope,
                                             /*FIXME: */SourceLocation(),
                                             Loc, LangBufPtr, StrSize,
                                       Tok.is(tok::l_brace)? Tok.getLocation()
                                                           : SourceLocation());

  CXX0XAttributeList Attr;
  if (getLang().CPlusPlus0x && isCXX0XAttributeSpecifier()) {
    Attr = ParseCXX0XAttributes();
  }
  
  if (Tok.isNot(tok::l_brace)) {
    ParseDeclarationOrFunctionDefinition(DS, Attr.AttrList);
    return Actions.ActOnFinishLinkageSpecification(CurScope, LinkageSpec,
                                                   SourceLocation());
  }

  DS.abort();

  if (Attr.HasAttr)
    Diag(Attr.Range.getBegin(), diag::err_attributes_not_allowed)
      << Attr.Range;

  SourceLocation LBrace = ConsumeBrace();
  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    CXX0XAttributeList Attr;
    if (getLang().CPlusPlus0x && isCXX0XAttributeSpecifier())
      Attr = ParseCXX0XAttributes();
    ParseExternalDeclaration(Attr);
  }

  SourceLocation RBrace = MatchRHSPunctuation(tok::r_brace, LBrace);
  return Actions.ActOnFinishLinkageSpecification(CurScope, LinkageSpec, RBrace);
}

/// ParseUsingDirectiveOrDeclaration - Parse C++ using using-declaration or
/// using-directive. Assumes that current token is 'using'.
Parser::DeclPtrTy Parser::ParseUsingDirectiveOrDeclaration(unsigned Context,
                                                     SourceLocation &DeclEnd,
                                                     CXX0XAttributeList Attr) {
  assert(Tok.is(tok::kw_using) && "Not using token");

  // Eat 'using'.
  SourceLocation UsingLoc = ConsumeToken();

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteUsing(CurScope);
    ConsumeToken();
  }
  
  if (Tok.is(tok::kw_namespace))
    // Next token after 'using' is 'namespace' so it must be using-directive
    return ParseUsingDirective(Context, UsingLoc, DeclEnd, Attr.AttrList);

  if (Attr.HasAttr)
    Diag(Attr.Range.getBegin(), diag::err_attributes_not_allowed)
      << Attr.Range;

  // Otherwise, it must be using-declaration.
  // Ignore illegal attributes (the caller should already have issued an error.
  return ParseUsingDeclaration(Context, UsingLoc, DeclEnd);
}

/// ParseUsingDirective - Parse C++ using-directive, assumes
/// that current token is 'namespace' and 'using' was already parsed.
///
///       using-directive: [C++ 7.3.p4: namespace.udir]
///        'using' 'namespace' ::[opt] nested-name-specifier[opt]
///                 namespace-name ;
/// [GNU] using-directive:
///        'using' 'namespace' ::[opt] nested-name-specifier[opt]
///                 namespace-name attributes[opt] ;
///
Parser::DeclPtrTy Parser::ParseUsingDirective(unsigned Context,
                                              SourceLocation UsingLoc,
                                              SourceLocation &DeclEnd,
                                              AttributeList *Attr) {
  assert(Tok.is(tok::kw_namespace) && "Not 'namespace' token");

  // Eat 'namespace'.
  SourceLocation NamespcLoc = ConsumeToken();

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteUsingDirective(CurScope);
    ConsumeToken();
  }
  
  CXXScopeSpec SS;
  // Parse (optional) nested-name-specifier.
  ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/0, false);

  IdentifierInfo *NamespcName = 0;
  SourceLocation IdentLoc = SourceLocation();

  // Parse namespace-name.
  if (SS.isInvalid() || Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_namespace_name);
    // If there was invalid namespace name, skip to end of decl, and eat ';'.
    SkipUntil(tok::semi);
    // FIXME: Are there cases, when we would like to call ActOnUsingDirective?
    return DeclPtrTy();
  }

  // Parse identifier.
  NamespcName = Tok.getIdentifierInfo();
  IdentLoc = ConsumeToken();

  // Parse (optional) attributes (most likely GNU strong-using extension).
  bool GNUAttr = false;
  if (Tok.is(tok::kw___attribute)) {
    GNUAttr = true;
    Attr = addAttributeLists(Attr, ParseGNUAttributes());
  }

  // Eat ';'.
  DeclEnd = Tok.getLocation();
  ExpectAndConsume(tok::semi,
                   GNUAttr ? diag::err_expected_semi_after_attribute_list :
                   diag::err_expected_semi_after_namespace_name, "", tok::semi);

  return Actions.ActOnUsingDirective(CurScope, UsingLoc, NamespcLoc, SS,
                                      IdentLoc, NamespcName, Attr);
}

/// ParseUsingDeclaration - Parse C++ using-declaration. Assumes that
/// 'using' was already seen.
///
///     using-declaration: [C++ 7.3.p3: namespace.udecl]
///       'using' 'typename'[opt] ::[opt] nested-name-specifier
///               unqualified-id
///       'using' :: unqualified-id
///
Parser::DeclPtrTy Parser::ParseUsingDeclaration(unsigned Context,
                                                SourceLocation UsingLoc,
                                                SourceLocation &DeclEnd,
                                                AccessSpecifier AS) {
  CXXScopeSpec SS;
  SourceLocation TypenameLoc;
  bool IsTypeName;

  // Ignore optional 'typename'.
  // FIXME: This is wrong; we should parse this as a typename-specifier.
  if (Tok.is(tok::kw_typename)) {
    TypenameLoc = Tok.getLocation();
    ConsumeToken();
    IsTypeName = true;
  }
  else
    IsTypeName = false;

  // Parse nested-name-specifier.
  ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/0, false);

  // Check nested-name specifier.
  if (SS.isInvalid()) {
    SkipUntil(tok::semi);
    return DeclPtrTy();
  }

  // Parse the unqualified-id. We allow parsing of both constructor and 
  // destructor names and allow the action module to diagnose any semantic
  // errors.
  UnqualifiedId Name;
  if (ParseUnqualifiedId(SS, 
                         /*EnteringContext=*/false,
                         /*AllowDestructorName=*/true,
                         /*AllowConstructorName=*/true, 
                         /*ObjectType=*/0, 
                         Name)) {
    SkipUntil(tok::semi);
    return DeclPtrTy();
  }
  
  // Parse (optional) attributes (most likely GNU strong-using extension).
  llvm::OwningPtr<AttributeList> AttrList;
  if (Tok.is(tok::kw___attribute))
    AttrList.reset(ParseGNUAttributes());

  // Eat ';'.
  DeclEnd = Tok.getLocation();
  ExpectAndConsume(tok::semi, diag::err_expected_semi_after,
                   AttrList ? "attributes list" : "using declaration", 
                   tok::semi);

  return Actions.ActOnUsingDeclaration(CurScope, AS, true, UsingLoc, SS, Name,
                                       AttrList.get(), IsTypeName, TypenameLoc);
}

/// ParseStaticAssertDeclaration - Parse C++0x static_assert-declaratoion.
///
///      static_assert-declaration:
///        static_assert ( constant-expression  ,  string-literal  ) ;
///
Parser::DeclPtrTy Parser::ParseStaticAssertDeclaration(SourceLocation &DeclEnd){
  assert(Tok.is(tok::kw_static_assert) && "Not a static_assert declaration");
  SourceLocation StaticAssertLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen);
    return DeclPtrTy();
  }

  SourceLocation LParenLoc = ConsumeParen();

  OwningExprResult AssertExpr(ParseConstantExpression());
  if (AssertExpr.isInvalid()) {
    SkipUntil(tok::semi);
    return DeclPtrTy();
  }

  if (ExpectAndConsume(tok::comma, diag::err_expected_comma, "", tok::semi))
    return DeclPtrTy();

  if (Tok.isNot(tok::string_literal)) {
    Diag(Tok, diag::err_expected_string_literal);
    SkipUntil(tok::semi);
    return DeclPtrTy();
  }

  OwningExprResult AssertMessage(ParseStringLiteralExpression());
  if (AssertMessage.isInvalid())
    return DeclPtrTy();

  MatchRHSPunctuation(tok::r_paren, LParenLoc);

  DeclEnd = Tok.getLocation();
  ExpectAndConsume(tok::semi, diag::err_expected_semi_after_static_assert);

  return Actions.ActOnStaticAssertDeclaration(StaticAssertLoc, move(AssertExpr),
                                              move(AssertMessage));
}

/// ParseDecltypeSpecifier - Parse a C++0x decltype specifier.
///
/// 'decltype' ( expression )
///
void Parser::ParseDecltypeSpecifier(DeclSpec &DS) {
  assert(Tok.is(tok::kw_decltype) && "Not a decltype specifier");

  SourceLocation StartLoc = ConsumeToken();
  SourceLocation LParenLoc = Tok.getLocation();

  if (ExpectAndConsume(tok::l_paren, diag::err_expected_lparen_after,
                       "decltype")) {
    SkipUntil(tok::r_paren);
    return;
  }

  // Parse the expression

  // C++0x [dcl.type.simple]p4:
  //   The operand of the decltype specifier is an unevaluated operand.
  EnterExpressionEvaluationContext Unevaluated(Actions,
                                               Action::Unevaluated);
  OwningExprResult Result = ParseExpression();
  if (Result.isInvalid()) {
    SkipUntil(tok::r_paren);
    return;
  }

  // Match the ')'
  SourceLocation RParenLoc;
  if (Tok.is(tok::r_paren))
    RParenLoc = ConsumeParen();
  else
    MatchRHSPunctuation(tok::r_paren, LParenLoc);

  if (RParenLoc.isInvalid())
    return;

  const char *PrevSpec = 0;
  unsigned DiagID;
  // Check for duplicate type specifiers (e.g. "int decltype(a)").
  if (DS.SetTypeSpecType(DeclSpec::TST_decltype, StartLoc, PrevSpec,
                         DiagID, Result.release()))
    Diag(StartLoc, DiagID) << PrevSpec;
}

/// ParseClassName - Parse a C++ class-name, which names a class. Note
/// that we only check that the result names a type; semantic analysis
/// will need to verify that the type names a class. The result is
/// either a type or NULL, depending on whether a type name was
/// found.
///
///       class-name: [C++ 9.1]
///         identifier
///         simple-template-id
///
Parser::TypeResult Parser::ParseClassName(SourceLocation &EndLocation,
                                          const CXXScopeSpec *SS) {
  // Check whether we have a template-id that names a type.
  if (Tok.is(tok::annot_template_id)) {
    TemplateIdAnnotation *TemplateId
      = static_cast<TemplateIdAnnotation *>(Tok.getAnnotationValue());
    if (TemplateId->Kind == TNK_Type_template ||
        TemplateId->Kind == TNK_Dependent_template_name) {
      AnnotateTemplateIdTokenAsType(SS);

      assert(Tok.is(tok::annot_typename) && "template-id -> type failed");
      TypeTy *Type = Tok.getAnnotationValue();
      EndLocation = Tok.getAnnotationEndLoc();
      ConsumeToken();

      if (Type)
        return Type;
      return true;
    }

    // Fall through to produce an error below.
  }

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_class_name);
    return true;
  }

  IdentifierInfo *Id = Tok.getIdentifierInfo();
  SourceLocation IdLoc = ConsumeToken();

  if (Tok.is(tok::less)) {
    // It looks the user intended to write a template-id here, but the
    // template-name was wrong. Try to fix that.
    TemplateNameKind TNK = TNK_Type_template;
    TemplateTy Template;
    if (!Actions.DiagnoseUnknownTemplateName(*Id, IdLoc, CurScope,
                                             SS, Template, TNK)) {
      Diag(IdLoc, diag::err_unknown_template_name)
        << Id;
    }
    
    if (!Template)
      return true;

    // Form the template name 
    UnqualifiedId TemplateName;
    TemplateName.setIdentifier(Id, IdLoc);
    
    // Parse the full template-id, then turn it into a type.
    if (AnnotateTemplateIdToken(Template, TNK, SS, TemplateName,
                                SourceLocation(), true))
      return true;
    if (TNK == TNK_Dependent_template_name)
      AnnotateTemplateIdTokenAsType(SS);
    
    // If we didn't end up with a typename token, there's nothing more we
    // can do.
    if (Tok.isNot(tok::annot_typename))
      return true;
    
    // Retrieve the type from the annotation token, consume that token, and
    // return.
    EndLocation = Tok.getAnnotationEndLoc();
    TypeTy *Type = Tok.getAnnotationValue();
    ConsumeToken();
    return Type;
  }

  // We have an identifier; check whether it is actually a type.
  TypeTy *Type = Actions.getTypeName(*Id, IdLoc, CurScope, SS, true);
  if (!Type) {    
    Diag(IdLoc, diag::err_expected_class_name);
    return true;
  }

  // Consume the identifier.
  EndLocation = IdLoc;
  return Type;
}

/// ParseClassSpecifier - Parse a C++ class-specifier [C++ class] or
/// elaborated-type-specifier [C++ dcl.type.elab]; we can't tell which
/// until we reach the start of a definition or see a token that
/// cannot start a definition. If SuppressDeclarations is true, we do know.
///
///       class-specifier: [C++ class]
///         class-head '{' member-specification[opt] '}'
///         class-head '{' member-specification[opt] '}' attributes[opt]
///       class-head:
///         class-key identifier[opt] base-clause[opt]
///         class-key nested-name-specifier identifier base-clause[opt]
///         class-key nested-name-specifier[opt] simple-template-id
///                          base-clause[opt]
/// [GNU]   class-key attributes[opt] identifier[opt] base-clause[opt]
/// [GNU]   class-key attributes[opt] nested-name-specifier
///                          identifier base-clause[opt]
/// [GNU]   class-key attributes[opt] nested-name-specifier[opt]
///                          simple-template-id base-clause[opt]
///       class-key:
///         'class'
///         'struct'
///         'union'
///
///       elaborated-type-specifier: [C++ dcl.type.elab]
///         class-key ::[opt] nested-name-specifier[opt] identifier
///         class-key ::[opt] nested-name-specifier[opt] 'template'[opt]
///                          simple-template-id
///
///  Note that the C++ class-specifier and elaborated-type-specifier,
///  together, subsume the C99 struct-or-union-specifier:
///
///       struct-or-union-specifier: [C99 6.7.2.1]
///         struct-or-union identifier[opt] '{' struct-contents '}'
///         struct-or-union identifier
/// [GNU]   struct-or-union attributes[opt] identifier[opt] '{' struct-contents
///                                                         '}' attributes[opt]
/// [GNU]   struct-or-union attributes[opt] identifier
///       struct-or-union:
///         'struct'
///         'union'
void Parser::ParseClassSpecifier(tok::TokenKind TagTokKind,
                                 SourceLocation StartLoc, DeclSpec &DS,
                                 const ParsedTemplateInfo &TemplateInfo,
                                 AccessSpecifier AS, bool SuppressDeclarations){
  DeclSpec::TST TagType;
  if (TagTokKind == tok::kw_struct)
    TagType = DeclSpec::TST_struct;
  else if (TagTokKind == tok::kw_class)
    TagType = DeclSpec::TST_class;
  else {
    assert(TagTokKind == tok::kw_union && "Not a class specifier");
    TagType = DeclSpec::TST_union;
  }

  if (Tok.is(tok::code_completion)) {
    // Code completion for a struct, class, or union name.
    Actions.CodeCompleteTag(CurScope, TagType);
    ConsumeToken();
  }
  
  AttributeList *AttrList = 0;
  // If attributes exist after tag, parse them.
  if (Tok.is(tok::kw___attribute))
    AttrList = ParseGNUAttributes();

  // If declspecs exist after tag, parse them.
  if (Tok.is(tok::kw___declspec))
    AttrList = ParseMicrosoftDeclSpec(AttrList);
  
  // If C++0x attributes exist here, parse them.
  // FIXME: Are we consistent with the ordering of parsing of different
  // styles of attributes?
  if (isCXX0XAttributeSpecifier())
    AttrList = addAttributeLists(AttrList, ParseCXX0XAttributes().AttrList);

  if (TagType == DeclSpec::TST_struct && Tok.is(tok::kw___is_pod)) {
    // GNU libstdc++ 4.2 uses __is_pod as the name of a struct template, but
    // __is_pod is a keyword in GCC >= 4.3. Therefore, when we see the
    // token sequence "struct __is_pod", make __is_pod into a normal
    // identifier rather than a keyword, to allow libstdc++ 4.2 to work
    // properly.
    Tok.getIdentifierInfo()->setTokenID(tok::identifier);
    Tok.setKind(tok::identifier);
  }

  if (TagType == DeclSpec::TST_struct && Tok.is(tok::kw___is_empty)) {
    // GNU libstdc++ 4.2 uses __is_empty as the name of a struct template, but
    // __is_empty is a keyword in GCC >= 4.3. Therefore, when we see the
    // token sequence "struct __is_empty", make __is_empty into a normal
    // identifier rather than a keyword, to allow libstdc++ 4.2 to work
    // properly.
    Tok.getIdentifierInfo()->setTokenID(tok::identifier);
    Tok.setKind(tok::identifier);
  }

  // Parse the (optional) nested-name-specifier.
  CXXScopeSpec &SS = DS.getTypeSpecScope();
  if (getLang().CPlusPlus) {
    // "FOO : BAR" is not a potential typo for "FOO::BAR".
    ColonProtectionRAIIObject X(*this);
    
    if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/0, true))
      if (Tok.isNot(tok::identifier) && Tok.isNot(tok::annot_template_id))
        Diag(Tok, diag::err_expected_ident);
  }

  TemplateParameterLists *TemplateParams = TemplateInfo.TemplateParams;

  // Parse the (optional) class name or simple-template-id.
  IdentifierInfo *Name = 0;
  SourceLocation NameLoc;
  TemplateIdAnnotation *TemplateId = 0;
  if (Tok.is(tok::identifier)) {
    Name = Tok.getIdentifierInfo();
    NameLoc = ConsumeToken();
    
    if (Tok.is(tok::less)) {
      // The name was supposed to refer to a template, but didn't. 
      // Eat the template argument list and try to continue parsing this as
      // a class (or template thereof).
      TemplateArgList TemplateArgs;
      SourceLocation LAngleLoc, RAngleLoc;
      if (ParseTemplateIdAfterTemplateName(TemplateTy(), NameLoc, &SS, 
                                           true, LAngleLoc,
                                           TemplateArgs, RAngleLoc)) {
        // We couldn't parse the template argument list at all, so don't
        // try to give any location information for the list.
        LAngleLoc = RAngleLoc = SourceLocation();
      }
      
      Diag(NameLoc, diag::err_explicit_spec_non_template)
        << (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation)
        << (TagType == DeclSpec::TST_class? 0
            : TagType == DeclSpec::TST_struct? 1
            : 2)
        << Name
        << SourceRange(LAngleLoc, RAngleLoc);
      
      // Strip off the last template parameter list if it was empty, since 
      // we've removed its template argument list.
      if (TemplateParams && TemplateInfo.LastParameterListWasEmpty) {
        if (TemplateParams && TemplateParams->size() > 1) {
          TemplateParams->pop_back();
        } else {
          TemplateParams = 0;
          const_cast<ParsedTemplateInfo&>(TemplateInfo).Kind 
            = ParsedTemplateInfo::NonTemplate;
        }
      } else if (TemplateInfo.Kind
                                == ParsedTemplateInfo::ExplicitInstantiation) {
        // Pretend this is just a forward declaration.
        TemplateParams = 0;
        const_cast<ParsedTemplateInfo&>(TemplateInfo).Kind 
          = ParsedTemplateInfo::NonTemplate;
        const_cast<ParsedTemplateInfo&>(TemplateInfo).TemplateLoc 
          = SourceLocation();
        const_cast<ParsedTemplateInfo&>(TemplateInfo).ExternLoc
          = SourceLocation();
      }
        
      
    }
  } else if (Tok.is(tok::annot_template_id)) {
    TemplateId = static_cast<TemplateIdAnnotation *>(Tok.getAnnotationValue());
    NameLoc = ConsumeToken();

    if (TemplateId->Kind != TNK_Type_template) {
      // The template-name in the simple-template-id refers to
      // something other than a class template. Give an appropriate
      // error message and skip to the ';'.
      SourceRange Range(NameLoc);
      if (SS.isNotEmpty())
        Range.setBegin(SS.getBeginLoc());

      Diag(TemplateId->LAngleLoc, diag::err_template_spec_syntax_non_template)
        << Name << static_cast<int>(TemplateId->Kind) << Range;

      DS.SetTypeSpecError();
      SkipUntil(tok::semi, false, true);
      TemplateId->Destroy();
      return;
    }
  }

  // There are four options here.  If we have 'struct foo;', then this
  // is either a forward declaration or a friend declaration, which
  // have to be treated differently.  If we have 'struct foo {...' or
  // 'struct foo :...' then this is a definition. Otherwise we have
  // something like 'struct foo xyz', a reference.
  // However, in some contexts, things look like declarations but are just
  // references, e.g.
  // new struct s;
  // or
  // &T::operator struct s;
  // For these, SuppressDeclarations is true.
  Action::TagUseKind TUK;
  if (SuppressDeclarations)
    TUK = Action::TUK_Reference;
  else if (Tok.is(tok::l_brace) || (getLang().CPlusPlus && Tok.is(tok::colon))){
    if (DS.isFriendSpecified()) {
      // C++ [class.friend]p2:
      //   A class shall not be defined in a friend declaration.
      Diag(Tok.getLocation(), diag::err_friend_decl_defines_class)
        << SourceRange(DS.getFriendSpecLoc());

      // Skip everything up to the semicolon, so that this looks like a proper
      // friend class (or template thereof) declaration.
      SkipUntil(tok::semi, true, true);
      TUK = Action::TUK_Friend;
    } else {
      // Okay, this is a class definition.
      TUK = Action::TUK_Definition;
    }
  } else if (Tok.is(tok::semi))
    TUK = DS.isFriendSpecified() ? Action::TUK_Friend : Action::TUK_Declaration;
  else
    TUK = Action::TUK_Reference;

  if (!Name && !TemplateId && TUK != Action::TUK_Definition) {
    // We have a declaration or reference to an anonymous class.
    Diag(StartLoc, diag::err_anon_type_definition)
      << DeclSpec::getSpecifierName(TagType);

    SkipUntil(tok::comma, true);

    if (TemplateId)
      TemplateId->Destroy();
    return;
  }

  // Create the tag portion of the class or class template.
  Action::DeclResult TagOrTempResult = true; // invalid
  Action::TypeResult TypeResult = true; // invalid

  // FIXME: When TUK == TUK_Reference and we have a template-id, we need
  // to turn that template-id into a type.

  bool Owned = false;
  if (TemplateId) {
    // Explicit specialization, class template partial specialization,
    // or explicit instantiation.
    ASTTemplateArgsPtr TemplateArgsPtr(Actions,
                                       TemplateId->getTemplateArgs(),
                                       TemplateId->NumArgs);
    if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation &&
        TUK == Action::TUK_Declaration) {
      // This is an explicit instantiation of a class template.
      TagOrTempResult
        = Actions.ActOnExplicitInstantiation(CurScope,
                                             TemplateInfo.ExternLoc,
                                             TemplateInfo.TemplateLoc,
                                             TagType,
                                             StartLoc,
                                             SS,
                                     TemplateTy::make(TemplateId->Template),
                                             TemplateId->TemplateNameLoc,
                                             TemplateId->LAngleLoc,
                                             TemplateArgsPtr,
                                             TemplateId->RAngleLoc,
                                             AttrList);
    } else if (TUK == Action::TUK_Reference) {
      TypeResult
        = Actions.ActOnTemplateIdType(TemplateTy::make(TemplateId->Template),
                                      TemplateId->TemplateNameLoc,
                                      TemplateId->LAngleLoc,
                                      TemplateArgsPtr,
                                      TemplateId->RAngleLoc);

      TypeResult = Actions.ActOnTagTemplateIdType(TypeResult, TUK,
                                                  TagType, StartLoc);
    } else {
      // This is an explicit specialization or a class template
      // partial specialization.
      TemplateParameterLists FakedParamLists;

      if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation) {
        // This looks like an explicit instantiation, because we have
        // something like
        //
        //   template class Foo<X>
        //
        // but it actually has a definition. Most likely, this was
        // meant to be an explicit specialization, but the user forgot
        // the '<>' after 'template'.
        assert(TUK == Action::TUK_Definition && "Expected a definition here");

        SourceLocation LAngleLoc
          = PP.getLocForEndOfToken(TemplateInfo.TemplateLoc);
        Diag(TemplateId->TemplateNameLoc,
             diag::err_explicit_instantiation_with_definition)
          << SourceRange(TemplateInfo.TemplateLoc)
          << CodeModificationHint::CreateInsertion(LAngleLoc, "<>");

        // Create a fake template parameter list that contains only
        // "template<>", so that we treat this construct as a class
        // template specialization.
        FakedParamLists.push_back(
          Actions.ActOnTemplateParameterList(0, SourceLocation(),
                                             TemplateInfo.TemplateLoc,
                                             LAngleLoc,
                                             0, 0,
                                             LAngleLoc));
        TemplateParams = &FakedParamLists;
      }

      // Build the class template specialization.
      TagOrTempResult
        = Actions.ActOnClassTemplateSpecialization(CurScope, TagType, TUK,
                       StartLoc, SS,
                       TemplateTy::make(TemplateId->Template),
                       TemplateId->TemplateNameLoc,
                       TemplateId->LAngleLoc,
                       TemplateArgsPtr,
                       TemplateId->RAngleLoc,
                       AttrList,
                       Action::MultiTemplateParamsArg(Actions,
                                    TemplateParams? &(*TemplateParams)[0] : 0,
                                 TemplateParams? TemplateParams->size() : 0));
    }
    TemplateId->Destroy();
  } else if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation &&
             TUK == Action::TUK_Declaration) {
    // Explicit instantiation of a member of a class template
    // specialization, e.g.,
    //
    //   template struct Outer<int>::Inner;
    //
    TagOrTempResult
      = Actions.ActOnExplicitInstantiation(CurScope,
                                           TemplateInfo.ExternLoc,
                                           TemplateInfo.TemplateLoc,
                                           TagType, StartLoc, SS, Name,
                                           NameLoc, AttrList);
  } else {
    if (TemplateInfo.Kind == ParsedTemplateInfo::ExplicitInstantiation &&
        TUK == Action::TUK_Definition) {
      // FIXME: Diagnose this particular error.
    }

    bool IsDependent = false;

    // Declaration or definition of a class type
    TagOrTempResult = Actions.ActOnTag(CurScope, TagType, TUK, StartLoc, SS,
                                       Name, NameLoc, AttrList, AS,
                                  Action::MultiTemplateParamsArg(Actions,
                                    TemplateParams? &(*TemplateParams)[0] : 0,
                                    TemplateParams? TemplateParams->size() : 0),
                                       Owned, IsDependent);

    // If ActOnTag said the type was dependent, try again with the
    // less common call.
    if (IsDependent)
      TypeResult = Actions.ActOnDependentTag(CurScope, TagType, TUK,
                                             SS, Name, StartLoc, NameLoc);      
  }

  // If there is a body, parse it and inform the actions module.
  if (TUK == Action::TUK_Definition) {
    assert(Tok.is(tok::l_brace) ||
           (getLang().CPlusPlus && Tok.is(tok::colon)));
    if (getLang().CPlusPlus)
      ParseCXXMemberSpecification(StartLoc, TagType, TagOrTempResult.get());
    else
      ParseStructUnionBody(StartLoc, TagType, TagOrTempResult.get());
  }

  void *Result;
  if (!TypeResult.isInvalid()) {
    TagType = DeclSpec::TST_typename;
    Result = TypeResult.get();
    Owned = false;
  } else if (!TagOrTempResult.isInvalid()) {
    Result = TagOrTempResult.get().getAs<void>();
  } else {
    DS.SetTypeSpecError();
    return;
  }

  const char *PrevSpec = 0;
  unsigned DiagID;

  // FIXME: The DeclSpec should keep the locations of both the keyword and the
  // name (if there is one).
  SourceLocation TSTLoc = NameLoc.isValid()? NameLoc : StartLoc;
  
  if (DS.SetTypeSpecType(TagType, TSTLoc, PrevSpec, DiagID,
                         Result, Owned))
    Diag(StartLoc, DiagID) << PrevSpec;
  
  // At this point, we've successfully parsed a class-specifier in 'definition'
  // form (e.g. "struct foo { int x; }".  While we could just return here, we're
  // going to look at what comes after it to improve error recovery.  If an
  // impossible token occurs next, we assume that the programmer forgot a ; at
  // the end of the declaration and recover that way.
  //
  // This switch enumerates the valid "follow" set for definition.
  if (TUK == Action::TUK_Definition) {
    switch (Tok.getKind()) {
    case tok::semi:               // struct foo {...} ;
    case tok::star:               // struct foo {...} *         P;
    case tok::amp:                // struct foo {...} &         R = ...
    case tok::identifier:         // struct foo {...} V         ;
    case tok::r_paren:            //(struct foo {...} )         {4}
    case tok::annot_cxxscope:     // struct foo {...} a::       b;
    case tok::annot_typename:     // struct foo {...} a         ::b;
    case tok::annot_template_id:  // struct foo {...} a<int>    ::b;
    case tok::l_paren:            // struct foo {...} (         x);
    case tok::comma:              // __builtin_offsetof(struct foo{...} ,
    // Storage-class specifiers
    case tok::kw_static:          // struct foo {...} static    x;
    case tok::kw_extern:          // struct foo {...} extern    x;
    case tok::kw_typedef:         // struct foo {...} typedef   x;
    case tok::kw_register:        // struct foo {...} register  x;
    case tok::kw_auto:            // struct foo {...} auto      x;
    // Type qualifiers
    case tok::kw_const:           // struct foo {...} const     x;
    case tok::kw_volatile:        // struct foo {...} volatile  x;
    case tok::kw_restrict:        // struct foo {...} restrict  x;
    case tok::kw_inline:          // struct foo {...} inline    foo() {};
      break;
        
    case tok::r_brace:  // struct bar { struct foo {...} } 
      // Missing ';' at end of struct is accepted as an extension in C mode.
      if (!getLang().CPlusPlus) break;
      // FALL THROUGH.
    default:
      ExpectAndConsume(tok::semi, diag::err_expected_semi_after_tagdecl,
                       TagType == DeclSpec::TST_class ? "class"
                       : TagType == DeclSpec::TST_struct? "struct" : "union");
      // Push this token back into the preprocessor and change our current token
      // to ';' so that the rest of the code recovers as though there were an
      // ';' after the definition.
      PP.EnterToken(Tok);
      Tok.setKind(tok::semi);  
      break;
    }
  }
}

/// ParseBaseClause - Parse the base-clause of a C++ class [C++ class.derived].
///
///       base-clause : [C++ class.derived]
///         ':' base-specifier-list
///       base-specifier-list:
///         base-specifier '...'[opt]
///         base-specifier-list ',' base-specifier '...'[opt]
void Parser::ParseBaseClause(DeclPtrTy ClassDecl) {
  assert(Tok.is(tok::colon) && "Not a base clause");
  ConsumeToken();

  // Build up an array of parsed base specifiers.
  llvm::SmallVector<BaseTy *, 8> BaseInfo;

  while (true) {
    // Parse a base-specifier.
    BaseResult Result = ParseBaseSpecifier(ClassDecl);
    if (Result.isInvalid()) {
      // Skip the rest of this base specifier, up until the comma or
      // opening brace.
      SkipUntil(tok::comma, tok::l_brace, true, true);
    } else {
      // Add this to our array of base specifiers.
      BaseInfo.push_back(Result.get());
    }

    // If the next token is a comma, consume it and keep reading
    // base-specifiers.
    if (Tok.isNot(tok::comma)) break;

    // Consume the comma.
    ConsumeToken();
  }

  // Attach the base specifiers
  Actions.ActOnBaseSpecifiers(ClassDecl, BaseInfo.data(), BaseInfo.size());
}

/// ParseBaseSpecifier - Parse a C++ base-specifier. A base-specifier is
/// one entry in the base class list of a class specifier, for example:
///    class foo : public bar, virtual private baz {
/// 'public bar' and 'virtual private baz' are each base-specifiers.
///
///       base-specifier: [C++ class.derived]
///         ::[opt] nested-name-specifier[opt] class-name
///         'virtual' access-specifier[opt] ::[opt] nested-name-specifier[opt]
///                        class-name
///         access-specifier 'virtual'[opt] ::[opt] nested-name-specifier[opt]
///                        class-name
Parser::BaseResult Parser::ParseBaseSpecifier(DeclPtrTy ClassDecl) {
  bool IsVirtual = false;
  SourceLocation StartLoc = Tok.getLocation();

  // Parse the 'virtual' keyword.
  if (Tok.is(tok::kw_virtual))  {
    ConsumeToken();
    IsVirtual = true;
  }

  // Parse an (optional) access specifier.
  AccessSpecifier Access = getAccessSpecifierIfPresent();
  if (Access != AS_none)
    ConsumeToken();

  // Parse the 'virtual' keyword (again!), in case it came after the
  // access specifier.
  if (Tok.is(tok::kw_virtual))  {
    SourceLocation VirtualLoc = ConsumeToken();
    if (IsVirtual) {
      // Complain about duplicate 'virtual'
      Diag(VirtualLoc, diag::err_dup_virtual)
        << CodeModificationHint::CreateRemoval(VirtualLoc);
    }

    IsVirtual = true;
  }

  // Parse optional '::' and optional nested-name-specifier.
  CXXScopeSpec SS;
  ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/0, true);

  // The location of the base class itself.
  SourceLocation BaseLoc = Tok.getLocation();

  // Parse the class-name.
  SourceLocation EndLocation;
  TypeResult BaseType = ParseClassName(EndLocation, &SS);
  if (BaseType.isInvalid())
    return true;

  // Find the complete source range for the base-specifier.
  SourceRange Range(StartLoc, EndLocation);

  // Notify semantic analysis that we have parsed a complete
  // base-specifier.
  return Actions.ActOnBaseSpecifier(ClassDecl, Range, IsVirtual, Access,
                                    BaseType.get(), BaseLoc);
}

/// getAccessSpecifierIfPresent - Determine whether the next token is
/// a C++ access-specifier.
///
///       access-specifier: [C++ class.derived]
///         'private'
///         'protected'
///         'public'
AccessSpecifier Parser::getAccessSpecifierIfPresent() const {
  switch (Tok.getKind()) {
  default: return AS_none;
  case tok::kw_private: return AS_private;
  case tok::kw_protected: return AS_protected;
  case tok::kw_public: return AS_public;
  }
}

void Parser::HandleMemberFunctionDefaultArgs(Declarator& DeclaratorInfo,
                                             DeclPtrTy ThisDecl) {
  // We just declared a member function. If this member function
  // has any default arguments, we'll need to parse them later.
  LateParsedMethodDeclaration *LateMethod = 0;
  DeclaratorChunk::FunctionTypeInfo &FTI
    = DeclaratorInfo.getTypeObject(0).Fun;
  for (unsigned ParamIdx = 0; ParamIdx < FTI.NumArgs; ++ParamIdx) {
    if (LateMethod || FTI.ArgInfo[ParamIdx].DefaultArgTokens) {
      if (!LateMethod) {
        // Push this method onto the stack of late-parsed method
        // declarations.
        getCurrentClass().MethodDecls.push_back(
                                LateParsedMethodDeclaration(ThisDecl));
        LateMethod = &getCurrentClass().MethodDecls.back();
        LateMethod->TemplateScope = CurScope->isTemplateParamScope();

        // Add all of the parameters prior to this one (they don't
        // have default arguments).
        LateMethod->DefaultArgs.reserve(FTI.NumArgs);
        for (unsigned I = 0; I < ParamIdx; ++I)
          LateMethod->DefaultArgs.push_back(
                    LateParsedDefaultArgument(FTI.ArgInfo[ParamIdx].Param));
      }

      // Add this parameter to the list of parameters (it or may
      // not have a default argument).
      LateMethod->DefaultArgs.push_back(
        LateParsedDefaultArgument(FTI.ArgInfo[ParamIdx].Param,
                                  FTI.ArgInfo[ParamIdx].DefaultArgTokens));
    }
  }
}

/// ParseCXXClassMemberDeclaration - Parse a C++ class member declaration.
///
///       member-declaration:
///         decl-specifier-seq[opt] member-declarator-list[opt] ';'
///         function-definition ';'[opt]
///         ::[opt] nested-name-specifier template[opt] unqualified-id ';'[TODO]
///         using-declaration                                            [TODO]
/// [C++0x] static_assert-declaration
///         template-declaration
/// [GNU]   '__extension__' member-declaration
///
///       member-declarator-list:
///         member-declarator
///         member-declarator-list ',' member-declarator
///
///       member-declarator:
///         declarator pure-specifier[opt]
///         declarator constant-initializer[opt]
///         identifier[opt] ':' constant-expression
///
///       pure-specifier:
///         '= 0'
///
///       constant-initializer:
///         '=' constant-expression
///
void Parser::ParseCXXClassMemberDeclaration(AccessSpecifier AS,
                                       const ParsedTemplateInfo &TemplateInfo) {
  // Access declarations.
  if (!TemplateInfo.Kind &&
      (Tok.is(tok::identifier) || Tok.is(tok::coloncolon)) &&
      TryAnnotateCXXScopeToken() &&
      Tok.is(tok::annot_cxxscope)) {
    bool isAccessDecl = false;
    if (NextToken().is(tok::identifier))
      isAccessDecl = GetLookAheadToken(2).is(tok::semi);
    else
      isAccessDecl = NextToken().is(tok::kw_operator);

    if (isAccessDecl) {
      // Collect the scope specifier token we annotated earlier.
      CXXScopeSpec SS;
      ParseOptionalCXXScopeSpecifier(SS, /*ObjectType*/ 0, false);

      // Try to parse an unqualified-id.
      UnqualifiedId Name;
      if (ParseUnqualifiedId(SS, false, true, true, /*ObjectType*/ 0, Name)) {
        SkipUntil(tok::semi);
        return;
      }

      // TODO: recover from mistakenly-qualified operator declarations.
      if (ExpectAndConsume(tok::semi,
                           diag::err_expected_semi_after,
                           "access declaration",
                           tok::semi))
        return;

      Actions.ActOnUsingDeclaration(CurScope, AS,
                                    false, SourceLocation(),
                                    SS, Name,
                                    /* AttrList */ 0,
                                    /* IsTypeName */ false,
                                    SourceLocation());
      return;
    }
  }

  // static_assert-declaration
  if (Tok.is(tok::kw_static_assert)) {
    // FIXME: Check for templates
    SourceLocation DeclEnd;
    ParseStaticAssertDeclaration(DeclEnd);
    return;
  }

  if (Tok.is(tok::kw_template)) {
    assert(!TemplateInfo.TemplateParams &&
           "Nested template improperly parsed?");
    SourceLocation DeclEnd;
    ParseDeclarationStartingWithTemplate(Declarator::MemberContext, DeclEnd,
                                         AS);
    return;
  }

  // Handle:  member-declaration ::= '__extension__' member-declaration
  if (Tok.is(tok::kw___extension__)) {
    // __extension__ silences extension warnings in the subexpression.
    ExtensionRAIIObject O(Diags);  // Use RAII to do this.
    ConsumeToken();
    return ParseCXXClassMemberDeclaration(AS, TemplateInfo);
  }

  // Don't parse FOO:BAR as if it were a typo for FOO::BAR, in this context it
  // is a bitfield.
  ColonProtectionRAIIObject X(*this);
  
  CXX0XAttributeList AttrList;
  // Optional C++0x attribute-specifier
  if (getLang().CPlusPlus0x && isCXX0XAttributeSpecifier())
    AttrList = ParseCXX0XAttributes();

  if (Tok.is(tok::kw_using)) {
    // FIXME: Check for template aliases
    
    if (AttrList.HasAttr)
      Diag(AttrList.Range.getBegin(), diag::err_attributes_not_allowed)
        << AttrList.Range;

    // Eat 'using'.
    SourceLocation UsingLoc = ConsumeToken();

    if (Tok.is(tok::kw_namespace)) {
      Diag(UsingLoc, diag::err_using_namespace_in_class);
      SkipUntil(tok::semi, true, true);
    } else {
      SourceLocation DeclEnd;
      // Otherwise, it must be using-declaration.
      ParseUsingDeclaration(Declarator::MemberContext, UsingLoc, DeclEnd, AS);
    }
    return;
  }

  SourceLocation DSStart = Tok.getLocation();
  // decl-specifier-seq:
  // Parse the common declaration-specifiers piece.
  ParsingDeclSpec DS(*this);
  DS.AddAttributes(AttrList.AttrList);
  ParseDeclarationSpecifiers(DS, TemplateInfo, AS, DSC_class);

  Action::MultiTemplateParamsArg TemplateParams(Actions,
      TemplateInfo.TemplateParams? TemplateInfo.TemplateParams->data() : 0,
      TemplateInfo.TemplateParams? TemplateInfo.TemplateParams->size() : 0);

  if (Tok.is(tok::semi)) {
    ConsumeToken();
    Actions.ParsedFreeStandingDeclSpec(CurScope, DS);
    return;
  }

  ParsingDeclarator DeclaratorInfo(*this, DS, Declarator::MemberContext);

  if (Tok.isNot(tok::colon)) {
    // Don't parse FOO:BAR as if it were a typo for FOO::BAR.
    ColonProtectionRAIIObject X(*this);

    // Parse the first declarator.
    ParseDeclarator(DeclaratorInfo);
    // Error parsing the declarator?
    if (!DeclaratorInfo.hasName()) {
      // If so, skip until the semi-colon or a }.
      SkipUntil(tok::r_brace, true);
      if (Tok.is(tok::semi))
        ConsumeToken();
      return;
    }

    // If attributes exist after the declarator, but before an '{', parse them.
    if (Tok.is(tok::kw___attribute)) {
      SourceLocation Loc;
      AttributeList *AttrList = ParseGNUAttributes(&Loc);
      DeclaratorInfo.AddAttributes(AttrList, Loc);
    }

    // function-definition:
    if (Tok.is(tok::l_brace)
        || (DeclaratorInfo.isFunctionDeclarator() &&
            (Tok.is(tok::colon) || Tok.is(tok::kw_try)))) {
      if (!DeclaratorInfo.isFunctionDeclarator()) {
        Diag(Tok, diag::err_func_def_no_params);
        ConsumeBrace();
        SkipUntil(tok::r_brace, true);
        return;
      }

      if (DS.getStorageClassSpec() == DeclSpec::SCS_typedef) {
        Diag(Tok, diag::err_function_declared_typedef);
        // This recovery skips the entire function body. It would be nice
        // to simply call ParseCXXInlineMethodDef() below, however Sema
        // assumes the declarator represents a function, not a typedef.
        ConsumeBrace();
        SkipUntil(tok::r_brace, true);
        return;
      }

      ParseCXXInlineMethodDef(AS, DeclaratorInfo, TemplateInfo);
      return;
    }
  }

  // member-declarator-list:
  //   member-declarator
  //   member-declarator-list ',' member-declarator

  llvm::SmallVector<DeclPtrTy, 8> DeclsInGroup;
  OwningExprResult BitfieldSize(Actions);
  OwningExprResult Init(Actions);
  bool Deleted = false;

  while (1) {
    // member-declarator:
    //   declarator pure-specifier[opt]
    //   declarator constant-initializer[opt]
    //   identifier[opt] ':' constant-expression

    if (Tok.is(tok::colon)) {
      ConsumeToken();
      BitfieldSize = ParseConstantExpression();
      if (BitfieldSize.isInvalid())
        SkipUntil(tok::comma, true, true);
    }

    // pure-specifier:
    //   '= 0'
    //
    // constant-initializer:
    //   '=' constant-expression
    //
    // defaulted/deleted function-definition:
    //   '=' 'default'                          [TODO]
    //   '=' 'delete'

    if (Tok.is(tok::equal)) {
      ConsumeToken();
      if (getLang().CPlusPlus0x && Tok.is(tok::kw_delete)) {
        ConsumeToken();
        Deleted = true;
      } else {
        Init = ParseInitializer();
        if (Init.isInvalid())
          SkipUntil(tok::comma, true, true);
      }
    }

    // If attributes exist after the declarator, parse them.
    if (Tok.is(tok::kw___attribute)) {
      SourceLocation Loc;
      AttributeList *AttrList = ParseGNUAttributes(&Loc);
      DeclaratorInfo.AddAttributes(AttrList, Loc);
    }

    // NOTE: If Sema is the Action module and declarator is an instance field,
    // this call will *not* return the created decl; It will return null.
    // See Sema::ActOnCXXMemberDeclarator for details.

    DeclPtrTy ThisDecl;
    if (DS.isFriendSpecified()) {
      // TODO: handle initializers, bitfields, 'delete'
      ThisDecl = Actions.ActOnFriendFunctionDecl(CurScope, DeclaratorInfo,
                                                 /*IsDefinition*/ false,
                                                 move(TemplateParams));
    } else {
      ThisDecl = Actions.ActOnCXXMemberDeclarator(CurScope, AS,
                                                  DeclaratorInfo,
                                                  move(TemplateParams),
                                                  BitfieldSize.release(),
                                                  Init.release(),
                                                  /*IsDefinition*/Deleted,
                                                  Deleted);
    }
    if (ThisDecl)
      DeclsInGroup.push_back(ThisDecl);

    if (DeclaratorInfo.isFunctionDeclarator() &&
        DeclaratorInfo.getDeclSpec().getStorageClassSpec()
          != DeclSpec::SCS_typedef) {
      HandleMemberFunctionDefaultArgs(DeclaratorInfo, ThisDecl);
    }

    DeclaratorInfo.complete(ThisDecl);

    // If we don't have a comma, it is either the end of the list (a ';')
    // or an error, bail out.
    if (Tok.isNot(tok::comma))
      break;

    // Consume the comma.
    ConsumeToken();

    // Parse the next declarator.
    DeclaratorInfo.clear();
    BitfieldSize = 0;
    Init = 0;
    Deleted = false;

    // Attributes are only allowed on the second declarator.
    if (Tok.is(tok::kw___attribute)) {
      SourceLocation Loc;
      AttributeList *AttrList = ParseGNUAttributes(&Loc);
      DeclaratorInfo.AddAttributes(AttrList, Loc);
    }

    if (Tok.isNot(tok::colon))
      ParseDeclarator(DeclaratorInfo);
  }

  if (ExpectAndConsume(tok::semi, diag::err_expected_semi_decl_list)) {
    // Skip to end of block or statement.
    SkipUntil(tok::r_brace, true, true);
    // If we stopped at a ';', eat it.
    if (Tok.is(tok::semi)) ConsumeToken();
    return;
  }

  Actions.FinalizeDeclaratorGroup(CurScope, DS, DeclsInGroup.data(),
                                  DeclsInGroup.size());
}

/// ParseCXXMemberSpecification - Parse the class definition.
///
///       member-specification:
///         member-declaration member-specification[opt]
///         access-specifier ':' member-specification[opt]
///
void Parser::ParseCXXMemberSpecification(SourceLocation RecordLoc,
                                         unsigned TagType, DeclPtrTy TagDecl) {
  assert((TagType == DeclSpec::TST_struct ||
         TagType == DeclSpec::TST_union  ||
         TagType == DeclSpec::TST_class) && "Invalid TagType!");

  PrettyStackTraceActionsDecl CrashInfo(TagDecl, RecordLoc, Actions,
                                        PP.getSourceManager(),
                                        "parsing struct/union/class body");

  // Determine whether this is a non-nested class. Note that local
  // classes are *not* considered to be nested classes.
  bool NonNestedClass = true;
  if (!ClassStack.empty()) {
    for (const Scope *S = CurScope; S; S = S->getParent()) {
      if (S->isClassScope()) {
        // We're inside a class scope, so this is a nested class.
        NonNestedClass = false;
        break;
      }

      if ((S->getFlags() & Scope::FnScope)) {
        // If we're in a function or function template declared in the
        // body of a class, then this is a local class rather than a
        // nested class.
        const Scope *Parent = S->getParent();
        if (Parent->isTemplateParamScope())
          Parent = Parent->getParent();
        if (Parent->isClassScope())
          break;
      }
    }
  }

  // Enter a scope for the class.
  ParseScope ClassScope(this, Scope::ClassScope|Scope::DeclScope);

  // Note that we are parsing a new (potentially-nested) class definition.
  ParsingClassDefinition ParsingDef(*this, TagDecl, NonNestedClass);

  if (TagDecl)
    Actions.ActOnTagStartDefinition(CurScope, TagDecl);

  if (Tok.is(tok::colon)) {
    ParseBaseClause(TagDecl);

    if (!Tok.is(tok::l_brace)) {
      Diag(Tok, diag::err_expected_lbrace_after_base_specifiers);
      return;
    }
  }

  assert(Tok.is(tok::l_brace));

  SourceLocation LBraceLoc = ConsumeBrace();

  if (!TagDecl) {
    SkipUntil(tok::r_brace, false, false);
    return;
  }

  Actions.ActOnStartCXXMemberDeclarations(CurScope, TagDecl, LBraceLoc);

  // C++ 11p3: Members of a class defined with the keyword class are private
  // by default. Members of a class defined with the keywords struct or union
  // are public by default.
  AccessSpecifier CurAS;
  if (TagType == DeclSpec::TST_class)
    CurAS = AS_private;
  else
    CurAS = AS_public;

  // While we still have something to read, read the member-declarations.
  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    // Each iteration of this loop reads one member-declaration.

    // Check for extraneous top-level semicolon.
    if (Tok.is(tok::semi)) {
      Diag(Tok, diag::ext_extra_struct_semi)
        << CodeModificationHint::CreateRemoval(Tok.getLocation());
      ConsumeToken();
      continue;
    }

    AccessSpecifier AS = getAccessSpecifierIfPresent();
    if (AS != AS_none) {
      // Current token is a C++ access specifier.
      CurAS = AS;
      ConsumeToken();
      ExpectAndConsume(tok::colon, diag::err_expected_colon);
      continue;
    }

    // FIXME: Make sure we don't have a template here.

    // Parse all the comma separated declarators.
    ParseCXXClassMemberDeclaration(CurAS);
  }

  SourceLocation RBraceLoc = MatchRHSPunctuation(tok::r_brace, LBraceLoc);

  // If attributes exist after class contents, parse them.
  llvm::OwningPtr<AttributeList> AttrList;
  if (Tok.is(tok::kw___attribute))
    AttrList.reset(ParseGNUAttributes()); // FIXME: where should I put them?

  Actions.ActOnFinishCXXMemberSpecification(CurScope, RecordLoc, TagDecl,
                                            LBraceLoc, RBraceLoc);

  // C++ 9.2p2: Within the class member-specification, the class is regarded as
  // complete within function bodies, default arguments,
  // exception-specifications, and constructor ctor-initializers (including
  // such things in nested classes).
  //
  // FIXME: Only function bodies and constructor ctor-initializers are
  // parsed correctly, fix the rest.
  if (NonNestedClass) {
    // We are not inside a nested class. This class and its nested classes
    // are complete and we can parse the delayed portions of method
    // declarations and the lexed inline method definitions.
    ParseLexedMethodDeclarations(getCurrentClass());
    ParseLexedMethodDefs(getCurrentClass());
  }

  // Leave the class scope.
  ParsingDef.Pop();
  ClassScope.Exit();

  Actions.ActOnTagFinishDefinition(CurScope, TagDecl, RBraceLoc);
}

/// ParseConstructorInitializer - Parse a C++ constructor initializer,
/// which explicitly initializes the members or base classes of a
/// class (C++ [class.base.init]). For example, the three initializers
/// after the ':' in the Derived constructor below:
///
/// @code
/// class Base { };
/// class Derived : Base {
///   int x;
///   float f;
/// public:
///   Derived(float f) : Base(), x(17), f(f) { }
/// };
/// @endcode
///
/// [C++]  ctor-initializer:
///          ':' mem-initializer-list
///
/// [C++]  mem-initializer-list:
///          mem-initializer
///          mem-initializer , mem-initializer-list
void Parser::ParseConstructorInitializer(DeclPtrTy ConstructorDecl) {
  assert(Tok.is(tok::colon) && "Constructor initializer always starts with ':'");

  SourceLocation ColonLoc = ConsumeToken();

  llvm::SmallVector<MemInitTy*, 4> MemInitializers;
  bool AnyErrors = false;
  
  do {
    MemInitResult MemInit = ParseMemInitializer(ConstructorDecl);
    if (!MemInit.isInvalid())
      MemInitializers.push_back(MemInit.get());
    else
      AnyErrors = true;
    
    if (Tok.is(tok::comma))
      ConsumeToken();
    else if (Tok.is(tok::l_brace))
      break;
    else {
      // Skip over garbage, until we get to '{'.  Don't eat the '{'.
      Diag(Tok.getLocation(), diag::err_expected_lbrace_or_comma);
      SkipUntil(tok::l_brace, true, true);
      break;
    }
  } while (true);

  Actions.ActOnMemInitializers(ConstructorDecl, ColonLoc,
                               MemInitializers.data(), MemInitializers.size(),
                               AnyErrors);
}

/// ParseMemInitializer - Parse a C++ member initializer, which is
/// part of a constructor initializer that explicitly initializes one
/// member or base class (C++ [class.base.init]). See
/// ParseConstructorInitializer for an example.
///
/// [C++] mem-initializer:
///         mem-initializer-id '(' expression-list[opt] ')'
///
/// [C++] mem-initializer-id:
///         '::'[opt] nested-name-specifier[opt] class-name
///         identifier
Parser::MemInitResult Parser::ParseMemInitializer(DeclPtrTy ConstructorDecl) {
  // parse '::'[opt] nested-name-specifier[opt]
  CXXScopeSpec SS;
  ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/0, false);
  TypeTy *TemplateTypeTy = 0;
  if (Tok.is(tok::annot_template_id)) {
    TemplateIdAnnotation *TemplateId
      = static_cast<TemplateIdAnnotation *>(Tok.getAnnotationValue());
    if (TemplateId->Kind == TNK_Type_template ||
        TemplateId->Kind == TNK_Dependent_template_name) {
      AnnotateTemplateIdTokenAsType(&SS);
      assert(Tok.is(tok::annot_typename) && "template-id -> type failed");
      TemplateTypeTy = Tok.getAnnotationValue();
    }
  }
  if (!TemplateTypeTy && Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_member_or_base_name);
    return true;
  }

  // Get the identifier. This may be a member name or a class name,
  // but we'll let the semantic analysis determine which it is.
  IdentifierInfo *II = Tok.is(tok::identifier) ? Tok.getIdentifierInfo() : 0;
  SourceLocation IdLoc = ConsumeToken();

  // Parse the '('.
  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen);
    return true;
  }
  SourceLocation LParenLoc = ConsumeParen();

  // Parse the optional expression-list.
  ExprVector ArgExprs(Actions);
  CommaLocsTy CommaLocs;
  if (Tok.isNot(tok::r_paren) && ParseExpressionList(ArgExprs, CommaLocs)) {
    SkipUntil(tok::r_paren);
    return true;
  }

  SourceLocation RParenLoc = MatchRHSPunctuation(tok::r_paren, LParenLoc);

  return Actions.ActOnMemInitializer(ConstructorDecl, CurScope, SS, II,
                                     TemplateTypeTy, IdLoc,
                                     LParenLoc, ArgExprs.take(),
                                     ArgExprs.size(), CommaLocs.data(),
                                     RParenLoc);
}

/// ParseExceptionSpecification - Parse a C++ exception-specification
/// (C++ [except.spec]).
///
///       exception-specification:
///         'throw' '(' type-id-list [opt] ')'
/// [MS]    'throw' '(' '...' ')'
///
///       type-id-list:
///         type-id
///         type-id-list ',' type-id
///
bool Parser::ParseExceptionSpecification(SourceLocation &EndLoc,
                                         llvm::SmallVector<TypeTy*, 2>
                                             &Exceptions,
                                         llvm::SmallVector<SourceRange, 2>
                                             &Ranges,
                                         bool &hasAnyExceptionSpec) {
  assert(Tok.is(tok::kw_throw) && "expected throw");

  SourceLocation ThrowLoc = ConsumeToken();

  if (!Tok.is(tok::l_paren)) {
    return Diag(Tok, diag::err_expected_lparen_after) << "throw";
  }
  SourceLocation LParenLoc = ConsumeParen();

  // Parse throw(...), a Microsoft extension that means "this function
  // can throw anything".
  if (Tok.is(tok::ellipsis)) {
    hasAnyExceptionSpec = true;
    SourceLocation EllipsisLoc = ConsumeToken();
    if (!getLang().Microsoft)
      Diag(EllipsisLoc, diag::ext_ellipsis_exception_spec);
    EndLoc = MatchRHSPunctuation(tok::r_paren, LParenLoc);
    return false;
  }

  // Parse the sequence of type-ids.
  SourceRange Range;
  while (Tok.isNot(tok::r_paren)) {
    TypeResult Res(ParseTypeName(&Range));
    if (!Res.isInvalid()) {
      Exceptions.push_back(Res.get());
      Ranges.push_back(Range);
    }
    if (Tok.is(tok::comma))
      ConsumeToken();
    else
      break;
  }

  EndLoc = MatchRHSPunctuation(tok::r_paren, LParenLoc);
  return false;
}

/// \brief We have just started parsing the definition of a new class,
/// so push that class onto our stack of classes that is currently
/// being parsed.
void Parser::PushParsingClass(DeclPtrTy ClassDecl, bool NonNestedClass) {
  assert((NonNestedClass || !ClassStack.empty()) &&
         "Nested class without outer class");
  ClassStack.push(new ParsingClass(ClassDecl, NonNestedClass));
}

/// \brief Deallocate the given parsed class and all of its nested
/// classes.
void Parser::DeallocateParsedClasses(Parser::ParsingClass *Class) {
  for (unsigned I = 0, N = Class->NestedClasses.size(); I != N; ++I)
    DeallocateParsedClasses(Class->NestedClasses[I]);
  delete Class;
}

/// \brief Pop the top class of the stack of classes that are
/// currently being parsed.
///
/// This routine should be called when we have finished parsing the
/// definition of a class, but have not yet popped the Scope
/// associated with the class's definition.
///
/// \returns true if the class we've popped is a top-level class,
/// false otherwise.
void Parser::PopParsingClass() {
  assert(!ClassStack.empty() && "Mismatched push/pop for class parsing");

  ParsingClass *Victim = ClassStack.top();
  ClassStack.pop();
  if (Victim->TopLevelClass) {
    // Deallocate all of the nested classes of this class,
    // recursively: we don't need to keep any of this information.
    DeallocateParsedClasses(Victim);
    return;
  }
  assert(!ClassStack.empty() && "Missing top-level class?");

  if (Victim->MethodDecls.empty() && Victim->MethodDefs.empty() &&
      Victim->NestedClasses.empty()) {
    // The victim is a nested class, but we will not need to perform
    // any processing after the definition of this class since it has
    // no members whose handling was delayed. Therefore, we can just
    // remove this nested class.
    delete Victim;
    return;
  }

  // This nested class has some members that will need to be processed
  // after the top-level class is completely defined. Therefore, add
  // it to the list of nested classes within its parent.
  assert(CurScope->isClassScope() && "Nested class outside of class scope?");
  ClassStack.top()->NestedClasses.push_back(Victim);
  Victim->TemplateScope = CurScope->getParent()->isTemplateParamScope();
}

/// ParseCXX0XAttributes - Parse a C++0x attribute-specifier. Currently only
/// parses standard attributes.
///
/// [C++0x] attribute-specifier:
///         '[' '[' attribute-list ']' ']'
///
/// [C++0x] attribute-list:
///         attribute[opt]
///         attribute-list ',' attribute[opt]
///
/// [C++0x] attribute:
///         attribute-token attribute-argument-clause[opt]
///
/// [C++0x] attribute-token:
///         identifier
///         attribute-scoped-token
///
/// [C++0x] attribute-scoped-token:
///         attribute-namespace '::' identifier
///
/// [C++0x] attribute-namespace:
///         identifier
///
/// [C++0x] attribute-argument-clause:
///         '(' balanced-token-seq ')'
///
/// [C++0x] balanced-token-seq:
///         balanced-token
///         balanced-token-seq balanced-token
///
/// [C++0x] balanced-token:
///         '(' balanced-token-seq ')'
///         '[' balanced-token-seq ']'
///         '{' balanced-token-seq '}'
///         any token but '(', ')', '[', ']', '{', or '}'
CXX0XAttributeList Parser::ParseCXX0XAttributes(SourceLocation *EndLoc) {
  assert(Tok.is(tok::l_square) && NextToken().is(tok::l_square)
      && "Not a C++0x attribute list");

  SourceLocation StartLoc = Tok.getLocation(), Loc;
  AttributeList *CurrAttr = 0;

  ConsumeBracket();
  ConsumeBracket();
  
  if (Tok.is(tok::comma)) {
    Diag(Tok.getLocation(), diag::err_expected_ident);
    ConsumeToken();
  }

  while (Tok.is(tok::identifier) || Tok.is(tok::comma)) {
    // attribute not present
    if (Tok.is(tok::comma)) {
      ConsumeToken();
      continue;
    }

    IdentifierInfo *ScopeName = 0, *AttrName = Tok.getIdentifierInfo();
    SourceLocation ScopeLoc, AttrLoc = ConsumeToken();
    
    // scoped attribute
    if (Tok.is(tok::coloncolon)) {
      ConsumeToken();

      if (!Tok.is(tok::identifier)) {
        Diag(Tok.getLocation(), diag::err_expected_ident);
        SkipUntil(tok::r_square, tok::comma, true, true);
        continue;
      }
      
      ScopeName = AttrName;
      ScopeLoc = AttrLoc;

      AttrName = Tok.getIdentifierInfo();
      AttrLoc = ConsumeToken();
    }

    bool AttrParsed = false;
    // No scoped names are supported; ideally we could put all non-standard
    // attributes into namespaces.
    if (!ScopeName) {
      switch(AttributeList::getKind(AttrName))
      {
      // No arguments
      case AttributeList::AT_base_check:
      case AttributeList::AT_carries_dependency:
      case AttributeList::AT_final:
      case AttributeList::AT_hiding:
      case AttributeList::AT_noreturn:
      case AttributeList::AT_override: {
        if (Tok.is(tok::l_paren)) {
          Diag(Tok.getLocation(), diag::err_cxx0x_attribute_forbids_arguments)
            << AttrName->getName();
          break;
        }

        CurrAttr = new AttributeList(AttrName, AttrLoc, 0, AttrLoc, 0,
                                     SourceLocation(), 0, 0, CurrAttr, false,
                                     true);
        AttrParsed = true;
        break;
      }

      // One argument; must be a type-id or assignment-expression
      case AttributeList::AT_aligned: {
        if (Tok.isNot(tok::l_paren)) {
          Diag(Tok.getLocation(), diag::err_cxx0x_attribute_requires_arguments)
            << AttrName->getName();
          break;
        }
        SourceLocation ParamLoc = ConsumeParen();

        OwningExprResult ArgExpr = ParseCXX0XAlignArgument(ParamLoc);

        MatchRHSPunctuation(tok::r_paren, ParamLoc);

        ExprVector ArgExprs(Actions);
        ArgExprs.push_back(ArgExpr.release());
        CurrAttr = new AttributeList(AttrName, AttrLoc, 0, AttrLoc,
                                     0, ParamLoc, ArgExprs.take(), 1, CurrAttr,
                                     false, true);

        AttrParsed = true;
        break;
      }

      // Silence warnings
      default: break;
      }
    }

    // Skip the entire parameter clause, if any
    if (!AttrParsed && Tok.is(tok::l_paren)) {
      ConsumeParen();
      // SkipUntil maintains the balancedness of tokens.
      SkipUntil(tok::r_paren, false);
    }
  }

  if (ExpectAndConsume(tok::r_square, diag::err_expected_rsquare))
    SkipUntil(tok::r_square, false);
  Loc = Tok.getLocation();
  if (ExpectAndConsume(tok::r_square, diag::err_expected_rsquare))
    SkipUntil(tok::r_square, false);

  CXX0XAttributeList Attr (CurrAttr, SourceRange(StartLoc, Loc), true);
  return Attr;
}

/// ParseCXX0XAlignArgument - Parse the argument to C++0x's [[align]]
/// attribute.
///
/// FIXME: Simply returns an alignof() expression if the argument is a
/// type. Ideally, the type should be propagated directly into Sema.
///
/// [C++0x] 'align' '(' type-id ')'
/// [C++0x] 'align' '(' assignment-expression ')'
Parser::OwningExprResult Parser::ParseCXX0XAlignArgument(SourceLocation Start) {
  if (isTypeIdInParens()) {
    EnterExpressionEvaluationContext Unevaluated(Actions,
                                                  Action::Unevaluated);
    SourceLocation TypeLoc = Tok.getLocation();
    TypeTy *Ty = ParseTypeName().get();
    SourceRange TypeRange(Start, Tok.getLocation());
    return Actions.ActOnSizeOfAlignOfExpr(TypeLoc, false, true, Ty,
                                              TypeRange);
  } else
    return ParseConstantExpression();
}
