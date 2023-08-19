//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Compiler
//               
//		This file contains Syntax Tree Builder class declaration
//
//                                             (C)2021-2022, by Aleksey Rakov
//---------------------------------------------------------------------------

#ifndef DERIVATION_H
#define DERIVATION_H

#include "elena.h"
#include "syntaxtree.h"
#include "clicommon.h"
#include "compilerlogic.h"

namespace elena_lang
{
   // --- SyntaxTreeWriter ---
   class SyntaxTreeBuilder : public SyntaxWriterBase
   {
      enum class ScopeType
      {
         Unknown = 0,
         InlineTemplate,
         ClassTemplate,
         PropertyTemplate,
         ExtensionTemplate
      };

      struct Scope
      {
         ScopeType    type;
         bool         ignoreTerminalInfo;
         ReferenceMap arguments;
         ReferenceMap parameters;
         int          nestedLevel;

         bool withTypeParameters() const
         {
            return type == ScopeType::ClassTemplate || type == ScopeType::PropertyTemplate || type == ScopeType::ExtensionTemplate;
         }
         bool withNameParameters() const
         {
            return type == ScopeType::PropertyTemplate;
         }

         bool isParameter(SyntaxNode node, SyntaxKey& parameterKey, ref_t& parameterIndex, bool allowType)
         {
            switch (type) {
               case ScopeType::InlineTemplate:
               {
                  ref_t index = arguments.get(node.identifier());
                  if (index) {
                     parameterKey = SyntaxKey::TemplateArgParameter;
                     parameterIndex = index;
                     return true;
                  }
                  index = parameters.get(node.identifier());
                  if (index) {
                     parameterKey = SyntaxKey::TemplateParameter;
                     parameterIndex = index;
                     return true;
                  }
                  return false;
               }
               case ScopeType::PropertyTemplate:
               {
                  ref_t index = arguments.get(node.identifier());
                  if (index == 1) {
                     parameterKey = SyntaxKey::NameParameter;
                     parameterIndex = index;
                     return true;
                  }
                  else if (index > 1) {
                     parameterKey = SyntaxKey::TemplateArgParameter;
                     parameterIndex = index;
                     return true;
                  }
                  return false;
               }
               case ScopeType::ClassTemplate:
               case ScopeType::ExtensionTemplate:
                  if (allowType) {
                     ref_t index = arguments.get(node.identifier());
                     if (index > 0) {
                        parameterKey = SyntaxKey::TemplateArgParameter;
                        parameterIndex = index + nestedLevel;

                        return true;
                     }
                  }
                  return false;
               default:
                  return false;
            }
         }

         Scope()
            : arguments(0), parameters(0)
         {
            type = ScopeType::Unknown;
            ignoreTerminalInfo = false;
            nestedLevel = 0;
         }
      };

      ErrorProcessor*         _errorProcessor;
      SyntaxTree              _cache;
      int                     _level;

      SyntaxTreeWriter        _writer;
      SyntaxTreeWriter        _cacheWriter;

      ModuleScopeBase*        _moduleScope;
      TemplateProssesorBase*  _templateProcessor;

      ScopeType defineTemplateType(SyntaxNode node);

      ref_t mapAttribute(SyntaxNode node, bool allowType, ref_t& previusCategory);

