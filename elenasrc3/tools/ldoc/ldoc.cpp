//---------------------------------------------------------------------------
//		E L E N A   P r o j e c t:  ELENA Tools
//
//		This is a main file containing doc generator code
//
//                                             (C)2021-2024, by Aleksey Rakov
//---------------------------------------------------------------------------

#include "ldoc.h"

#include "bytecode.h"
#include "langcommon.h"
#include "ldocconst.h"
#include "module.h"

constexpr auto ARRAY_TEMPLATE_PREFIX = "system'Array&lt;";
constexpr auto REF_TEMPLATE_PREFIX = "system'Reference&lt;";

using namespace elena_lang;

constexpr auto DESCRIPTION_SECTION = "'meta$system@descriptions";

inline bool isTemplateBased(ustr_t reference)
{
   for (size_t i = 0; i < getlength(reference); i++) {
      if (reference[i] == '#' && reference[i + 1] >= '0' && reference[i + 1] <= '9')
         return true;
   }

   return false;
}

void writeNs(TextFileWriter& writer, ustr_t ns)
{
   for (size_t i = 0; i < getlength(ns); i++)
   {
      if (ns[i] == '\'') {
         writer.writeChar('-');
      }
      else writer.writeChar(ns[i]);
   }
}

void writeNs(IdentifierString& name, ApiModuleInfo* info)
{
   for (size_t i = 0; i < info->name.length(); i++)
   {
      if (info->name[i] == '\'') {
         name.append('-');
      }
      else name.append(info->name[i]);
   }
}

void writeHeader(TextFileWriter& writer, const char* package, const char* packageLink)
{
   writer.writeTextLine("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Frameset//EN\"\"http://www.w3.org/TR/REC-html40/frameset.dtd\">");
   writer.writeTextLine("<HTML>");
   writer.writeTextLine("<HEAD>");
   writer.writeTextLine("<TITLE>");
   writer.writeText(TITLE);
   writer.writeTextLine(package);
   writer.writeTextLine("</TITLE>");
   writer.writeTextLine("<meta name=\"collection\" content=\"api\">");
   writer.writeTextLine("<LINK REL =\"stylesheet\" TYPE=\"text/css\" HREF=\"stylesheet.css\" TITLE=\"Style\">");
   writer.writeTextLine("</HEAD>");
   writer.writeTextLine("<BODY BGCOLOR=\"white\">");

   writer.writeTextLine("<DIV CLASS=\"topNav\">");

   writer.writeTextLine("<UL CLASS=\"navList\">");

   writer.writeTextLine("<LI>");
   writer.writeTextLine("<A HREF=\"index.html\">Overview</A>");
   writer.writeTextLine("</LI>");

   writer.writeTextLine("<LI>");
   if (!emptystr(packageLink)) {
      writer.writeText("<A HREF=\"");
      writer.writeText(packageLink);
      writer.writeTextLine("\">Module</A>");
   }
   else writer.writeTextLine("Module");
   writer.writeTextLine("</LI>");

   writer.writeTextLine("</UL>");

   writer.writeTextLine("<DIV CLASS=\"aboutLanguage\">");
   writer.writeTextLine("<STRONG>");
   writer.writeTextLine(TITLE2);
   writer.writeTextLine("</STRONG>");
   writer.writeTextLine("</DIV>");

   writer.writeTextLine("</DIV>");
}

void writeSummaryHeader(TextFileWriter& writer, const char* name, const char* shortDescr)
{
   writer.writeTextLine("<DIV CLASS=\"header\">");
   writer.writeTextLine("<H1>");
   writer.writeText("Module ");
   writer.writeTextLine(name);
   writer.writeTextLine("</H1>");
   writer.writeTextLine("</DIV>");

   writer.writeTextLine("<DIV CLASS=\"docSummary\">");
   writer.writeText("<DIV CLASS=\"block\">");
   writer.writeText(shortDescr);
   writer.writeTextLine("</DIV>");
   writer.writeTextLine("</DIV>");

   writer.writeTextLine("<DIV CLASS=\"contentContainer\">");
   writer.writeTextLine("<UL CLASS=\"blockList\">");
}

void writeClassSummaryHeader(TextFileWriter& writer)
{
   writer.writeTextLine("<LI CLASS=\"blockList\">");
   writer.writeTextLine("<TABLE CLASS=\"typeSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<HEADER>");
   writer.writeTextLine("Class Summary");
   writer.writeTextLine("</HEADER>");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Class name</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Description</TH>");
   writer.writeTextLine("</TR>");
}

void writeSymbolSummaryHeader(TextFileWriter& writer)
{
   writer.writeTextLine("<LI CLASS=\"blockList\">");
   writer.writeTextLine("<TABLE CLASS=\"typeSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<HEADER>");
   writer.writeTextLine("Symbol Summary");
   writer.writeTextLine("</HEADER>");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Symbol name</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Description</TH>");
   writer.writeTextLine("</TR>");
}

void writeExtendedSummaryHeader(TextFileWriter& writer)
{
   writer.writeTextLine("<LI CLASS=\"blockList\">");
   writer.writeTextLine("<TABLE CLASS=\"typeSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<HEADER>");
   writer.writeTextLine("Extended Class Summary");
   writer.writeTextLine("</HEADER>");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Symbol name</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Description</TH>");
   writer.writeTextLine("</TR>");

}

void parseNs(IdentifierString& ns, ustr_t root, ustr_t fullName)
{
   if (isWeakReference(fullName)) {
      ns.copy(root);
   }

   size_t ltIndex = fullName.findStr("&lt;");
   size_t last = 0;
   for (size_t i = 0; i < getlength(fullName); i++) {
      if (fullName[i] == '\'') {
         last = i;
      }
      else if (fullName[i] == '#' || (fullName[i] == '&' && i == ltIndex))
         break;
   }

   ns.append(fullName, last);
}

inline bool isTemplateArg(ustr_t name, size_t index)
{
   size_t endPos = name.findSub(index, '&', name.length());

   IdentifierString subName(name + index, endPos - index);

   if ((*subName).findStr("$private@") != NOTFOUND_POS) {
      while (endPos > index) {
         if (name[endPos] == '@' && name[endPos + 1] == 'T')
            return true;

         endPos--;
      }
   }

   return false;
}

inline void parseClassName(IdentifierString& line, ustr_t name, size_t& index, bool skipNs, bool templateMode)
{
   if (name.compareSub("@$auto@", index, 7))
      index += 7;

   size_t prev = index;
   for (size_t i = index; i < name.length(); i++) {
      if (name[i] == '@') {
         if (!skipNs) {
            line.append(name + prev, i - prev);
            line.append('\'');
         }
         prev = i + 1;
      }
      else if (name[i] == '#') {
         line.append(name + prev, i - prev);
         prev = i + 1;

         String<char, 5> tmp;
         size_t numEnd = name.findSub(i, '&', NOTFOUND_POS);
         tmp.copy(name.str() + i + 1, numEnd - i - 1);
         int count = tmp.toInt();
         i = numEnd + 1;
         line.append("&lt;");
         for (int j = 0; j < count; j++) {
            if (j != 0)
               line.append(",");

            parseClassName(line, name, i, templateMode && isTemplateArg(name, i), templateMode);
            i++;
         }
         line.append("&gt;");
         index = i;

         return;
      }
      else if (name[i] == '&') {
         line.append(name + prev, i - prev);
         index = i;

         return;
      }
   }

   index = name.length() - 1;

   line.append(name + prev, index - prev + 1);
}

void parseTemplateName(IdentifierString& line, bool templateMode)
{
   IdentifierString temp(*line);

   line.clear();

   size_t index = 0;
   parseClassName(line, *temp, index, true, templateMode);
}

