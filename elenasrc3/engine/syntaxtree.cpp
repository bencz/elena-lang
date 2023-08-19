//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Compiler
//
//		This file contains Syntax Tree class implementation
//
//                                             (C)2021-2023, by Aleksey Rakov
//---------------------------------------------------------------------------

#include "elena.h"
// --------------------------------------------------------------------------
#include "syntaxtree.h"

using namespace elena_lang;

// --- SyntaxTree ---

void SyntaxTree :: loadTokens(TokenMap& map)
{
   map.add("root", SyntaxKey::Root);
   map.add("namespace", SyntaxKey::Namespace);
   map.add("public_namespace", SyntaxKey::Namespace);
   //   tokens.add("class", lxClass);
   //   tokens.add("singleton", lxClass);
   map.add("public_symbol", SyntaxKey::Symbol);
   map.add("nested", SyntaxKey::NestedBlock);
   map.add("script_method", SyntaxKey::Method);
   map.add("script_function", SyntaxKey::Method);
   //   tokens.add("method", lxClassMethod);
   //   tokens.add("function", lxClassMethod);
   map.add("get_method", SyntaxKey::Method);
   map.add("message", SyntaxKey::Message);
   map.add("code", SyntaxKey::CodeBlock);
   map.add("object", SyntaxKey::Object);
   map.add("expression", SyntaxKey::Expression);
   map.add("get_expression", SyntaxKey::GetExpression);
   map.add("returning", SyntaxKey::ReturnExpression);
   map.add( "message_operation", SyntaxKey::MessageOperation);
   map.add("property_operation", SyntaxKey::PropertyOperation);
   map.add("symbol", SyntaxKey::Symbol);
   //   tokens.add("preloaded_symbol", lxSymbol);
   //   tokens.add("literal", lxLiteral);
   map.add("identifier", SyntaxKey::identifier);
   //   tokens.add("character", lxCharacter);
   //   tokens.add("variable_identifier", lxIdentifier);
   //   tokens.add("new_identifier", lxIdentifier);
   //   tokens.add("prev_identifier", lxIdentifier);
   map.add("integer", SyntaxKey::integer);
   map.add("parameter", SyntaxKey::Parameter);
   ////   tokens.add("include", lxInclude);
   //   //tokens.add("forward", lxForward);
   //   tokens.add("reference", lxReference);
   //   tokens.add("new_reference", lxReference);
   //   tokens.add("variable", lxVariable);
   //   //tokens.add("assign", lxAssign);
   //   //tokens.add("operator", lxOperator);
   map.add("nameattr", SyntaxKey::Name);
   map.add("property_parameter", SyntaxKey::PropertyOperation);
   //   //tokens.add("import", lxImport);
   //   tokens.add("loop_expression", lxExpression);
}

bool SyntaxTree :: save(MemoryBase* section)
{
   MemoryWriter writer(section);

   writer.writePos(_body.length());
   writer.write(_body.get(0), _body.length());

   writer.writePos(_strings.length());
   writer.write(_strings.get(0), _strings.length());

   return _body.length() > 0;
}

void SyntaxTree :: load(MemoryBase* section)
{
   _body.clear();
   _strings.clear();

   MemoryReader reader(section);
   pos_t bodyLength = reader.getPos();
   _body.load(reader, bodyLength);

   pos_t stringLength = reader.getPos();
   _strings.load(reader, stringLength);
}

void SyntaxTree :: copyNode(SyntaxTreeWriter& writer, SyntaxNode node, bool includingNode)
{
   if (includingNode) {
      if (node.arg.strArgPosition != INVALID_POS) {
         writer.newNode(node.key, node.identifier());
      }
      else writer.newNode(node.key, node.arg.reference);
   }

   SyntaxNode current = node.firstChild();
   while (current != SyntaxKey::None) {
      copyNode(writer, current, true);

      current = current.nextNode();
   }

   if (includingNode)
      writer.closeNode();
}

void SyntaxTree :: copyNodeSafe(SyntaxTreeWriter& writer, SyntaxNode node, bool includingNode)
{
   if (includingNode) {
      if (node.arg.strArgPosition != INVALID_POS) {
         IdentifierString tmp(node.identifier());
         writer.newNode(node.key, *tmp);
      }
      else writer.newNode(node.key, node.arg.reference);
   }

   SyntaxNode current = node.firstChild();
   while (current != SyntaxKey::None) {
      copyNodeSafe(writer, current, true);

      current = current.nextNode();
   }

   if (includingNode)
      writer.closeNode();
}

void SyntaxTree :: saveNode(SyntaxNode node, MemoryBase* section, bool includingNode)
{
   SyntaxTree tree;
   SyntaxTreeWriter writer(tree);

   writer.newNode(SyntaxKey::Root);

   copyNode(writer, node, includingNode);

   writer.closeNode();

   tree.save(section);
}