      void parseStatement(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode current, 
         List<SyntaxNode>& arguments, List<SyntaxNode>& parameters, IdentifierString& postfix);
      void generateTemplateStatement(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void generateTemplateOperation(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);

      void flushNode(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushCollection(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);

      void flushL6AsTemplateArg(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushIdentifier(SyntaxTreeWriter& writer, SyntaxNode identNode, bool ignoreTerminalInfo);
      void flushTemplateCode(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushTemplateArgDescr(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushParameterArgDescr(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushTemplateArg(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, bool allowType);
      void flushTemplageExpression(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, SyntaxKey type, bool allowType);
      void flushTemplateType(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, bool exprMode = true);
      void flushArrayType(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, int nestLevel = 1);
      void flushMessage(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushResend(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushObject(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushNested(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushClosure(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushExpression(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushExpressionCollection(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushExpressionAsDescriptor(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushExpressionMember(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushStatement(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushMethodCode(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);

      void copyHeader(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, bool includeType);
      void copyType(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);

      void flushSubScopeMember(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, SyntaxNode headerNode);
      void flushSubScope(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, SyntaxNode headerNode);

      void flushClassMember(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, bool functionMode = false);
      void flushMethod(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushMethodMember(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushParameterBlock(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushTemplate(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      bool flushAttribute(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, ref_t& previusCategory, 
         bool allowType, int arrayNestLevel = 0);
      void flushTypeAttribute(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, ref_t& previusCategory, 
         bool allowType, bool onlyChildren = false);
      void flushInlineTemplatePostfixes(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushClassMemberPostfixes(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node/*, bool ignorePostfix*/);
      void flushClassPostfixes(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushSymbolPostfixes(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushParent(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushParentTemplate(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);

      void flushDescriptor(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, bool withNameNode = true, 
         bool typeDescriptor = false);
      void flushClass(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node, bool functionMode);
      void flushInlineTemplate(SyntaxTreeWriter& writer, Scope& scope, SyntaxNode node);
      void flushDeclaration(SyntaxTreeWriter& writer, SyntaxNode node);
      void flushDictionary(SyntaxTreeWriter& writer, SyntaxNode node);
      void flushNamespace(SyntaxTreeWriter& writer, SyntaxNode node);
      void flush(SyntaxTreeWriter& writer, SyntaxNode node);

   public:
      void newNode(parse_key_t key) override;
      void newNode(parse_key_t key, ustr_t arg) override;
      void appendTerminal(parse_key_t key, ustr_t value, LineInfo lineInfo) override;
      void injectNode(parse_key_t key) override;

      void closeNode() override;

      void saveTree(SyntaxTree& tree) override;

      void clear()
      {
         _level = 0;

         _cacheWriter.clear();

         _cacheWriter.newNode(SyntaxKey::Root);
      }

      SyntaxTreeBuilder(SyntaxTree* target, ErrorProcessor* errorProcessor, 
         ModuleScopeBase* moduleScope, TemplateProssesorBase* templateProcessor)
         : _writer(*target), _cacheWriter(_cache)
      {
         _errorProcessor = errorProcessor;
         _moduleScope = moduleScope;
         _templateProcessor = templateProcessor;

         _writer.clear();
         _writer.newNode(SyntaxKey::Root);

         clear();
      }
   };

   // ---- TemplateProssesor ---
   class TemplateProssesor
   {
      enum class Type
      {
         None = 0,
         Inline,
         CodeTemplate,
         Class,
         InlineProperty
      };

      typedef Map<ref_t, SyntaxNode> NodeMap;

      struct TemplateScope
      {
         Type             type;
         NodeMap          argValues;
         NodeMap          parameterValues;
         ModuleScopeBase* moduleScope;
         ref_t            targetRef;

         TemplateScope() :
            type(Type::None),
            argValues({}),
            parameterValues({}),
            moduleScope(nullptr), targetRef(0)
         {
         }
         TemplateScope(Type type, ModuleScopeBase* scope, ref_t targetRef) :
            type(type),
            argValues({}),
            parameterValues({}),
            moduleScope(scope),
            targetRef(targetRef)
         {
         }
      };

      void loadArguments(TemplateScope& scope, List<SyntaxNode>* parameters);
      void loadParameters(TemplateScope& scope, List<SyntaxNode>* parameters);

      void copyNode(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);
      void copyChildren(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);
      void copyField(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);
      void copyMethod(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);
      void copyParent(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);
      void copyClassMembers(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);
      void copyTemplatePostfix(SyntaxTreeWriter& writer, TemplateScope& scope, SyntaxNode node);

      void copyModuleInfo(SyntaxTreeWriter& writer, SyntaxNode rootNode, TemplateScope& scope);

      void generate(SyntaxTreeWriter& writer, TemplateScope& scope, MemoryBase* templateSection);

      void generateTemplate(SyntaxTreeWriter& writer, TemplateScope& scope, 
         MemoryBase* templateBody, bool importModuleInfo);

      void importTemplate(Type type, MemoryBase* templateSection, SyntaxNode target,
         List<SyntaxNode>* arguments, List<SyntaxNode>* parameters);

   public:
      void importTemplate(MemoryBase* section, SyntaxNode target, List<SyntaxNode>& parameters);
      void importInlineTemplate(MemoryBase* section, SyntaxNode target, List<SyntaxNode>& parameters);
      void importInlinePropertyTemplate(MemoryBase* section, SyntaxNode target, List<SyntaxNode>& parameters);
      void importCodeTemplate(MemoryBase* templateSection,
         SyntaxNode target, List<SyntaxNode>& arguments, List<SyntaxNode>& parameters);

      void generateClassTemplate(ModuleScopeBase* moduleScope, ref_t classRef, SyntaxTreeWriter& writer,
         MemoryBase* sectionBody, List<SyntaxNode>& args);

      TemplateProssesor() = default;
   };
}

#endif