void retrieveTemplateType(IdentifierString& line, bool templateMode)
{
   IdentifierString temp;

   size_t index = 0;
   parseClassName(temp, *line, index, false, templateMode);
}

void parseTemplateName(ReferenceProperName& name, bool templateMode)
{
   IdentifierString temp(*name);
   parseTemplateName(temp, templateMode);
   name.copy(*temp);
}

inline void skipNestedArgs(ustr_t s, size_t& i)
{
   int level = 1;

   size_t len = s.length();
   while (i < len) {
      if (s.compareSub("&lt;", i, 4)) {
         level++;
      }
      else if (s.compareSub("&gt;", i, 4)) {
         level--;

         if (!level)
            break;
      }

      i++;
   }
}

void writeRefName(TextFileWriter& writer, ustr_t name, bool allowResolvedTemplates)
{
   int paramIndex = 1;
   bool paramMode = false;
   for (size_t i = 0; i < getlength(name); i++)
   {
      if (!paramMode && name[i] == '\'') {
         writer.writeChar('-');
      }
      else if (name.compareSub("&lt;", i, 4)) {
         i += 3;
         if (paramMode) {
            skipNestedArgs(name, i);
         }
         else {
            paramMode = true;
            writer.writeText("&lt;");
         }
      }
      else if (name.compareSub("&gt;", i, 4)) {
         if (!allowResolvedTemplates) {
            String<char, 5> tmp;
            tmp.copy("T");
            tmp.appendInt(paramIndex);
            writer.writeText(tmp.str());
         }
         writer.writeText("&gt;");
         paramMode = false;

         i += 3;
      }
      else if (name[i] == ',') {
         if (!allowResolvedTemplates) {
            String<char, 5> tmp;
            tmp.copy("T");
            tmp.appendInt(paramIndex);
            writer.writeText(tmp.str());

            paramIndex++;
         }
         writer.writeText(",");
      }
      else if (name[i] == '[' && name[i + 1] == ']') {
         // skip array brackets
         i++;
      }
      else if (!paramMode || allowResolvedTemplates)
         writer.writeChar(name[i]);
   }
}

void writeClassName(TextFileWriter& writer, ApiClassInfo* info)
{
   if (info->prefix.length() > 0) {
      writer.writeText(*info->prefix);
   }

   writer.writeText("<SPAN CLASS=\"typeNameLabel\">");
   writer.writeText(*info->name);
   writer.writeText("</SPAN>");

   const char* descr = *info->shortDescr;
   if (!emptystr(descr)) {
      writer.writeTextLine("<BR/>");
      writer.writeText("<I>");
      writer.writeText(descr);
      writer.writeText("</I>");
   }
}

void writeSymbolName(TextFileWriter& writer, ApiSymbolInfo* info)
{
   const char* descr = *info->shortDescr;
   if (!emptystr(descr)) {
      writer.writeText(descr);
   }
}

void writeType(TextFileWriter& writer, bool paramMode, ustr_t type, bool fullReference = false)
{
   if (type.startsWith("params ")) {
      writer.writeText("<I>params</I>&nbsp;");
      type = type + 7;
   }
   if (paramMode && type.startsWith(REF_TEMPLATE_PREFIX)) {
      writer.writeText("<I>ref</I>&nbsp;");

      IdentifierString tmp(type + getlength(REF_TEMPLATE_PREFIX));
      if ((*tmp).endsWith("&gt;"))
         tmp.cut(tmp.length() - 4, 4);

      writeType(writer, false, *tmp, fullReference);

      return;
   }

   writer.writeText("<SPAN CLASS=\"memberNameLink\">");
   if (type.find('\'') != NOTFOUND_POS) {
      writer.writeText("<A HREF=\"");

      size_t index = type.findStr("&lt;");
      NamespaceString ns(type);
      if (index != NOTFOUND_POS) {
         size_t i = index;
         while (true) {
            if (type[i] == '\'') {
               ns.copy(type, i);
               break;
            }
            if (i != 0) {
               i--;
            }
            else break;
         }
      }

      if (emptystr(*ns)) {
         writer.writeText("#");
      }
      else {
         writeNs(writer, *ns);
         writer.writeText(".html#");
      }
      writeRefName(writer, type + ns.length() + 1, false);

      writer.writeText("\">");

      if (type.startsWith(ARRAY_TEMPLATE_PREFIX)) {
         IdentifierString tmp(type);
         int counter = 0;
         while ((*tmp).startsWith(ARRAY_TEMPLATE_PREFIX)) {
            tmp.cut(0, getlength(ARRAY_TEMPLATE_PREFIX));
            if ((*tmp).endsWith("&gt;"))
               tmp.cut(tmp.length() - 4, 4);

            counter++;
         }

         writer.writeText(*tmp);

         for (int i = 0; i < counter; i++) {
            writer.writeText("<I>[]</I>");
         }
      }
      else {
         if (fullReference) {
            writer.writeText(type);
         }
         else writer.writeText(type + ns.length() + 1);
      }

      writer.writeText("</A>");
   }
   else writer.writeText(type);

   writer.writeText("</SPAN>");
}

void writeSummaryTable(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<TD CLASS=\"colFirst\">");

   writer.writeText("<A HREF=\"");
   writer.writeText(bodyFileName);
   writer.writeText("#");
   writeRefName(writer, *info->name, false);
   writer.writeText("\">");
   writer.writeText(*info->name);
   writer.writeTextLine("</A>");
   writer.writeTextLine("</TD>");

   writer.writeTextLine("<TD CLASS=\"colLast\">");
   writer.writeTextLine("<DIV CLASS=\"block\">");
   writeClassName(writer, info);
   writer.writeTextLine("</DIV>");
   writer.writeTextLine("</TD>");
}

void writeSummaryTable(TextFileWriter& writer, ApiSymbolInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<TD CLASS=\"colFirst\">");

   writer.writeText("<A HREF=\"");
   writer.writeText(bodyFileName);
   writer.writeText("#");
   writeRefName(writer, (*info->name), false);
   writer.writeText("\">");
   writer.writeText(*info->name);
   writer.writeTextLine("</A>");
   writer.writeTextLine("</TD>");

   writer.writeTextLine("<TD CLASS=\"colLast\">");
   writer.writeTextLine("<DIV CLASS=\"block\">");
   writer.writeText(*info->name);
   writer.writeTextLine("</DIV>");
   writer.writeTextLine("</TD>");
}

void writeClassBodyHeader(TextFileWriter& writer, ApiClassInfo* info, ustr_t moduleName)
{
   writer.writeText("<A NAME=\"");
   writer.writeText(*info->name);
   writer.writeTextLine("\">");
   writer.writeTextLine("</A>");

   writer.writeTextLine("<!-- ======== START OF CLASS DATA ======== -->");

   writer.writeTextLine("<DIV CLASS=\"header\">");

   writer.writeTextLine("<DIV CLASS=\"subTitle\">");
   writer.writeText(moduleName);
   writer.writeText("'");
   writer.writeTextLine("</DIV>");

   writer.writeText("<H2 title=\"");
   writer.writeText(*info->title);
   writer.writeText("\" class=\"title\">");
   writer.writeText(*info->title);
   writer.writeTextLine("</H2>");

   writer.writeTextLine("</DIV>");

   writer.writeTextLine("<DIV CLASS=\"contentContainer\">");

   writer.writeTextLine("<DIV CLASS=\"description\">");
   writer.writeTextLine("<BR>");
   writer.writeTextLine("<HR>");
   //const char* descr = nullptr;
   writer.writeTextLine("<PRE STYLE=\"padding-top: 15px;\">");
   writeClassName(writer, info);
   writer.writeTextLine("</PRE>");
   writer.writeTextLine("<BR>");
   writer.writeTextLine("</DIV>");
}

