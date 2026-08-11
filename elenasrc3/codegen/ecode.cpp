#include "ecode.h"

using namespace elena_lang;
using namespace elena_lang::codegen;

static bool scaleIndex(long long value, int scale, int& result)
{
   long long scaled = value * scale;
   if (scaled < -0x80000000LL || scaled > 0x7FFFFFFFLL)
      return false;

   result = (int)scaled;

   return true;
}

static int frameDisplacement(int argument, int dataOffset)
{
   return -(argument - (argument < 0 ? dataOffset : 0));
}

ECodeResolveError ECodeOperandResolver :: resolve(const ByteCommand& source, const RuntimeSpec& runtime,
   const ECodeLoweringContext& context, ByteCommand& command)
{
   command = source;

   switch (source.code) {
      case ByteCode::SetDP:
      case ByteCode::LoadDP:
      case ByteCode::XCmpDP:
      case ByteCode::XAddDP:
      case ByteCode::SaveDP:
      case ByteCode::LSaveDP:
      case ByteCode::LLoadDP:
         command.arg1 = frameDisplacement(source.arg1, runtime.objectLayout.fieldSize);
         break;

      case ByteCode::SetFP:
      case ByteCode::XSetFP:
      case ByteCode::StoreFI:
      case ByteCode::PeekFI:
      case ByteCode::XLoadArgFI:
      {
         int scaled = 0;
         if (!scaleIndex(source.arg1, runtime.objectLayout.fieldSize, scaled))
            return ECodeResolveError::InvalidArgument;

         command.arg1 = frameDisplacement(scaled, context.frameOffset);
         break;
      }

      case ByteCode::SaveSI:
      case ByteCode::StoreSI:
      case ByteCode::XFlushSI:
      case ByteCode::XRefreshSI:
      case ByteCode::PeekSI:
      case ByteCode::LLoadSI:
      case ByteCode::LoadSI:
         command.arg2 = source.arg1;
         if (!scaleIndex((long long)source.arg1 + context.stackOffset,
            runtime.objectLayout.fieldSize, command.arg1))
         {
            return ECodeResolveError::InvalidArgument;
         }
         break;

      case ByteCode::GetI:
      case ByteCode::XStoreI:
         if (!scaleIndex(source.arg1, runtime.objectLayout.fieldSize, command.arg1))
            return ECodeResolveError::InvalidArgument;

         if (source.arg1 < 0) {
            long long displacement = (long long)command.arg1 - context.vmtSize;
            if (displacement < -0x80000000LL || displacement > 0x7FFFFFFFLL)
               return ECodeResolveError::InvalidArgument;

            command.arg1 = (int)displacement;
         }
         break;

      case ByteCode::XAssignI:
         if (!scaleIndex(source.arg1, runtime.objectLayout.fieldSize, command.arg1))
            return ECodeResolveError::InvalidArgument;
         break;

      case ByteCode::SetSP:
         if (!scaleIndex((long long)source.arg1 + context.stackOffset,
            runtime.objectLayout.fieldSize, command.arg1))
         {
            return ECodeResolveError::InvalidArgument;
         }
         break;

      default:
         break;
   }

   return ECodeResolveError::None;
}

ECodeRuntimeCallSelection ECodeRuntimeProvider :: select(
   const ByteCommand& command, const RuntimeSpec& runtime)
{
   if (command.code == ByteCode::System) {
      if (command.arg1 == 1 || command.arg1 == 2)
         return { RuntimeOperation::Collect, true };
      if (command.arg1 == 4)
         return { RuntimeOperation::Prepare, true };
      if ((command.arg1 == 8 || command.arg1 == 9)
         && runtime.threadingMode == ThreadingMode::MultiThread)
      {
         return { RuntimeOperation::WaitForGC, true };
      }
   }

   if (command.code == ByteCode::Exclude && runtime.threadingMode == ThreadingMode::MultiThread)
      return { RuntimeOperation::WaitForGC, true };

   if (command.code == ByteCode::XCreateR)
      return { RuntimeOperation::AllocatePermanent, true };

   if (command.code == ByteCode::NewIR || command.code == ByteCode::NewNR
      || command.code == ByteCode::CreateR || command.code == ByteCode::CreateNR)
   {
      return { RuntimeOperation::AllocateYoung, true };
   }

   return { RuntimeOperation::AllocateYoung, false };
}

