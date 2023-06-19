#include "Archs/MIPS/MipsElfRelocator.h"

#include "Commands/CAssemblerCommand.h"
#include "Core/Common.h"
#include "Parser/Parser.h"

int MipsElfRelocator::expectedMachine() const
{
	return EM_MIPS;
}

void MipsElfRelocator::processHi16Entries(uint32_t lo16Opcode, int64_t lo16RelocationBase, int symbolIndex, std::vector<RelocationAction>& actions, std::vector<std::string>& errors)
{
	auto it = hi16Entries.begin();
	while (it != hi16Entries.end())
	{
		if (it->symblIndex != symbolIndex)
		{
			++it;
			continue;
		}

		int32_t addend = (int32_t)((it->opcode & 0xFFFF) << 16) + (int16_t)(lo16Opcode & 0xFFFF);
		int64_t fullPosition = addend + it->relocationBase;
		uint32_t opcode = (it->opcode & 0xffff0000) | (((fullPosition >> 16) + ((fullPosition & 0x8000) != 0)) & 0xFFFF);
		actions.emplace_back(it->offset, opcode);

		it = hi16Entries.erase(it);
	}
}

bool MipsElfRelocator::relocateOpcode(int type, const RelocationData& data, std::vector<RelocationAction>& actions, std::vector<std::string>& errors)
{
	unsigned int op = data.opcode;
	bool result = true;

	switch (type)
	{
	case R_MIPS_26: //j, jal
		op = (op & 0xFC000000) | (((op&0x03FFFFFF)+(data.relocationBase>>2))&0x03FFFFFF);
		break;
	case R_MIPS_32:
		op += (int) data.relocationBase;
		break;
	case R_MIPS_HI16:
		hi16Entries.push_back({data.opcodeOffset, data.relocationBase, data.opcode, data.symbolIndex});
		break;
	case R_MIPS_LO16:
		processHi16Entries(op, data.relocationBase, data.symbolIndex, actions, errors);
		op = (op&0xffff0000) | (((op&0xffff)+data.relocationBase)&0xffff);
		break;
	default:
		errors.emplace_back(tfm::format("Unknown MIPS relocation type %d",type));
		return false;
	}

	actions.emplace_back(data.opcodeOffset, op);
	return result;
}

bool MipsElfRelocator::finish(std::vector<RelocationAction>& actions, std::vector<std::string>& errors)
{
	// This shouldn't happen
	if (!hi16Entries.empty())
	{
		errors.emplace_back(tfm::format("Unmatched R_MIPS_HI16 relocation entries"));
		return false;
	}
	return true;
}

void MipsElfRelocator::setSymbolAddress(RelocationData& data, int64_t symbolAddress, int symbolType)
{
	data.symbolAddress = symbolAddress;
	data.targetSymbolType = symbolType;
}

const char* mipsCtorTemplate = R"(
	addiu	sp,-32
	sw		ra,0(sp)
	sw		s0,4(sp)
	sw		s1,8(sp)
	sw		s2,12(sp)
	sw		s3,16(sp)
	li		s0,%ctorTable%
	li		s1,%ctorTable%+%ctorTableSize%
	%outerLoopLabel%:
	lw		s2,(s0)
	lw		s3,4(s0)
	addiu	s0,8
	%innerLoopLabel%:
	lw		a0,(s2)
	jalr	a0
	addiu	s2,4h
	bne		s2,s3,%innerLoopLabel%
	nop
	bne		s0,s1,%outerLoopLabel%
	nop
	lw		ra,0(sp)
	lw		s0,4(sp)
	lw		s1,8(sp)
	lw		s2,12(sp)
	lw		s3,16(sp)
	jr		ra
	addiu	sp,32
	%ctorTable%:
	.word	%ctorContent%
)";

std::unique_ptr<CAssemblerCommand> MipsElfRelocator::generateCtorStub(std::vector<ElfRelocatorCtor>& ctors)
{
	Parser parser;
	if (ctors.size() != 0)
	{
		// create constructor table
		std::string table;
		for (size_t i = 0; i < ctors.size(); i++)
		{
			if (i != 0)
				table += ',';
			table += tfm::format("%s,%s+0x%08X",ctors[i].symbolName,ctors[i].symbolName,ctors[i].size);
		}

		return parser.parseTemplate(mipsCtorTemplate,{
			{ "%ctorTable%",		Global.symbolTable.getUniqueLabelName().string() },
			{ "%ctorTableSize%",	tfm::format("%d",ctors.size()*8) },
			{ "%outerLoopLabel%",	Global.symbolTable.getUniqueLabelName().string() },
			{ "%innerLoopLabel%",	Global.symbolTable.getUniqueLabelName().string() },
			{ "%ctorContent%",		table },
		});
	} else {
		return parser.parseTemplate("jr ra :: nop");
	}
}