void writeSymbolBodyHeader(TextFileWriter& writer, ApiSymbolInfo* info, ustr_t moduleName)
{
   writer.writeText("<A NAME=\"");
   writer.writeText(*info->name);
   writer.writeTextLine("\">");
   writer.writeTextLine("</A>");

   writer.writeTextLine("<!-- ======== START OF SYMBOL DATA ======== -->");

   writer.writeTextLine("<DIV CLASS=\"header\">");

   writer.writeTextLine("<DIV CLASS=\"subTitle\">");
   writer.writeText(moduleName);
   writer.writeText("'");
   writer.writeTextLine("</DIV>");

   writer.writeText("<H2 title=\"");
   writer.writeText(*info->title);
   writer.writeText("\" class=\"title\">");
   writer.writeText(*info->title);
   writer.writeTextLine("</H2>");

   writer.writeTextLine("</DIV>");

   writer.writeTextLine("<DIV CLASS=\"contentContainer\">");

   writer.writeTextLine("<DIV CLASS=\"description\">");
   writer.writeTextLine("<BR>");
   writer.writeTextLine("<HR>");
   //const char* descr = nullptr;
   writer.writeTextLine("<PRE STYLE=\"padding-top: 15px;\">");
   writeSymbolName(writer, info);
   writer.writeTextLine("</PRE>");
   writer.writeTextLine("<BR>");
   writer.writeTextLine("</DIV>");
}

void writeParent(TextFileWriter& writer, StringList::Iterator& it, ApiClassInfo* info)
{
   writer.writeTextLine("<UL CLASS=\"inheritance\">");
   writer.writeTextLine("<LI>");

   if (!it.eof()) {
      writeType(writer, false, (*it), true);
      writer.writeTextLine("</LI>");

      it++;
      writer.writeTextLine("<LI>");
      writeParent(writer, it, info);
   }
   else {
      writer.writeText(*info->fullName);
   }

   writer.writeTextLine("</LI>");

   writer.writeTextLine("</UL>");
}

void writeParents(TextFileWriter& writer, ApiClassInfo* info, ustr_t moduleName)
{
   auto it = info->parents.start();
   writeParent(writer, it, info);
}

void writeClassMethodsHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== METHOD SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Method Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Method</TH>");
   writer.writeTextLine("</TR>");
}

void writePropertyHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== PROPERTY SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Property Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Property</TH>");
   writer.writeTextLine("</TR>");
}

void writeConversionHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== PROPERTY SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Conversion Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Conversion Method</TH>");
   writer.writeTextLine("</TR>");
}

void writeFieldHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== FIELD SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Field Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Field</TH>");
   writer.writeTextLine("</TR>");
}

void writeConstructorHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== CONSTRUCTOR SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Constructor Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Constructor / Static Method</TH>");
   writer.writeTextLine("</TR>");
}

void writeStaticPropertyHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== STATIC PROPERTY SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Static Property Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Static Property</TH>");
   writer.writeTextLine("</TR>");
}

void writeStaticMethodHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== STATIC METHOD SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Static Method Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Constructor / Static Method</TH>");
   writer.writeTextLine("</TR>");
}

void writeExtensionsHeader(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("<!-- ========== EXTENSION SUMMARY =========== -->");

   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Extension Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Extension Method</TH>");
   writer.writeTextLine("</TR>");
}

void writeSymbolHeader(TextFileWriter& writer)
{
   writer.writeTextLine("<DIV CLASS=\"contentContainer\">");
   writer.writeTextLine("<UL CLASS=\"blockList\">");
   writer.writeTextLine("<LI CLASS=\"blockList\">");

   writer.writeTextLine("<H3>Symbol Summary</H3>");

   writer.writeTextLine("<TABLE CLASS=\"memberSummary\" BORDER=\"0\" CELLPADDING=\"3\" CELLSPACING=\"0\">");
   writer.writeTextLine("<TR>");
   writer.writeTextLine("<TH CLASS=\"colFirst\" scope=\"col\">Modifier and Type</TH>");
   writer.writeTextLine("<TH CLASS=\"colLast\" scope=\"col\">Name</TH>");
   writer.writeTextLine("</TR>");

}

void writeFirstColumn(TextFileWriter& writer, ApiMethodInfo* info)
{
   writer.writeTextLine("<TD CLASS=\"colFirst\">");
   writer.writeTextLine("<CODE>");
   if (info->prefix.length() != 0) {
      writer.writeText("<i>");
      writer.writeText(*info->prefix);
      writer.writeText("</i>");
      writer.writeText("&nbsp;");
   }
   if (info->outputType.length() != 0) {
      writeType(writer, false, *info->outputType);
   }

   writer.writeTextLine("</CODE></TD>");
}

void writeSecondColumn(TextFileWriter& writer, ApiMethodInfo* info)
{
   writer.writeTextLine("<TD CLASS=\"colLast\">");
   writer.writeText("<CODE>");

   if (info->special)
      writer.writeText("<i>");

   if (info->function) {
      writer.writeText("function");
   }
   else writer.writeText(*info->name);

   if (info->special)
      writer.writeText("</i>");

   if ((info->property && info->paramNames.count() == 0) || info->cast) {
      writer.writeText("()");
   }
   else {
      writer.writeText("(");

      bool last = false;
      bool first = true;
      auto it = info->paramTypes.start();
      auto name_it = info->paramNames.start();
      while (!name_it.eof()) {
         last = name_it.last();

         if (!first) {
            writer.writeText(", ");
         }
         else first = false;

         if (!it.eof()) {
            if (last && info->variadic) {
               writer.writeText("<i>params</i> ");
               writeType(writer, false, *it);
               writer.writeText("<i>[]</i>");
            }
            else {
               writeType(writer, true, *it);
            }
            writer.writeText(" ");
            writer.writeText(*name_it);
            ++it;
         }
         else writer.writeText(*name_it);

         ++name_it;
      }

      writer.writeTextLine(")");
   }

   writer.writeTextLine("</CODE>");
   if (info->shortDescr.length() > 0) {
      writer.writeTextLine("<div class=\"block\">");
      writer.writeText(*info->shortDescr);
      writer.writeTextLine("</div>");
   }
   writer.writeTextLine("</TD>");
}

void writeFieldFirstColumn(TextFileWriter& writer, ApiFieldInfo* info)
{
   writer.writeTextLine("<TD CLASS=\"colFirst\">");
   writer.writeTextLine("<CODE>");
   if (info->prefix.length() != 0) {
      writer.writeText("<i>");
      writer.writeText(*info->prefix);
      writer.writeText("</i>");
      writer.writeText("&nbsp;");
   }
   if (info->type.length() != 0) {
      writeType(writer, false, *info->type);
   }

   writer.writeTextLine("</CODE></TD>");
}

void writeFieldSecondColumn(TextFileWriter& writer, ApiFieldInfo* info)
{
   writer.writeTextLine("<TD CLASS=\"colLast\">");
   writer.writeText("<CODE>");

   if (info->special)
      writer.writeText("<i>");

   writer.writeText(*info->name);

   if (info->special)
      writer.writeText("</i>");

   writer.writeTextLine("</CODE>");
   if (info->shortDescr.length() > 0) {
      writer.writeTextLine("<div class=\"block\">");
      writer.writeText(*info->shortDescr);
      writer.writeTextLine("</div>");
   }
   writer.writeTextLine("</TD>");
}


