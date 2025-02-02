//===- ELFObjectFileTest.cpp - Tests for ELFObjectFile --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

using namespace llvm;
using namespace llvm::object;

namespace {

// A struct to initialize a buffer to represent an ELF object file.
struct DataForTest {
  std::vector<uint8_t> Data;

  template <typename T>
  std::vector<uint8_t> makeElfData(uint8_t Class, uint8_t Encoding,
                                   uint16_t Machine) {
    T Ehdr{}; // Zero-initialise the header.
    Ehdr.e_ident[ELF::EI_MAG0] = 0x7f;
    Ehdr.e_ident[ELF::EI_MAG1] = 'E';
    Ehdr.e_ident[ELF::EI_MAG2] = 'L';
    Ehdr.e_ident[ELF::EI_MAG3] = 'F';
    Ehdr.e_ident[ELF::EI_CLASS] = Class;
    Ehdr.e_ident[ELF::EI_DATA] = Encoding;
    Ehdr.e_ident[ELF::EI_VERSION] = 1;
    Ehdr.e_type = ELF::ET_REL;
    Ehdr.e_machine = Machine;
    Ehdr.e_version = 1;
    Ehdr.e_ehsize = sizeof(T);

    bool IsLittleEndian = Encoding == ELF::ELFDATA2LSB;
    if (sys::IsLittleEndianHost != IsLittleEndian) {
      sys::swapByteOrder(Ehdr.e_type);
      sys::swapByteOrder(Ehdr.e_machine);
      sys::swapByteOrder(Ehdr.e_version);
      sys::swapByteOrder(Ehdr.e_ehsize);
    }

    uint8_t *EhdrBytes = reinterpret_cast<uint8_t *>(&Ehdr);
    std::vector<uint8_t> Bytes;
    std::copy(EhdrBytes, EhdrBytes + sizeof(Ehdr), std::back_inserter(Bytes));
    return Bytes;
  }

  DataForTest(uint8_t Class, uint8_t Encoding, uint16_t Machine) {
    if (Class == ELF::ELFCLASS64)
      Data = makeElfData<ELF::Elf64_Ehdr>(Class, Encoding, Machine);
    else {
      assert(Class == ELF::ELFCLASS32);
      Data = makeElfData<ELF::Elf32_Ehdr>(Class, Encoding, Machine);
    }
  }
};

void checkFormatAndArch(const DataForTest &D, StringRef Fmt,
                        Triple::ArchType Arch) {
  Expected<std::unique_ptr<ObjectFile>> ELFObjOrErr =
      object::ObjectFile::createELFObjectFile(
          MemoryBufferRef(toStringRef(D.Data), "dummyELF"));
  ASSERT_THAT_EXPECTED(ELFObjOrErr, Succeeded());

  const ObjectFile &File = *(*ELFObjOrErr).get();
  EXPECT_EQ(Fmt, File.getFileFormatName());
  EXPECT_EQ(Arch, File.getArch());
}

std::array<DataForTest, 4> generateData(uint16_t Machine) {
  return {DataForTest(ELF::ELFCLASS32, ELF::ELFDATA2LSB, Machine),
          DataForTest(ELF::ELFCLASS32, ELF::ELFDATA2MSB, Machine),
          DataForTest(ELF::ELFCLASS64, ELF::ELFDATA2LSB, Machine),
          DataForTest(ELF::ELFCLASS64, ELF::ELFDATA2MSB, Machine)};
}

} // namespace

TEST(ELFObjectFileTest, MachineTestForNoneOrUnused) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_NONE))
    checkFormatAndArch(D, Formats[I++], Triple::UnknownArch);

  // Test an arbitrary unused EM_* value (255).
  I = 0;
  for (const DataForTest &D : generateData(255))
    checkFormatAndArch(D, Formats[I++], Triple::UnknownArch);
}

TEST(ELFObjectFileTest, MachineTestForVE) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-ve", "elf64-ve"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_VE))
    checkFormatAndArch(D, Formats[I++], Triple::ve);
}

TEST(ELFObjectFileTest, MachineTestForX86_64) {
  std::array<StringRef, 4> Formats = {"elf32-x86-64", "elf32-x86-64",
                                      "elf64-x86-64", "elf64-x86-64"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_X86_64))
    checkFormatAndArch(D, Formats[I++], Triple::x86_64);
}

TEST(ELFObjectFileTest, MachineTestFor386) {
  std::array<StringRef, 4> Formats = {"elf32-i386", "elf32-i386", "elf64-i386",
                                      "elf64-i386"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_386))
    checkFormatAndArch(D, Formats[I++], Triple::x86);
}