bool ECodeInfo :: has(ECodeProperty property) const
{
   return ((unsigned short)properties & (unsigned short)property) == (unsigned short)property;
}

bool ECodeInfo :: has(ECodeFeature feature) const
{
   return ((unsigned short)features & (unsigned short)feature) == (unsigned short)feature;
}

static constexpr unsigned short inlineVariant(unsigned int index)
{
   return (unsigned short)(1u << index);
}

unsigned short ECodeMigrationProvider :: requiredInlineVariants(
   ByteCode code,
   ThreadingMode threadingMode)
{
   if (threadingMode != ThreadingMode::MultiThread)
      return 0;

   switch (code) {
      case ByteCode::SNop:
      case ByteCode::Throw:
      case ByteCode::Unhook:
      case ByteCode::Exclude:
      case ByteCode::Include:
      case ByteCode::TstStck:
      case ByteCode::TryLock:
      case ByteCode::FreeLock:
      case ByteCode::PeekTLS:
      case ByteCode::StoreTLS:
      case ByteCode::XHookDPR:
         return inlineVariant(0);

      case ByteCode::ExtCloseN:
         return inlineVariant(0) | inlineVariant(1);

      case ByteCode::System:
         return inlineVariant(1)
            | inlineVariant(2)
            | inlineVariant(3)
            | inlineVariant(4)
            | inlineVariant(6)
            | inlineVariant(7)
            | inlineVariant(8)
            | inlineVariant(9);

      case ByteCode::ExtOpenIN:
         return inlineVariant(0)
            | inlineVariant(1)
            | inlineVariant(2)
            | inlineVariant(3)
            | inlineVariant(4)
            | inlineVariant(5)
            | inlineVariant(6)
            | inlineVariant(7)
            | inlineVariant(8)
            | inlineVariant(9)
            | inlineVariant(10)
            | inlineVariant(11);

      default:
         return 0;
   }
}

static bool isBetween(ByteCode code, ByteCode first, ByteCode last)
{
   return (unsigned int)code >= (unsigned int)first
      && (unsigned int)code <= (unsigned int)last;
}

static bool isValidOpcode(ByteCode code)
{
   return isBetween(code, ByteCode::Nop, ByteCode::LFSave)
      || isBetween(code, ByteCode::FIAdd, ByteCode::FIDiv)
      || isBetween(code, ByteCode::Shl, ByteCode::XLAddDP)
      || isBetween(code, ByteCode::CmpR, ByteCode::XCmpSI)
      || isBetween(code, ByteCode::CmpFI, ByteCode::CallExtR);
}