void writeSymbolFirstColumn(TextFileWriter& writer, ApiSymbolInfo* info)
{
   writer.writeTextLine("<TD CLASS=\"colFirst\">");
   writer.writeTextLine("<CODE>");
   if (info->prefix.length() != 0) {
      writer.writeText("<i>");
      writer.writeText(*info->prefix);
      writer.writeText("</i>");
      writer.writeText("&nbsp;");
   }
   if (info->type.length() != 0) {
      writeType(writer, false, *info->type);
   }

   writer.writeTextLine("</CODE></TD>");
}

void writeSymbolSecondColumn(TextFileWriter& writer, ApiSymbolInfo* info)
{
   writer.writeTextLine("<TD CLASS=\"colLast\">");
   writer.writeText("<CODE>");

   writer.writeText(*info->name);

   writer.writeTextLine("</CODE>");
   if (info->shortDescr.length() > 0) {
      writer.writeTextLine("<div class=\"block\">");
      writer.writeText(*info->shortDescr);
      writer.writeTextLine("</div>");
   }
   writer.writeTextLine("</TD>");
}
void writeConstructorFooter(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("</TABLE>");

   writer.writeTextLine("</LI>");
   writer.writeTextLine("</UL>");
}

void writeClassMethodsFooter(TextFileWriter& writer, ApiClassInfo* info, const char* bodyFileName)
{
   writer.writeTextLine("</TABLE>");

   writer.writeTextLine("</LI>");
   writer.writeTextLine("</UL>");
}

void writeSymbolFooter(TextFileWriter& writer)
{
   writer.writeTextLine("</TABLE>");

   writer.writeTextLine("</LI>");
   writer.writeTextLine("</UL>");

   writer.writeTextLine("</DIV>");
   writer.writeTextLine("<HR>");
   writer.writeTextLine("</DIV>");
}

void writeClassBodyFooter(TextFileWriter& writer, ApiClassInfo* info, ustr_t moduleName)
{
   writer.writeTextLine("<HR>");

   writer.writeTextLine("</DIV>");
}

void writeClassSummaryFooter(TextFileWriter& writer)
{
   writer.writeTextLine("</TABLE>");
   writer.writeTextLine("</LI>");
}

void writeSummaryFooter(TextFileWriter& writer)
{
   writer.writeTextLine("</UL>");
   writer.writeTextLine("</DIV>");
}

void writeFooter(TextFileWriter& writer, const char* packageLink)
{
   writer.writeTextLine("<DIV CLASS=\"bottomNav\">");

   writer.writeTextLine("<UL CLASS=\"navList\">");

   writer.writeTextLine("<LI>");
   writer.writeTextLine("<A HREF=\"index.html\">Overview</A>");
   writer.writeTextLine("</LI>");

   writer.writeTextLine("<LI>");
   if (!emptystr(packageLink)) {
      writer.writeText("<A HREF=\"");
      writer.writeText(packageLink);
      writer.writeTextLine("\">Module</A>");
   }
   else writer.writeTextLine("Module");
   writer.writeTextLine("</LI>");

   writer.writeTextLine("</UL>");

   writer.writeTextLine("<DIV CLASS=\"aboutLanguage\">");
   writer.writeTextLine("<STRONG>");
   writer.writeTextLine(TITLE2);
   writer.writeTextLine("</STRONG>");
   writer.writeTextLine("</DIV>");

   writer.writeTextLine("</DIV>");

   writer.writeTextLine("</BODY>");
   writer.writeTextLine("</HTML>");
}

// --- DocGenerator ---

struct HelpStruct
{
   DocGenerator*      docGenerator;
   ApiModuleInfoList* list;
};

ApiModuleInfo* DocGenerator :: findModule(ApiModuleInfoList& modules, ustr_t ns)
{
   for (auto it = modules.start(); !it.eof(); ++it) {
      if ((*it)->name.compare(ns)) {
         return *it;
      }
   }
   return nullptr;
}

ApiClassInfo* DocGenerator :: findClass(ApiModuleInfo* module, ustr_t fullName)
{
   for (auto it = module->classes.start(); !it.eof(); ++it) {
      if ((*it)->fullName.compare(fullName)) {
         return *it;
      }
   }
   return nullptr;
}

ApiSymbolInfo* DocGenerator :: findSymbol(ApiModuleInfo* module, ustr_t fullName)
{
   for (auto it = module->symbols.start(); !it.eof(); ++it) {
      if ((*it)->fullName.compare(fullName)) {
         return *it;
      }
   }
   return nullptr;
}

bool DocGenerator :: loadClassInfo(ref_t reference, ClassInfo& info, bool headerOnly)
{
   if (!reference)
      return false;

   auto section = _module->mapSection(reference | mskMetaClassInfoRef, true);
   if (!section) {
      ustr_t refName = _module->resolveReference(reference);
      if (isTemplateWeakReference(refName)) {
         ref_t resolvedReference = _module->mapReference(refName + getlength(TEMPLATE_PREFIX_NS) - 1);

         section = _module->mapSection(resolvedReference | mskMetaClassInfoRef, true);
         if (!section)
            return false;
      }
      else return false;
   }

   MemoryReader vmtReader(section);
   // read VMT info
   info.load(&vmtReader, headerOnly);

   return true;
}

bool DocGenerator :: loadSymbolInfo(ref_t reference, SymbolInfo& info)
{
   if (!reference)
      return false;

   auto section = _module->mapSection(reference | mskMetaSymbolInfoRef, true);
   if (!section) {
      return false;
   }

   MemoryReader vmtReader(section);
   // read VMT info
   info.load(&vmtReader);

   return true;
}

inline void readTemplateNs(IdentifierString& ns, ustr_t reference)
{
   size_t i = reference.find('#');

   ns.copy(reference, i);

   size_t j = (*ns).findLast('@');
   ns.truncate(j + 1);

   ns.replaceAll('@', '\'', 0);
}

inline bool isTemplateName(ustr_t name)
{
   size_t index = name.find('#');
   if (index != NOTFOUND_POS) {
      if (name[index + 1] >= '0' && name[index + 1] <= '9')
         return true;
   }

   return false;
}

//bool validateTemplateType(IdentifierString& type, bool templateBased, bool argMode)
//{
//   if (templateBased) {
//      if (isTemplateBased(*type)) {
//         if (isTemplateWeakReference(*type))
//            type.cut(0, 6);
//
//         retrieveTemplateType(type);
//      }
//      else if ((*type).findStr("$private'T") != NOTFOUND_POS) {
//         size_t index = (*type).findLast('\'');
//         type.cut(0, index + 1);
//      }
//
//      return true;
//   }
//   else if (isTemplateWeakReference(*type)) {
//      NamespaceString ns(*type);
//
//      return parseTemplateType(type, ns.length(), argMode);
//   }
//
//   return false;
//}
//

//void loadType(IdentifierString& type, ustr_t line, ustr_t rootNs, bool templateBased, bool argMode)
//{
//   bool nsExpected = false;
//   if (isTemplateWeakReference(line)) {
//      type.copy(line);
//      nsExpected = false;
//   }
//   else if (line[0] == '\'') {
//      type.copy(rootNs);
//      type.append(line);
//   }
//   else type.copy(line);
//
//   nsExpected &= validateTemplateType(type, templateBased, argMode);
//
//   if (nsExpected) {
//      size_t spacePos = (*type).find(' ');
//      type.insert("'", spacePos + 1);
//      type.insert(rootNs, spacePos + 1);
//   }
//}


// loadType(fieldInfo->type, typeName, *_rootNs, apiClassInfo->templateBased, false);

void DocGenerator :: loadType(ref_t reference, IdentifierString& target, bool templateMode)
{
   ustr_t name = _module->resolveReference(reference);

   loadType(name, target, templateMode);
}