TEST(ELFObjectFileTest, MachineTestForMIPS) {
  std::array<StringRef, 4> Formats = {"elf32-mips", "elf32-mips", "elf64-mips",
                                      "elf64-mips"};
  std::array<Triple::ArchType, 4> Archs = {Triple::mipsel, Triple::mips,
                                           Triple::mips64el, Triple::mips64};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_MIPS)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForAMDGPU) {
  std::array<StringRef, 4> Formats = {"elf32-amdgpu", "elf32-amdgpu",
                                      "elf64-amdgpu", "elf64-amdgpu"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_AMDGPU))
    checkFormatAndArch(D, Formats[I++], Triple::UnknownArch);
}

TEST(ELFObjectFileTest, MachineTestForIAMCU) {
  std::array<StringRef, 4> Formats = {"elf32-iamcu", "elf32-iamcu",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_IAMCU))
    checkFormatAndArch(D, Formats[I++], Triple::x86);
}

TEST(ELFObjectFileTest, MachineTestForAARCH64) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-littleaarch64",
                                      "elf64-bigaarch64"};
  std::array<Triple::ArchType, 4> Archs = {Triple::aarch64, Triple::aarch64_be,
                                           Triple::aarch64, Triple::aarch64_be};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_AARCH64)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForPPC64) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-powerpcle", "elf64-powerpc"};
  std::array<Triple::ArchType, 4> Archs = {Triple::ppc64le, Triple::ppc64,
                                           Triple::ppc64le, Triple::ppc64};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_PPC64)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForPPC) {
  std::array<StringRef, 4> Formats = {"elf32-powerpc", "elf32-powerpc",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_PPC))
    checkFormatAndArch(D, Formats[I++], Triple::ppc);
}

TEST(ELFObjectFileTest, MachineTestForRISCV) {
  std::array<StringRef, 4> Formats = {"elf32-littleriscv", "elf32-littleriscv",
                                      "elf64-littleriscv", "elf64-littleriscv"};
  std::array<Triple::ArchType, 4> Archs = {Triple::riscv32, Triple::riscv32,
                                           Triple::riscv64, Triple::riscv64};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_RISCV)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForARM) {
  std::array<StringRef, 4> Formats = {"elf32-littlearm", "elf32-bigarm",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_ARM))
    checkFormatAndArch(D, Formats[I++], Triple::arm);
}

TEST(ELFObjectFileTest, MachineTestForS390) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-s390", "elf64-s390"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_S390))
    checkFormatAndArch(D, Formats[I++], Triple::systemz);
}

TEST(ELFObjectFileTest, MachineTestForSPARCV9) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-sparc", "elf64-sparc"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_SPARCV9))
    checkFormatAndArch(D, Formats[I++], Triple::sparcv9);
}

TEST(ELFObjectFileTest, MachineTestForSPARC) {
  std::array<StringRef, 4> Formats = {"elf32-sparc", "elf32-sparc",
                                      "elf64-unknown", "elf64-unknown"};
  std::array<Triple::ArchType, 4> Archs = {Triple::sparcel, Triple::sparc,
                                           Triple::sparcel, Triple::sparc};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_SPARC)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForSPARC32PLUS) {
  std::array<StringRef, 4> Formats = {"elf32-sparc", "elf32-sparc",
                                      "elf64-unknown", "elf64-unknown"};
  std::array<Triple::ArchType, 4> Archs = {Triple::sparcel, Triple::sparc,
                                           Triple::sparcel, Triple::sparc};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_SPARC32PLUS)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForBPF) {
  std::array<StringRef, 4> Formats = {"elf32-unknown", "elf32-unknown",
                                      "elf64-bpf", "elf64-bpf"};
  std::array<Triple::ArchType, 4> Archs = {Triple::bpfel, Triple::bpfeb,
                                           Triple::bpfel, Triple::bpfeb};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_BPF)) {
    checkFormatAndArch(D, Formats[I], Archs[I]);
    ++I;
  }
}

TEST(ELFObjectFileTest, MachineTestForAVR) {
  std::array<StringRef, 4> Formats = {"elf32-avr", "elf32-avr", "elf64-unknown",
                                      "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_AVR))
    checkFormatAndArch(D, Formats[I++], Triple::avr);
}

TEST(ELFObjectFileTest, MachineTestForHEXAGON) {
  std::array<StringRef, 4> Formats = {"elf32-hexagon", "elf32-hexagon",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_HEXAGON))
    checkFormatAndArch(D, Formats[I++], Triple::hexagon);
}