static ECodeFeature getFeatures(ByteCode code)
{
   switch (code) {
      case ByteCode::Nop:
      case ByteCode::Breakpoint:
      case ByteCode::SNop:
      case ByteCode::XNop:
         return ECodeFeature::Metadata;
      case ByteCode::Redirect:
      case ByteCode::XRedirectM:
      case ByteCode::XDispatchMR:
      case ByteCode::DispatchMR:
         return ECodeFeature::Dispatch | ECodeFeature::ControlFlow;
      case ByteCode::Quit:
      case ByteCode::XJump:
      case ByteCode::XQuit:
      case ByteCode::Jump:
      case ByteCode::Jeq:
      case ByteCode::Jne:
      case ByteCode::Jlt:
      case ByteCode::Jge:
      case ByteCode::Jgr:
      case ByteCode::Jle:
         return ECodeFeature::ControlFlow;
      case ByteCode::JumpVI:
      case ByteCode::VJumpMR:
      case ByteCode::JumpMR:
         return ECodeFeature::Dispatch | ECodeFeature::ControlFlow;
      case ByteCode::Throw:
      case ByteCode::Unhook:
      case ByteCode::XHookDPR:
      case ByteCode::XLabelDPR:
         return ECodeFeature::Exception;
      case ByteCode::Exclude:
      case ByteCode::Include:
      case ByteCode::TryLock:
      case ByteCode::FreeLock:
         return ECodeFeature::Synchronization | ECodeFeature::Runtime;
      case ByteCode::PeekTLS:
      case ByteCode::StoreTLS:
         return ECodeFeature::TLS | ECodeFeature::Memory;
      case ByteCode::XCall:
      case ByteCode::CallR:
      case ByteCode::CallVI:
      case ByteCode::CallMR:
      case ByteCode::CallExtR:
         return ECodeFeature::Call;
      case ByteCode::VCallMR:
         return ECodeFeature::Call | ECodeFeature::Dispatch;
      case ByteCode::MovEnv:
      case ByteCode::TstStck:
      case ByteCode::System:
         return ECodeFeature::Runtime;
      case ByteCode::DAlloc:
      case ByteCode::DFree:
      case ByteCode::CreateR:
      case ByteCode::AllocI:
      case ByteCode::FreeI:
      case ByteCode::XCreateR:
      case ByteCode::XNewNR:
      case ByteCode::NewIR:
      case ByteCode::NewNR:
      case ByteCode::CreateNR:
      case ByteCode::FillIR:
      case ByteCode::XFillR:
         return ECodeFeature::Allocation | ECodeFeature::Object;
      case ByteCode::Len:
      case ByteCode::Class:
      case ByteCode::MLen:
      case ByteCode::DTrans:
      case ByteCode::Coalesce:
      case ByteCode::Parent:
      case ByteCode::NLen:
         return ECodeFeature::Object;
      case ByteCode::FIAdd:
      case ByteCode::FISub:
      case ByteCode::FIMul:
      case ByteCode::FIDiv:
      case ByteCode::FAbsDP:
      case ByteCode::FSqrtDP:
      case ByteCode::FExpDP:
      case ByteCode::FLnDP:
      case ByteCode::FSinDP:
      case ByteCode::FCosDP:
      case ByteCode::FArctanDP:
      case ByteCode::FPiDP:
      case ByteCode::NConvFDP:
      case ByteCode::FTruncDP:
      case ByteCode::XAddDP:
      case ByteCode::FRoundDP:
      case ByteCode::FCmpN:
      case ByteCode::FAddDPN:
      case ByteCode::FSubDPN:
      case ByteCode::FMulDPN:
      case ByteCode::FDivDPN:
      case ByteCode::XLAddDP:
         return ECodeFeature::FloatingPoint;
      case ByteCode::FSave:
      case ByteCode::XFSave:
      case ByteCode::LFSave:
      case ByteCode::StoreFI:
      case ByteCode::PeekFI:
      case ByteCode::CmpFI:
      case ByteCode::XLoadArgFI:
      case ByteCode::XStoreFIR:
         return ECodeFeature::FloatingPoint | ECodeFeature::Memory;
      case ByteCode::MovFrm:
      case ByteCode::SetDP:
      case ByteCode::LoadDP:
      case ByteCode::SetFP:
      case ByteCode::XSetFP:
      case ByteCode::SetSP:
      case ByteCode::CloseN:
      case ByteCode::ExtCloseN:
      case ByteCode::XOpenIN:
      case ByteCode::OpenIN:
      case ByteCode::ExtOpenIN:
         return ECodeFeature::Frame;
      case ByteCode::SaveDP:
      case ByteCode::SaveSI:
      case ByteCode::StoreSI:
      case ByteCode::XFlushSI:
      case ByteCode::XRefreshSI:
      case ByteCode::PeekSI:
      case ByteCode::LSaveDP:
      case ByteCode::LSaveSI:
      case ByteCode::LLoadDP:
      case ByteCode::XSwapSI:
      case ByteCode::SwapSI:
      case ByteCode::XCmpSI:
      case ByteCode::CmpSI:
      case ByteCode::LLoadSI:
      case ByteCode::LoadSI:
      case ByteCode::XStoreSIR:
      case ByteCode::MovSIFI:
      case ByteCode::XMovSISI:
         return ECodeFeature::Frame | ECodeFeature::Memory;
      case ByteCode::Not:
      case ByteCode::Neg:
      case ByteCode::LNeg:
      case ByteCode::Shl:
      case ByteCode::Shr:
      case ByteCode::XCmpDP:
      case ByteCode::SubN:
      case ByteCode::AddN:
      case ByteCode::AndN:
      case ByteCode::CmpN:
      case ByteCode::OrN:
      case ByteCode::MulN:
      case ByteCode::ICmpN:
      case ByteCode::TstFlag:
      case ByteCode::TstN:
      case ByteCode::TstM:
      case ByteCode::IAndDPN:
      case ByteCode::IOrDPN:
      case ByteCode::IXorDPN:
      case ByteCode::INotDPN:
      case ByteCode::IShlDPN:
      case ByteCode::IShrDPN:
      case ByteCode::UDivDPN:
      case ByteCode::SelGrRR:
      case ByteCode::SelULtRR:
      case ByteCode::IAddDPN:
      case ByteCode::ISubDPN:
      case ByteCode::IMulDPN:
      case ByteCode::IDivDPN:
      case ByteCode::NAddDPN:
      case ByteCode::SelEqRR:
      case ByteCode::SelLtRR:
      case ByteCode::XCmp:
      case ByteCode::XLCmp:
      case ByteCode::XPeekEq:
      case ByteCode::CmpR:
         return ECodeFeature::Integer;
      case ByteCode::Load:
      case ByteCode::Save:
      case ByteCode::LoadV:
      case ByteCode::BLoad:
      case ByteCode::WLoad:
      case ByteCode::Assign:
      case ByteCode::LoadS:
      case ByteCode::XAssign:
      case ByteCode::LLoad:
      case ByteCode::LSave:
      case ByteCode::XLoad:
      case ByteCode::XLLoad:
      case ByteCode::BRead:
      case ByteCode::WRead:
      case ByteCode::BCopy:
      case ByteCode::WCopy:
      case ByteCode::XGet:
      case ByteCode::LoadZ:
      case ByteCode::WLoadZ:
      case ByteCode::XSaveN:
      case ByteCode::SetR:
      case ByteCode::XAssignI:
      case ByteCode::PeekR:
      case ByteCode::StoreR:
      case ByteCode::MovM:
      case ByteCode::MovN:
      case ByteCode::Copy:
      case ByteCode::ReadN:
      case ByteCode::WriteN:
      case ByteCode::DCopy:
      case ByteCode::GetI:
      case ByteCode::AssignI:
      case ByteCode::XStoreI:
      case ByteCode::XSaveDispN:
      case ByteCode::CopyDPN:
      case ByteCode::NSaveDPN:
      case ByteCode::DCopyDPN:
      case ByteCode::XWriteON:
      case ByteCode::XCopyON:
         return ECodeFeature::Memory;
      case ByteCode::ConvL:
      case ByteCode::AltMode:
         return ECodeFeature::State;
      default:
         return ECodeFeature::None;
   }
}