void DocGenerator :: loadType(ustr_t name, IdentifierString& target, bool templateMode)
{
   if (isTemplateWeakReference(name)) {
      if (name.findStr("Rang") != NOTFOUND_POS)
         templateMode = true;

      IdentifierString type(name);
      IdentifierString ns;

      type.cut(0, 7);

      readTemplateNs(ns, *type);

      parseTemplateName(type, templateMode);
      type.insert(*ns, 0);

      target.copy(*type);
   }
   else if (isTemplateName(name)) {
      IdentifierString type(name);
      IdentifierString ns;

      readTemplateNs(ns, *type);

      parseTemplateName(type, templateMode);
      type.insert(*ns, 0);

      target.copy(*type);
   }
   else if (isWeakReference(name)) {
      target.copy(*_rootNs);
      target.append(name);
   }
   else target.copy(name);
}

void DocGenerator :: loadParents(ApiClassInfo* apiClassInfo, ref_t reference)
{
   if (!reference)
      return;

   // read VMT info
   ClassInfo info;
   if (loadClassInfo(reference, info, false)) {
      loadParents(apiClassInfo, info.header.parentRef);
   }

   IdentifierString typeName;
   loadType(reference, typeName, apiClassInfo->templateBased);
   apiClassInfo->parents.add((*typeName).clone());
}

void DocGenerator :: loadMethodName(ApiMethodInfo* apiMethodInfo, bool templateBased)
{
   bool skipOne = apiMethodInfo->extensionOne;

   ustr_t name = *apiMethodInfo->name;
   if (name.startsWith("static:")) {
      apiMethodInfo->name.cut(0, 7);
      name += 7;
   }

   size_t sign_index = name.find('<');
   if (sign_index != NOTFOUND_POS) {
      if (!apiMethodInfo->cast) {
         IdentifierString param;
         IdentifierString type;
         int argIndex = 1;
         for (size_t i = sign_index + 1; i < name.length(); i++) {
            if (name[i] == ',' || name[i] == '>') {
               if (skipOne) {
                  skipOne = false;
               }
               else {
                  loadType(*param, type, templateBased);

                  //if (info->withVargs && info->name[i] == '>') {
                  //   type.append("[]");

                  //   type.insert("params ", 0);
                  //}

                  IdentifierString argName("arg");
                  argName.appendInt(argIndex++);
                  apiMethodInfo->paramNames.add((*argName).clone());
                  apiMethodInfo->paramTypes.add((*type).clone());
               }
               param.clear();
               type.clear();
            }
            else param.append(name[i]);
         }
      }

      apiMethodInfo->name.truncate(sign_index);
   }
   else {
      size_t arg_index = name.find('[');
      if (arg_index != NOTFOUND_POS) {
         String<char, 10> temp;
         temp.copy(name + arg_index + 1, name.length() - arg_index - 2);
         int argCount = temp.toUInt(10);
         for (int i = 1; i < argCount; i++) {
            IdentifierString argName("arg");
            argName.appendInt(i);

            apiMethodInfo->paramNames.add((*argName).clone());
         }

         apiMethodInfo->name.truncate(arg_index);
      }
   }
}

void DocGenerator :: loadClassMethod(ApiClassInfo* apiClassInfo, mssg_t message, MethodInfo& methodInfo, 
   MemberType memberType, DescriptionMap* descriptions, ClassInfo& classInfo)
{
   auto apiMethodInfo = new ApiMethodInfo();
   apiMethodInfo->extensionOne = memberType == MemberType::Extension;

   if (test(methodInfo.hints, (ref_t)MethodHint::Constructor))
      apiMethodInfo->constructor = true;

   if (ByteCodeUtil::resolveMessageName(apiMethodInfo->name, _module, message)) {
      if (descriptions) {
         ustr_t descr = descriptions->get(*apiMethodInfo->name);
         if (descr)
            apiMethodInfo->shortDescr.copy(descr);
      }

      if ((*apiMethodInfo->name).startsWith(DISPATCH_MESSAGE)) {
         apiMethodInfo->special = true;
         apiMethodInfo->name.copy("dispatch");
      }
      else if ((*apiMethodInfo->name).findStr(CONSTRUCTOR_MESSAGE) != NOTFOUND_POS) {
         if ((*apiMethodInfo->name).findStr("$#constructor") != NOTFOUND_POS)
            apiMethodInfo->function = false;

         if (test(message, STATIC_MESSAGE))
            apiMethodInfo->prefix.append("private ");

         if ((*apiMethodInfo->name).findStr("$$") != NOTFOUND_POS) {
            apiMethodInfo->prefix.append("internal ");

            apiMethodInfo->name.cut(0, (*apiMethodInfo->name).findStr("$$") + 2);
         }

         if ((*apiMethodInfo->name).startsWith("static:"))
            apiMethodInfo->name.cut(0, 7);

         if ((*apiMethodInfo->name).startsWith("function:"))
            apiMethodInfo->name.cut(0, 9);

         if ((*apiMethodInfo->name).startsWith("params:")) {
            apiMethodInfo->name.cut(0, 7);
            apiMethodInfo->variadic = true;
         }

         if (apiMethodInfo->name[0] == '$') {
            // if it is interpolate constructor
            apiMethodInfo->name.cut(0, 2);

            apiMethodInfo->name.insert("interpolate ", 0);
         }
         else apiMethodInfo->name.cut(0, 1);

         apiMethodInfo->special = true;

         loadMethodName(apiMethodInfo, apiClassInfo->templateBased);
         if (methodInfo.outputRef) {
            loadType(methodInfo.outputRef, apiMethodInfo->outputType, apiClassInfo->templateBased);
         }
      }
      else {
         if ((*apiMethodInfo->name).startsWith("function:")) {
            apiMethodInfo->name.cut(0, 9);
            if (memberType != MemberType::Extension)
               apiMethodInfo->function = true;
         }
         if ((*apiMethodInfo->name).startsWith("prop:")) {
            apiMethodInfo->name.cut(0, 5);
            apiMethodInfo->property = true;
            if (getArgCount(message) > 1) {
               apiMethodInfo->prefix.append("set ");
            }
            else apiMethodInfo->prefix.append("get ");
         }
         if ((*apiMethodInfo->name).startsWith("params:")) {
            apiMethodInfo->name.cut(0, 7);
            apiMethodInfo->variadic = true;
         }
         if ((*apiMethodInfo->name).startsWith("typecast:#cast")) {
            apiMethodInfo->name.cut(0, 10);
            apiMethodInfo->special = true;
            apiMethodInfo->cast = true;
         }

         if (MethodInfo::checkVisibility(methodInfo, MethodHint::Internal)) {
            size_t index = (*apiMethodInfo->name).findStr("$$");
            if (index != NOTFOUND_POS)
               apiMethodInfo->name.cut(0, index + 2);

            apiMethodInfo->prefix.append("internal ");
         }

         if (test(methodInfo.hints, (ref_t)MethodHint::Predefined))
            apiMethodInfo->prefix.append("predefined ");

         if (test(methodInfo.hints, (ref_t)MethodHint::Abstract))
            apiMethodInfo->prefix.append("abstract ");

         if (test(message, STATIC_MESSAGE))
            apiMethodInfo->prefix.append("private ");

         loadMethodName(apiMethodInfo, apiClassInfo->templateBased);
         if (methodInfo.outputRef) {
            loadType(methodInfo.outputRef, apiMethodInfo->outputType, apiClassInfo->templateBased);
         }
      }

      if (!test(methodInfo.hints, (ref_t)MethodHint::Autogenerated) 
         && (!_publicOnly || !MethodInfo::checkVisibility(methodInfo, MethodHint::Private)))
      {
         bool first = true;
         for (auto attr_it = classInfo.attributes.start(); !attr_it.eof(); ++attr_it) {
            auto key = attr_it.key();

            if (key.value2 == ClassAttribute::ParameterName && key.value1 == message) {
               if (first) {
                  first = false;
                  apiMethodInfo->paramNames.clear();
               }

               MemoryReader reader(_parameterNames, *attr_it);

               apiMethodInfo->paramNames.add(reader.getString(nullptr).clone());
            }
         }

         if (memberType == MemberType::ClassClass) {
            if (apiMethodInfo->property) {
               apiClassInfo->staticProperties.add(apiMethodInfo);
            }
            else if (!apiMethodInfo->constructor) {
               apiClassInfo->staticMethods.add(apiMethodInfo);
            }
            else apiClassInfo->constructors.add(apiMethodInfo);
         }
         else if (memberType == MemberType::Extension) {
            if (apiMethodInfo->property)
               apiMethodInfo->prefix.append("property ");

            apiClassInfo->extensions.add(apiMethodInfo);
         }
         else if (apiMethodInfo->property) {
            apiClassInfo->properties.add(apiMethodInfo);
         }
         else if (apiMethodInfo->cast) {
            apiClassInfo->convertors.add(apiMethodInfo);
         }
         else apiClassInfo->methods.add(apiMethodInfo);
      }
      else freeobj(apiMethodInfo);
   }
}