TEST(ELFObjectFileTest, MachineTestForLANAI) {
  std::array<StringRef, 4> Formats = {"elf32-lanai", "elf32-lanai",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_LANAI))
    checkFormatAndArch(D, Formats[I++], Triple::lanai);
}

TEST(ELFObjectFileTest, MachineTestForMSP430) {
  std::array<StringRef, 4> Formats = {"elf32-msp430", "elf32-msp430",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_MSP430))
    checkFormatAndArch(D, Formats[I++], Triple::msp430);
}

TEST(ELFObjectFileTest, MachineTestForCSKY) {
  std::array<StringRef, 4> Formats = {"elf32-csky", "elf32-csky",
                                      "elf64-unknown", "elf64-unknown"};
  size_t I = 0;
  for (const DataForTest &D : generateData(ELF::EM_CSKY))
    checkFormatAndArch(D, Formats[I++], Triple::csky);
}

// ELF relative relocation type test.
TEST(ELFObjectFileTest, RelativeRelocationTypeTest) {
  EXPECT_EQ(ELF::R_CKCORE_RELATIVE, getELFRelativeRelocationType(ELF::EM_CSKY));
}

template <class ELFT>
static Expected<ELFObjectFile<ELFT>> toBinary(SmallVectorImpl<char> &Storage,
                                              StringRef Yaml) {
  raw_svector_ostream OS(Storage);
  yaml::Input YIn(Yaml);
  if (!yaml::convertYAML(YIn, OS, [](const Twine &Msg) {}))
    return createStringError(std::errc::invalid_argument,
                             "unable to convert YAML");
  return ELFObjectFile<ELFT>::create(MemoryBufferRef(OS.str(), "dummyELF"));
}

// Check we are able to create an ELFObjectFile even when the content of the
// SHT_SYMTAB_SHNDX section can't be read properly.
TEST(ELFObjectFileTest, InvalidSymtabShndxTest) {
  SmallString<0> Storage;
  Expected<ELFObjectFile<ELF64LE>> ExpectedFile = toBinary<ELF64LE>(Storage, R"(
--- !ELF
FileHeader:
  Class: ELFCLASS64
  Data:  ELFDATA2LSB
  Type:  ET_REL
Sections:
  - Name:    .symtab_shndx
    Type:    SHT_SYMTAB_SHNDX
    Entries: [ 0 ]
    ShSize: 0xFFFFFFFF
)");

  ASSERT_THAT_EXPECTED(ExpectedFile, Succeeded());
}

// Test that we are able to create an ELFObjectFile even when loadable segments
// are unsorted by virtual address.
// Test that ELFFile<ELFT>::toMappedAddr works properly in this case.

TEST(ELFObjectFileTest, InvalidLoadSegmentsOrderTest) {
  SmallString<0> Storage;
  Expected<ELFObjectFile<ELF64LE>> ExpectedFile = toBinary<ELF64LE>(Storage, R"(
--- !ELF
FileHeader:
  Class: ELFCLASS64
  Data:  ELFDATA2LSB
  Type:  ET_EXEC
Sections:
  - Name:         .foo
    Type:         SHT_PROGBITS
    Address:      0x1000
    Offset:       0x3000
    ContentArray: [ 0x11 ]
  - Name:         .bar
    Type:         SHT_PROGBITS
    Address:      0x2000
    Offset:       0x4000
    ContentArray: [ 0x99 ]
ProgramHeaders:
  - Type:     PT_LOAD
    VAddr:    0x2000
    FirstSec: .bar
    LastSec:  .bar
  - Type:     PT_LOAD
    VAddr:    0x1000
    FirstSec: .foo
    LastSec:  .foo
)");

  ASSERT_THAT_EXPECTED(ExpectedFile, Succeeded());

  std::string WarnString;
  auto ToMappedAddr = [&](uint64_t Addr) -> const uint8_t * {
    Expected<const uint8_t *> DataOrErr =
        ExpectedFile->getELFFile().toMappedAddr(Addr, [&](const Twine &Msg) {
          EXPECT_TRUE(WarnString.empty());
          WarnString = Msg.str();
          return Error::success();
        });

    if (!DataOrErr) {
      ADD_FAILURE() << toString(DataOrErr.takeError());
      return nullptr;
    }

    EXPECT_TRUE(WarnString ==
                "loadable segments are unsorted by virtual address");
    WarnString = "";
    return *DataOrErr;
  };

  const uint8_t *Data = ToMappedAddr(0x1000);
  ASSERT_TRUE(Data);
  MemoryBufferRef Buf = ExpectedFile->getMemoryBufferRef();
  EXPECT_EQ((const char *)Data - Buf.getBufferStart(), 0x3000);
  EXPECT_EQ(Data[0], 0x11);

  Data = ToMappedAddr(0x2000);
  ASSERT_TRUE(Data);
  Buf = ExpectedFile->getMemoryBufferRef();
  EXPECT_EQ((const char *)Data - Buf.getBufferStart(), 0x4000);
  EXPECT_EQ(Data[0], 0x99);
}