bool ECodeProvider :: get(ByteCode code, ECodeInfo& info)
{
   if (!isValidOpcode(code))
      return false;

   ECodeFeature features = getFeatures(code);
   if (features == ECodeFeature::None)
      return false;

   info = {
      .code = code,
      .flow = ECodeFlow::Next,
      .properties = ECodeProperty::Fallthrough,
      .features = features,
      .operandCount = 0,
      .encodedSize = 1
   };

   unsigned int value = (unsigned int)code;

   if (value > (unsigned int)ByteCode::MaxDoubleOp) {
      info.operandCount = 2;
      info.encodedSize = 1 + sizeof(arg_t) * 2;
   }
   else if (value > (unsigned int)ByteCode::MaxSingleOp) {
      info.operandCount = 1;
      info.encodedSize = 1 + sizeof(arg_t);
   }

   switch (code) {
      case ByteCode::Jump:
         info.flow = ECodeFlow::DirectBranch;
         info.properties = ECodeProperty::EndsBlock | ECodeProperty::DirectTarget;
         break;
      case ByteCode::Jeq:
      case ByteCode::Jne:
      case ByteCode::Jlt:
      case ByteCode::Jle:
      case ByteCode::Jge:
      case ByteCode::Jgr:
         info.flow = ECodeFlow::ConditionalBranch;
         info.properties = ECodeProperty::Fallthrough | ECodeProperty::EndsBlock
            | ECodeProperty::DirectTarget;
         break;
      case ByteCode::XJump:
      case ByteCode::JumpVI:
      case ByteCode::VJumpMR:
      case ByteCode::JumpMR:
         info.flow = ECodeFlow::IndirectBranch;
         info.properties = ECodeProperty::EndsBlock | ECodeProperty::IndirectTarget;
         break;
      case ByteCode::Redirect:
      case ByteCode::XRedirectM:
      case ByteCode::XDispatchMR:
      case ByteCode::DispatchMR:
         info.flow = ECodeFlow::ConditionalIndirectBranch;
         info.properties = ECodeProperty::Fallthrough | ECodeProperty::EndsBlock
            | ECodeProperty::IndirectTarget;
         break;
      case ByteCode::Quit:
      case ByteCode::XQuit:
         info.flow = ECodeFlow::Exit;
         info.properties = ECodeProperty::EndsBlock | ECodeProperty::Terminal;
         break;
      case ByteCode::Throw:
         info.flow = ECodeFlow::Throw;
         info.properties = ECodeProperty::EndsBlock | ECodeProperty::Terminal;
         break;
      case ByteCode::XHookDPR:
      case ByteCode::XLabelDPR:
         info.properties = info.properties | ECodeProperty::LabelTarget;
         break;
      default:
         break;
   }

   return true;
}