void DocGenerator :: loadConstructors(ApiClassInfo* apiClassInfo, ref_t reference, DescriptionMap* descriptions)
{
   ClassInfo info;
   if (loadClassInfo(reference, info, false)) {
      for (auto m_it = info.methods.start(); !m_it.eof(); ++m_it) {
         auto methodInfo = *m_it;
         if (!methodInfo.inherited) {
            loadClassMethod(apiClassInfo, m_it.key(), methodInfo, MemberType::ClassClass, descriptions, info);
         }
      }
   }
}

void DocGenerator :: loadExtensions(ApiClassInfo* apiClassInfo, ref_t reference, DescriptionMap* descriptions)
{
   ClassInfo info;
   if (loadClassInfo(reference, info, false)) {
      for (auto m_it = info.methods.start(); !m_it.eof(); ++m_it) {
         auto methodInfo = *m_it;
         if (!methodInfo.inherited) {
            loadClassMethod(apiClassInfo, m_it.key(), methodInfo, MemberType::Extension, descriptions, info);
         }
      }
   }
}

void DocGenerator :: loadFields(ApiClassInfo* apiClassInfo, ClassInfo& info)
{
   for (auto it = info.fields.start(); !it.eof(); ++it) {
      auto fieldInfo = new ApiFieldInfo();

      fieldInfo->name.copy(it.key());

      ref_t typeRef = (*it).typeInfo.typeRef;
      if (typeRef) {
         switch (typeRef) {
            case V_INT32:
               fieldInfo->type.copy("__int[4]");
               break;
            default:
               if (!isPrimitiveRef(typeRef)) {
                  loadType(typeRef, fieldInfo->type, apiClassInfo->templateBased);
               }
               break;
         }

      }

      apiClassInfo->fields.add(fieldInfo);
   }
}

void DocGenerator :: loadClassMembers(ApiClassInfo* apiClassInfo, ref_t reference, DescriptionMap* descriptions)
{
   ClassInfo info;
   if (loadClassInfo(reference, info, false)) {
      loadParents(apiClassInfo, info.header.parentRef);

      loadFields(apiClassInfo, info);

      for (auto m_it = info.methods.start(); !m_it.eof(); ++m_it) {
         auto methodInfo = *m_it;
         if (!methodInfo.inherited) {
            loadClassMethod(apiClassInfo, m_it.key(), methodInfo, MemberType::Normal, descriptions, info);
         }
      }
   }
}

bool DocGenerator :: isExtension(ref_t reference)
{
   ClassInfo info;
   if (loadClassInfo(reference, info, true)) {
      return test(info.header.flags, elExtension);
   }
   else return false;
}

ref_t DocGenerator :: findExtensionTarget(ref_t reference)
{
   ClassInfo info;
   if (loadClassInfo(reference, info, false)) {
      ref_t targetRef = info.attributes.get({ 0, ClassAttribute::ExtensionRef });

      return targetRef;
   }
   else return 0;
}

inline void replace(IdentifierString& str, ustr_t oldStr, ustr_t newStr)
{
   size_t index = (*str).findStr(oldStr);
   if (index != NOTFOUND_POS) {
      str.cut(index, oldStr.length());
      str.insert(newStr, index);
   }
}

void DocGenerator :: loadClassPrefixes(ApiClassInfo* apiClassInfo, ref_t reference)
{
   ClassInfo info;
   if (loadClassInfo(reference, info, true)) {
      if (test(info.header.flags, elStateless) && test(info.header.flags, elSealed)) {
         replace(apiClassInfo->prefix, "class", "singleton");
      }

      if (test(info.header.flags, elAbstract)) {
         apiClassInfo->prefix.insert("abstract ", 0);
      }
   }
}

bool isTemplateDeclaration(ustr_t referenceName)
{
   size_t index = referenceName.find('#');
   if (index == NOTFOUND_POS)
      return false;

   // if it is a template of template
   if (referenceName.findSub(index + 1, '#') != NOTFOUND_POS)
      return false;

   return referenceName.findStr("@T1") != NOTFOUND_POS/* && referenceName.findStr("$private") != NOTFOUND_POS*/;
}

bool isOwnTemplate(ustr_t referenceName, ustr_t ns, NamespaceString& templateNs)
{
   ReferenceProperName templateName(referenceName);

   size_t index = (*templateName).find('#');
   templateName.cut(index, templateName.length() - index);
   index = (*templateName).findLast('@');
   templateName.cut(index, templateName.length() - index);
   templateName.replaceAll('@', '\'', 0);

   if (!ns.compare(*templateName)) {
      if ((*templateName).startsWith(ns) && (*templateName)[ns.length()] == '\'') {
         templateNs.copy(*templateName);

         return true;
      }
      return false;
   }
   else return true;
}