// This is a test for API that is related to symbols.
// We check that errors are properly reported here.
TEST(ELFObjectFileTest, InvalidSymbolTest) {
  SmallString<0> Storage;
  Expected<ELFObjectFile<ELF64LE>> ElfOrErr = toBinary<ELF64LE>(Storage, R"(
--- !ELF
FileHeader:
  Class:   ELFCLASS64
  Data:    ELFDATA2LSB
  Type:    ET_DYN
  Machine: EM_X86_64
Sections:
  - Name: .symtab
    Type: SHT_SYMTAB
)");

  ASSERT_THAT_EXPECTED(ElfOrErr, Succeeded());
  const ELFFile<ELF64LE> &Elf = ElfOrErr->getELFFile();
  const ELFObjectFile<ELF64LE> &Obj = *ElfOrErr;

  Expected<const typename ELF64LE::Shdr *> SymtabSecOrErr = Elf.getSection(1);
  ASSERT_THAT_EXPECTED(SymtabSecOrErr, Succeeded());
  ASSERT_EQ((*SymtabSecOrErr)->sh_type, ELF::SHT_SYMTAB);

  // We create a symbol with an index that is too large to exist in the object.
  constexpr unsigned BrokenSymIndex = 0xFFFFFFFF;
  ELFSymbolRef BrokenSym = Obj.toSymbolRef(*SymtabSecOrErr, BrokenSymIndex);

  const char *ErrMsg = "unable to access section [index 1] data at "
                       "0x1800000028: offset goes past the end of file";
  // 1) Check the behavior of ELFObjectFile<ELFT>::getSymbolName().
  //    SymbolRef::getName() calls it internally. We can't test it directly,
  //    because it is protected.
  EXPECT_THAT_ERROR(BrokenSym.getName().takeError(), FailedWithMessage(ErrMsg));

  // 2) Check the behavior of ELFObjectFile<ELFT>::getSymbol().
  EXPECT_THAT_ERROR(Obj.getSymbol(BrokenSym.getRawDataRefImpl()).takeError(),
                    FailedWithMessage(ErrMsg));

  // 3) Check the behavior of ELFObjectFile<ELFT>::getSymbolSection().
  //    SymbolRef::getSection() calls it internally. We can't test it directly,
  //    because it is protected.
  EXPECT_THAT_ERROR(BrokenSym.getSection().takeError(),
                    FailedWithMessage(ErrMsg));

  // 4) Check the behavior of ELFObjectFile<ELFT>::getSymbolFlags().
  //    SymbolRef::getFlags() calls it internally. We can't test it directly,
  //    because it is protected.
  EXPECT_THAT_ERROR(BrokenSym.getFlags().takeError(),
                    FailedWithMessage(ErrMsg));

  // 5) Check the behavior of ELFObjectFile<ELFT>::getSymbolType().
  //    SymbolRef::getType() calls it internally. We can't test it directly,
  //    because it is protected.
  EXPECT_THAT_ERROR(BrokenSym.getType().takeError(), FailedWithMessage(ErrMsg));

  // 6) Check the behavior of ELFObjectFile<ELFT>::getSymbolAddress().
  //    SymbolRef::getAddress() calls it internally. We can't test it directly,
  //    because it is protected.
  EXPECT_THAT_ERROR(BrokenSym.getAddress().takeError(),
                    FailedWithMessage(ErrMsg));

  // Finally, check the `ELFFile<ELFT>::getEntry` API. This is an underlying
  // method that generates errors for all cases above.
  EXPECT_THAT_EXPECTED(Elf.getEntry<typename ELF64LE::Sym>(**SymtabSecOrErr, 0),
                       Succeeded());
  EXPECT_THAT_ERROR(
      Elf.getEntry<typename ELF64LE::Sym>(**SymtabSecOrErr, BrokenSymIndex)
          .takeError(),
      FailedWithMessage(ErrMsg));
}