ECodeProcedure :: ECodeProcedure()
   : _sourceOffset(0), _bodyOffset(0), _bodyLength(0)
{
}

void ECodeProcedure :: clear()
{
   _sourceOffset = 0;
   _bodyOffset = 0;
   _bodyLength = 0;
   _instructions.clear();
   _blocks.clear();
   _edges.clear();
}

pos_t ECodeProcedure :: sourceOffset() const
{
   return _sourceOffset;
}

pos_t ECodeProcedure :: bodyOffset() const
{
   return _bodyOffset;
}

pos_t ECodeProcedure :: bodyLength() const
{
   return _bodyLength;
}

pos_t ECodeProcedure :: instructionCount() const
{
   return _instructions.count_pos();
}

pos_t ECodeProcedure :: blockCount() const
{
   return _blocks.count_pos();
}

pos_t ECodeProcedure :: edgeCount() const
{
   return _edges.count_pos();
}

ECodeInstruction& ECodeProcedure :: instruction(pos_t index)
{
   return _instructions.get(index);
}

ECodeBlock& ECodeProcedure :: block(pos_t index)
{
   return _blocks.get(index);
}

ECodeEdge& ECodeProcedure :: edge(pos_t index)
{
   return _edges.get(index);
}

static pos_t findInstruction(ECodeProcedure& procedure, pos_t offset)
{
   for (pos_t i = 0; i < procedure.instructionCount(); i++) {
      if (procedure.instruction(i).offset == offset)
         return i;
   }

   return INVALID_POS;
}