void DocGenerator :: loadMember(ApiModuleInfoList& modules, ref_t reference)
{
   auto referenceName = _module->resolveReference(reference);

   // HOTFIX : skip internal references
   if (referenceName.findStr("$$") != NOTFOUND_POS)
      return;

   // HOTFIX : skip static references
   if (referenceName.findStr(STATICFIELD_POSTFIX) != NOTFOUND_POS)
      return;

   if (isWeakReference(referenceName)) {
      IdentifierString prefix("public ");
      if (referenceName.startsWith(INTERNAL_PREFIX)) {
         if (_publicOnly)
            return;

         referenceName += getlength(INTERNAL_PREFIX);
         prefix.copy("intern ");
      }
      else if (referenceName.startsWith(PRIVATE_PREFIX)) {
         if (_publicOnly)
            return;

         referenceName += getlength(PRIVATE_PREFIX);
         prefix.copy("private ");
      }
      else if (referenceName.startsWith(TEMPLATE_PREFIX)) {
         return;
      }

      ref_t classClassRef = 0;
      ref_t extensionRef = 0;
      if (referenceName.endsWith(CLASSCLASS_POSTFIX)) {
         classClassRef = reference;
         IdentifierString name(referenceName);
         name.truncate(name.length() - getlength(CLASSCLASS_POSTFIX));

         reference = _module->mapReference(*name, true);
         referenceName = _module->resolveReference(reference);
      }

      ReferenceProperName properName(referenceName);
      ReferenceName fullName(*_rootNs, referenceName + 1);

      // HOTFIX : skip internal class
      if (properName[0] == '$')
         return;

      NamespaceString ns(*fullName);
      if (isExtension(reference)) {
         extensionRef = reference;
         reference = findExtensionTarget(extensionRef);
         referenceName = _module->resolveReference(reference);
         if (isWeakReference(referenceName)) {
            fullName.copy(*_rootNs);
            fullName.append(referenceName);
         }
         else fullName.copy(referenceName);

         size_t pos = referenceName.findLast('\'', 0);
         properName.copy(referenceName.str() + pos + 1);
      }

      if (_module->mapSection(reference | mskVMTRef, true) || extensionRef) {
         bool templateBased = false;
         if (isTemplateBased(referenceName)) {
            if (isTemplateDeclaration(referenceName) && isOwnTemplate(referenceName, _module->name(), ns)) {
               templateBased = true;

               parseTemplateName(properName, templateBased);

               fullName.copy(*ns);
               fullName.combine(*properName);
            }
            else return;

            prefix.append("template ");
         }
         else prefix.append("class ");

         ApiModuleInfo* moduleInfo = findModule(modules, *ns);
         if (!moduleInfo) {
            moduleInfo = new ApiModuleInfo();
            moduleInfo->name.copy(*ns);

            modules.add(moduleInfo);
         }

         ApiClassInfo* info = findClass(moduleInfo, *fullName);
         if (!info) {
            info = new ApiClassInfo();

            if (extensionRef)
               info->virtualMode = true;

            info->fullName.copy(*fullName);
            info->name.copy(*properName);
            info->title.copy(*properName);
            info->prefix.copy(*prefix);

            loadClassPrefixes(info, reference);

            moduleInfo->classes.add(info);

            ustr_t descr = _classDescriptions.get(referenceName);
            if (!descr.empty())
               info->shortDescr.copy(descr);
         }
         else if (!extensionRef){
            if (classClassRef && info->classCompleted)
               return;
            if (!classClassRef && info->completed)
               return;

            info->virtualMode = false;

            info->prefix.copy(*prefix);
            loadClassPrefixes(info, reference);
         }

         if (templateBased)
            info->templateBased = true;

         if (classClassRef != 0) {
            DescriptionMap descriptions(nullptr);
            IdentifierString descrName("$");
            descrName.append(referenceName);
            descrName.replaceAll('\'', '@', 0);
            descrName.insert(DESCRIPTION_SECTION, 0);

            loadDescriptions(_module->mapReference(*descrName), descriptions);

            loadConstructors(info, classClassRef, &descriptions);

            // HOTFIX : to skip duplicates
            info->classCompleted = true;
         }
         else if (extensionRef != 0) {
            DescriptionMap descriptions(nullptr);
            IdentifierString descrName("$");
            descrName.append(_module->resolveReference(extensionRef));
            descrName.replaceAll('\'', '@', 0);
            descrName.insert(DESCRIPTION_SECTION, 0);

            loadDescriptions(_module->mapReference(*descrName), descriptions);

            loadExtensions(info, extensionRef, &descriptions);
         }
         else {
            DescriptionMap descriptions(nullptr);
            IdentifierString descrName("$");
            descrName.append(referenceName);
            descrName.replaceAll('\'', '@', 0);
            descrName.insert(DESCRIPTION_SECTION, 0);

            loadDescriptions(_module->mapReference(*descrName), descriptions);

            loadClassMembers(info, reference, &descriptions);

            // HOTFIX : to skip duplicates
            info->completed = true;
         }
      }
      else if (_module->mapSection(reference | mskSymbolRef, true)) {
         ApiModuleInfo* moduleInfo = findModule(modules, *ns);
         if (!moduleInfo) {
            moduleInfo = new ApiModuleInfo();
            moduleInfo->name.copy(*ns);

            modules.add(moduleInfo);
         }

         SymbolInfo symbolInfo;
         loadSymbolInfo(reference, symbolInfo);

         ApiClassInfo* classInfo = findClass(moduleInfo, *fullName);
         // HOTFIX : skip the class symbol
         if (classInfo)
            return;

         ApiSymbolInfo* apiSymbolInfo = findSymbol(moduleInfo, *fullName);
         if (!apiSymbolInfo) {
            apiSymbolInfo = new ApiSymbolInfo();

            apiSymbolInfo->fullName.copy(*fullName);
            apiSymbolInfo->name.copy(*properName);
            apiSymbolInfo->title.copy(*properName);
            apiSymbolInfo->prefix.copy(*prefix);

            moduleInfo->symbols.add(apiSymbolInfo);

            if (symbolInfo.typeRef) {
               loadType(symbolInfo.typeRef, apiSymbolInfo->type, false);
            }
         }
      }
   }
}

void DocGenerator :: loadNestedModules(ApiModuleInfoList& modules)
{
   _presenter->printLine(LDOC_READING);

   HelpStruct arg = { this, &modules };

   _module->forEachReference(&arg, [](ModuleBase* module, ref_t reference, void* arg)
      {
         HelpStruct* dicArg = static_cast<HelpStruct*>(arg);

         dicArg->docGenerator->loadMember(*dicArg->list, reference);
      });
}

void DocGenerator :: generateMethodList(TextFileWriter& bodyWriter, ApiMethodInfoList& list)
{
   bool alt = true;
   for (auto it = list.start(); !it.eof(); ++it) {
      if (alt) {
         bodyWriter.writeTextLine("<TR CLASS=\"altColor\">");
      }
      else {
         bodyWriter.writeTextLine("<TR CLASS=\"rowColor\">");
      }
      alt = !alt;

      writeFirstColumn(bodyWriter, *it);
      writeSecondColumn(bodyWriter, *it);

      bodyWriter.writeTextLine("</TR>");
   }
}

void DocGenerator :: generateFieldList(TextFileWriter& bodyWriter, ApiFieldInfoList& list)
{
   bool alt = true;
   for (auto it = list.start(); !it.eof(); ++it) {
      if (alt) {
         bodyWriter.writeTextLine("<TR CLASS=\"altColor\">");
      }
      else {
         bodyWriter.writeTextLine("<TR CLASS=\"rowColor\">");
      }
      alt = !alt;

      writeFieldFirstColumn(bodyWriter, *it);
      writeFieldSecondColumn(bodyWriter, *it);

      bodyWriter.writeTextLine("</TR>");
   }
}

void DocGenerator :: generateClassDoc(TextFileWriter& summaryWriter, TextFileWriter& bodyWriter, ApiClassInfo* classInfo, ustr_t bodyName)
{
   IdentifierString moduleName;
   parseNs(moduleName, *_rootNs, *classInfo->fullName);

   writeSummaryTable(summaryWriter, classInfo, bodyName);

   writeClassBodyHeader(bodyWriter, classInfo, *moduleName);

   if (classInfo->parents.count() > 0) {
      writeParents(bodyWriter, classInfo, *_rootNs);
   }

   if (classInfo->fields.count() > 0) {
      writeFieldHeader(bodyWriter, classInfo, *moduleName);
      generateFieldList(bodyWriter, classInfo->fields);
      writeConstructorFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->constructors.count() > 0) {
      writeConstructorHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->constructors);
      writeConstructorFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->staticMethods.count() > 0) {
      writeStaticMethodHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->staticMethods);
      writeConstructorFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->staticProperties.count() > 0) {
      writeStaticPropertyHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->staticProperties);
      writeConstructorFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->properties.count() > 0) {
      writePropertyHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->properties);
      writeClassMethodsFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->convertors.count() > 0) {
      writeConversionHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->convertors);
      writeClassMethodsFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->methods.count() > 0) {
      writeClassMethodsHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->methods);
      writeClassMethodsFooter(bodyWriter, classInfo, *moduleName);
   }

   if (classInfo->extensions.count() > 0) {
      writeExtensionsHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->extensions);
      writeClassMethodsFooter(bodyWriter, classInfo, *moduleName);
   }

   writeClassBodyFooter(bodyWriter, classInfo, *moduleName);
}

