#ifndef CODEGEN_ECODE_H
#define CODEGEN_ECODE_H

#include "dispatch.h"
#include "runtime.h"

namespace elena_lang::codegen
{
   enum class ECodeFlow : unsigned char
   {
      Next,
      DirectBranch,
      ConditionalBranch,
      IndirectBranch,
      ConditionalIndirectBranch,
      Exit,
      Throw
   };

   enum class ECodeEdgeKind : unsigned char
   {
      Fallthrough,
      Direct,
      Taken,
      Indirect,
      Exit
   };

   enum class ECodeProperty : unsigned short
   {
      None           = 0x00,
      Fallthrough    = 0x01,
      EndsBlock      = 0x02,
      DirectTarget   = 0x04,
      LabelTarget    = 0x08,
      IndirectTarget = 0x10,
      Terminal       = 0x20
   };

   enum class ECodeFeature : unsigned short
   {
      None            = 0x0000,
      Metadata        = 0x0001,
      State           = 0x0002,
      Integer         = 0x0004,
      FloatingPoint   = 0x0008,
      Memory          = 0x0010,
      Frame           = 0x0020,
      Object          = 0x0040,
      Allocation      = 0x0080,
      Call            = 0x0100,
      Dispatch        = 0x0200,
      ControlFlow     = 0x0400,
      Exception       = 0x0800,
      Synchronization = 0x1000,
      TLS             = 0x2000,
      Runtime         = 0x4000
   };

   inline ECodeProperty operator | (ECodeProperty left, ECodeProperty right)
   {
      return (ECodeProperty)((unsigned short)left | (unsigned short)right);
   }

   inline ECodeFeature operator | (ECodeFeature left, ECodeFeature right)
   {
      return (ECodeFeature)((unsigned short)left | (unsigned short)right);
   }

   enum class ECodeDecodeError : unsigned char
   {
      None,
      InvalidSource,
      MissingHeader,
      InvalidProcedureSize,
      EmptyProcedure,
      InvalidOpcode,
      TruncatedInstruction,
      InvalidBranchTarget,
      BranchTargetNotInstruction,
      InvalidLabelTarget,
      LabelTargetNotInstruction
   };

   struct ECodeInfo
   {
      ByteCode  code;
      ECodeFlow flow;
      ECodeProperty properties;
      ECodeFeature features;
      unsigned char operandCount;
      unsigned char encodedSize;

      bool has(ECodeProperty property) const;
      bool has(ECodeFeature feature) const;
   };

   struct ECodeInstruction
   {
      ByteCommand command;
      pos_t       offset;
      pos_t       size;
      pos_t       targetOffset;
      pos_t       labelOffset;
      pos_t       block;
      ECodeFlow   flow;
      bool        blockStart;
   };

   struct ECodeBlock
   {
      pos_t id;
      pos_t firstInstruction;
      pos_t instructionCount;
      pos_t offset;
      pos_t endOffset;
   };

   struct ECodeEdge
   {
      pos_t sourceBlock;
      pos_t targetBlock;
      ECodeEdgeKind kind;
   };

   struct ECodeLoweringContext
   {
      int frameOffset;
      int stackOffset;
      int vmtSize;
      TargetPlatform platform;
      bool alternativeMode = false;
      int dataOffset = 0;
      int dataHeader = 0;
   };

   enum class ECodeResolveError : unsigned char
   {
      None,
      InvalidArgument
   };

   class ECodeOperandResolver
   {
   public:
      static ECodeResolveError resolve(const ByteCommand& source, const RuntimeSpec& runtime,
         const ECodeLoweringContext& context, ByteCommand& command);
   };

   struct ECodeRuntimeCallSelection
   {
      RuntimeOperation operation;
      bool             verifyEncoding;
   };

   class ECodeRuntimeProvider
   {
   public:
      static ECodeRuntimeCallSelection select(const ByteCommand& command, const RuntimeSpec& runtime);
   };

   class ECodeProvider
   {
   public:
      static bool get(ByteCode code, ECodeInfo& info);
   };

   class ECodeMigrationProvider
   {
   public:
      static unsigned short requiredInlineVariants(
         ByteCode code,
         ThreadingMode threadingMode);
   };

   class ECodeProcedure
   {
      friend class ECodeDecoder;

      pos_t _sourceOffset;
      pos_t _bodyOffset;
      pos_t _bodyLength;
      CachedList<ECodeInstruction, 64> _instructions;
      CachedList<ECodeBlock, 16>       _blocks;
      CachedList<ECodeEdge, 32>        _edges;

   public:
      void clear();

      pos_t sourceOffset() const;
      pos_t bodyOffset() const;
      pos_t bodyLength() const;
      pos_t instructionCount() const;
      pos_t blockCount() const;
      pos_t edgeCount() const;

      ECodeInstruction& instruction(pos_t index);
      ECodeBlock& block(pos_t index);
      ECodeEdge& edge(pos_t index);

      ECodeProcedure();
      ECodeProcedure(const ECodeProcedure&) = delete;
      ECodeProcedure& operator =(const ECodeProcedure&) = delete;
   };

   class ECodeDecoder
   {
      static ECodeDecodeError decodeInstructions(MemoryBase* source, ECodeProcedure& procedure);
      static ECodeDecodeError markBlocks(ECodeProcedure& procedure);
      static void createBlocks(ECodeProcedure& procedure);
      static void addEdge(ECodeProcedure& procedure, pos_t sourceBlock, pos_t targetBlock, ECodeEdgeKind kind);
      static void addFallthrough(ECodeProcedure& procedure, ECodeBlock& block);
      static void createEdges(ECodeProcedure& procedure);

   public:
      static ECodeDecodeError decode(MemoryBase* source, pos_t procedureOffset, ECodeProcedure& procedure);
   };
}

#endif