static pos_t findBlock(ECodeProcedure& procedure, pos_t offset)
{
   for (pos_t i = 0; i < procedure.blockCount(); i++) {
      if (procedure.block(i).offset == offset)
         return i;
   }

   return INVALID_POS;
}

ECodeDecodeError ECodeDecoder :: decodeInstructions(MemoryBase* source, ECodeProcedure& procedure)
{
   pos_t cursor = procedure._bodyOffset;
   pos_t end = cursor + procedure._bodyLength;

   while (cursor < end) {
      unsigned char rawCode = 0;
      if (!source->read(cursor, &rawCode, sizeof(rawCode)))
         return ECodeDecodeError::TruncatedInstruction;

      ECodeInfo info = {};
      if (!ECodeProvider::get((ByteCode)rawCode, info))
         return ECodeDecodeError::InvalidOpcode;

      if (info.encodedSize > end - cursor)
         return ECodeDecodeError::TruncatedInstruction;

      ECodeInstruction instruction = {
         .command = { info.code, 0, 0 },
         .offset = cursor - procedure._bodyOffset,
         .size = info.encodedSize,
         .targetOffset = INVALID_POS,
         .labelOffset = INVALID_POS,
         .block = INVALID_POS,
         .flow = info.flow,
         .blockStart = procedure._instructions.count() == 0
      };

      if (info.operandCount > 0
         && !source->read(cursor + 1, &instruction.command.arg1, sizeof(arg_t)))
      {
         return ECodeDecodeError::TruncatedInstruction;
      }
      if (info.operandCount > 1
         && !source->read(cursor + 1 + sizeof(arg_t), &instruction.command.arg2, sizeof(arg_t)))
      {
         return ECodeDecodeError::TruncatedInstruction;
      }

      if (info.has(ECodeProperty::DirectTarget)) {
         int64_t target = (int64_t)instruction.offset + instruction.size
            + instruction.command.arg1;
         if (target < 0 || target >= procedure._bodyLength)
            return ECodeDecodeError::InvalidBranchTarget;

         instruction.targetOffset = (pos_t)target;
      }

      if (info.has(ECodeProperty::LabelTarget)
         && ((ref_t)instruction.command.arg2 & mskAnyRef) == mskLabelRef)
      {
         pos_t target = (ref_t)instruction.command.arg2 & ~mskAnyRef;
         if (target < procedure._bodyOffset || target >= end)
            return ECodeDecodeError::InvalidLabelTarget;

         instruction.labelOffset = target - procedure._bodyOffset;
      }

      procedure._instructions.add(instruction);
      cursor += info.encodedSize;
   }

   return ECodeDecodeError::None;
}

ECodeDecodeError ECodeDecoder :: markBlocks(ECodeProcedure& procedure)
{
   for (pos_t i = 0; i < procedure.instructionCount(); i++) {
      ECodeInstruction& instruction = procedure.instruction(i);
      ECodeInfo info = {};
      ECodeProvider::get(instruction.command.code, info);

      if (instruction.targetOffset != INVALID_POS) {
         pos_t target = findInstruction(procedure, instruction.targetOffset);
         if (target == INVALID_POS)
            return ECodeDecodeError::BranchTargetNotInstruction;

         procedure.instruction(target).blockStart = true;
      }
      if (instruction.labelOffset != INVALID_POS) {
         pos_t target = findInstruction(procedure, instruction.labelOffset);
         if (target == INVALID_POS)
            return ECodeDecodeError::LabelTargetNotInstruction;

         procedure.instruction(target).blockStart = true;
      }
      if (info.has(ECodeProperty::EndsBlock) && i + 1 < procedure.instructionCount())
         procedure.instruction(i + 1).blockStart = true;
   }

   return ECodeDecodeError::None;
}