void DocGenerator :: generateSymbolDoc(TextFileWriter& summaryWriter, TextFileWriter& bodyWriter, ApiSymbolInfo* symbolInfo, ustr_t bodyName)
{
   IdentifierString moduleName;
   parseNs(moduleName, *_rootNs, *symbolInfo->fullName);

   writeSummaryTable(summaryWriter, symbolInfo, bodyName);

   writeSymbolBodyHeader(bodyWriter, symbolInfo, *moduleName);

   writeSymbolHeader(bodyWriter);
   writeSymbolFirstColumn(bodyWriter, symbolInfo);
   writeSymbolSecondColumn(bodyWriter, symbolInfo);
   writeSymbolFooter(bodyWriter);
}

void DocGenerator :: generateExtendedDoc(TextFileWriter& summaryWriter, TextFileWriter& bodyWriter, ApiClassInfo* classInfo, ustr_t bodyName)
{
   IdentifierString moduleName;
   parseNs(moduleName, *_rootNs, *classInfo->fullName);

   writeSummaryTable(summaryWriter, classInfo, bodyName);

   writeClassBodyHeader(bodyWriter, classInfo, *moduleName);

   if (classInfo->extensions.count() > 0) {
      writeExtensionsHeader(bodyWriter, classInfo, *moduleName);
      generateMethodList(bodyWriter, classInfo->extensions);
      writeClassMethodsFooter(bodyWriter, classInfo, *moduleName);
   }

   writeClassBodyFooter(bodyWriter, classInfo, *moduleName);
}

void DocGenerator :: generateModuleDoc(ApiModuleInfo* moduleInfo, path_t output)
{
   _presenter->printLine(LDOC_GENERATING, *moduleInfo->name);

   IdentifierString name;
   writeNs(name, moduleInfo);
   name.append(".html");

   IdentifierString summaryname;
   writeNs(summaryname, moduleInfo);
   summaryname.append("-summary");
   summaryname.append(".html");

   PathString outPath(output);
   if (!output.empty()) {
      outPath.combine(*name);
   }
   else outPath.copy(*name);

   PathString outSumPath(output);
   if (!output.empty()) {
      outSumPath.combine(*summaryname);
   }
   else outSumPath.copy(*summaryname);

   TextFileWriter bodyWriter(outPath.str(), FileEncoding::UTF8, false);
   TextFileWriter summaryWriter(outSumPath.str(), FileEncoding::UTF8, false);

   writeHeader(summaryWriter, *moduleInfo->name, nullptr);
   writeHeader(bodyWriter, *moduleInfo->name, *summaryname);

   writeSummaryHeader(summaryWriter, *moduleInfo->name, *moduleInfo->shortDescr);

   bool withVirtualOnes = false;
   if (moduleInfo->classes.count() > 0) {
      // classes
      writeClassSummaryHeader(summaryWriter);

      bool alt = true;
      for (auto class_it = moduleInfo->classes.start(); !class_it.eof(); ++class_it) {
         ApiClassInfo* classInfo = *class_it;
         if (classInfo->virtualMode) {
            withVirtualOnes = true;
            continue;
         }

         if (alt) {
            summaryWriter.writeTextLine("<TR CLASS=\"altColor\">");
         }
         else {
            summaryWriter.writeTextLine("<TR CLASS=\"rowColor\">");
         }
         alt = !alt;

         generateClassDoc(summaryWriter, bodyWriter, classInfo, *name);

         summaryWriter.writeTextLine("</TR>");
      }

      writeClassSummaryFooter(summaryWriter);
   }

   if (moduleInfo->symbols.count() > 0) {
      // symbols
      writeSymbolSummaryHeader(summaryWriter);

      bool alt = true;
      for (auto symbol_it = moduleInfo->symbols.start(); !symbol_it.eof(); ++symbol_it) {
         ApiSymbolInfo* symbolInfo = *symbol_it;
         if (alt) {
            summaryWriter.writeTextLine("<TR CLASS=\"altColor\">");
         }
         else {
            summaryWriter.writeTextLine("<TR CLASS=\"rowColor\">");
         }
         alt = !alt;

         generateSymbolDoc(summaryWriter, bodyWriter, symbolInfo, *name);

         summaryWriter.writeTextLine("</TR>");
      }

      writeClassSummaryFooter(summaryWriter);
   }

   if (withVirtualOnes) {
      // classes
      writeExtendedSummaryHeader(summaryWriter);

      bool alt = true;
      for (auto class_it = moduleInfo->classes.start(); !class_it.eof(); ++class_it) {
         ApiClassInfo* classInfo = *class_it;
         if (!classInfo->virtualMode) {
            continue;
         }

         if (alt) {
            summaryWriter.writeTextLine("<TR CLASS=\"altColor\">");
         }
         else {
            summaryWriter.writeTextLine("<TR CLASS=\"rowColor\">");
         }
         alt = !alt;

         generateExtendedDoc(summaryWriter, bodyWriter, classInfo, *name);

         summaryWriter.writeTextLine("</TR>");
      }

      writeClassSummaryFooter(summaryWriter);
   }

   writeSummaryFooter(summaryWriter);

   writeFooter(summaryWriter, nullptr);
   writeFooter(bodyWriter, *summaryname);
}

void DocGenerator :: loadDescriptions(ref_t descrRef, DescriptionMap& map)
{
   auto section = _module->mapSection(descrRef | mskStringMapRef, true);
   if (section != nullptr) {
      IdentifierString key;
      IdentifierString value;
      MemoryReader reader(section);
      while (!reader.eof()) {
         reader.readString(key);
         int type = reader.getDWord();

         if (type == 2) {
            reader.getDWord();

            reader.readString(value);

            map.add(*key, (*value).clone());
         }
      }
   }
}

void DocGenerator :: loadDescriptions()
{
   ref_t descrRef = _module->mapReference(DESCRIPTION_SECTION, true);

   loadDescriptions(descrRef, _classDescriptions);
}

void DocGenerator :: loadMetaSections()
{
   IdentifierString sectionName(META_PREFIX, PARAMETER_NAMES);

   ref_t paramNameRef = _module->mapReference(*sectionName, true);
   if (paramNameRef) {
      _parameterNames = _module->mapSection(paramNameRef | mskLiteralListRef, true);
   }
}

bool DocGenerator :: load(path_t path)
{
   FileNameString fileName(path);
   IdentifierString name(*fileName);
   _rootNs.copy(*name);

   _module = new Module();

   FileReader reader(path, FileRBMode, FileEncoding::Raw, false);
   auto retVal = static_cast<Module*>(_module)->load(reader);

   if (retVal == LoadResult::Successful) {
      loadMetaSections();

      return true;
   }
   else return false;
}

bool DocGenerator :: loadByName(ustr_t name)
{
   _module = _provider->loadModule(name);

   _rootNs.copy(name);

   if (_module) {
      loadMetaSections();

      return true;
   }
   else return false;
}

void DocGenerator :: generate(path_t output)
{
   loadDescriptions();

   ApiModuleInfoList modules(nullptr);
   loadNestedModules(modules);

   for (auto it = modules.start(); !it.eof(); ++it) {
      generateModuleDoc(*it, output);
   }
}