void ECodeDecoder :: createBlocks(ECodeProcedure& procedure)
{
   pos_t first = 0;
   for (pos_t i = 1; i <= procedure.instructionCount(); i++) {
      if (i == procedure.instructionCount() || procedure.instruction(i).blockStart) {
         ECodeInstruction& firstInstruction = procedure.instruction(first);
         ECodeInstruction& lastInstruction = procedure.instruction(i - 1);
         pos_t blockId = procedure._blocks.count_pos();

         procedure._blocks.add({
            .id = blockId,
            .firstInstruction = first,
            .instructionCount = i - first,
            .offset = firstInstruction.offset,
            .endOffset = lastInstruction.offset + lastInstruction.size
         });

         for (pos_t j = first; j < i; j++)
            procedure.instruction(j).block = blockId;

         first = i;
      }
   }
}

void ECodeDecoder :: addEdge(ECodeProcedure& procedure, pos_t sourceBlock, pos_t targetBlock, ECodeEdgeKind kind)
{
   procedure._edges.add({
      .sourceBlock = sourceBlock,
      .targetBlock = targetBlock,
      .kind = kind
   });
}

void ECodeDecoder :: addFallthrough(ECodeProcedure& procedure, ECodeBlock& block)
{
   pos_t target = findBlock(procedure, block.endOffset);

   addEdge(
      procedure,
      block.id,
      target,
      target == INVALID_POS ? ECodeEdgeKind::Exit : ECodeEdgeKind::Fallthrough);
}

void ECodeDecoder :: createEdges(ECodeProcedure& procedure)
{
   for (pos_t i = 0; i < procedure.blockCount(); i++) {
      ECodeBlock& block = procedure.block(i);
      ECodeInstruction& instruction = procedure.instruction(
         block.firstInstruction + block.instructionCount - 1);

      switch (instruction.flow) {
         case ECodeFlow::Next:
            addFallthrough(procedure, block);
            break;

         case ECodeFlow::DirectBranch:
            addEdge(procedure, block.id, findBlock(procedure, instruction.targetOffset), ECodeEdgeKind::Direct);
            break;

         case ECodeFlow::ConditionalBranch:
            addEdge(procedure, block.id, findBlock(procedure, instruction.targetOffset), ECodeEdgeKind::Taken);
            addFallthrough(procedure, block);
            break;

         case ECodeFlow::IndirectBranch:
            addEdge(procedure, block.id, INVALID_POS, ECodeEdgeKind::Indirect);
            break;

         case ECodeFlow::ConditionalIndirectBranch:
            addEdge(procedure, block.id, INVALID_POS, ECodeEdgeKind::Indirect);
            addFallthrough(procedure, block);
            break;

         case ECodeFlow::Exit:
         case ECodeFlow::Throw:
            break;
      }
   }
}

ECodeDecodeError ECodeDecoder :: decode(MemoryBase* source, pos_t procedureOffset, ECodeProcedure& procedure)
{
   procedure.clear();

   if (!source)
      return ECodeDecodeError::InvalidSource;

   if (procedureOffset > source->length()
      || source->length() - procedureOffset < sizeof(pos_t))
   {
      return ECodeDecodeError::MissingHeader;
   }

   pos_t bodyLength = 0;
   if (!source->read(procedureOffset, &bodyLength, sizeof(bodyLength)))
      return ECodeDecodeError::MissingHeader;

   pos_t bodyOffset = procedureOffset + sizeof(pos_t);
   if (bodyLength > source->length() - bodyOffset)
      return ECodeDecodeError::InvalidProcedureSize;
   if (bodyLength == 0)
      return ECodeDecodeError::EmptyProcedure;

   procedure._sourceOffset = procedureOffset;
   procedure._bodyOffset = bodyOffset;
   procedure._bodyLength = bodyLength;

   ECodeDecodeError error = decodeInstructions(source, procedure);
   if (error != ECodeDecodeError::None)
      return error;

   error = markBlocks(procedure);
   if (error != ECodeDecodeError::None)
      return error;

   createBlocks(procedure);
   createEdges(procedure);

   return ECodeDecodeError::None;
}
