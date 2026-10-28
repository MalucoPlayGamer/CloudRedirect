// Read-only compatibility check: maps PE bytes as non-executable data.
// No Steam entry point, export or resolved function is executed.
#include "sc_resolver.h"
#include "protobuf_wrapper_layout.h"
#include <windows.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <stdexcept>
namespace Log { void Write(const char* fmt, ...) { va_list a; va_start(a,fmt); vprintf(fmt,a); va_end(a); puts(""); } }
int main(int argc,char** argv) {
 if(argc!=2) { puts("Usage: dolintools_resolver_tests <steamclient64.dll>"); return 2; }
 std::ifstream f(argv[1],std::ios::binary|std::ios::ate); if(!f) return 2;
 size_t len=(size_t)f.tellg(); if(len<4096 || len>256*1024*1024) return 2;
 std::vector<unsigned char> raw(len); f.seekg(0);f.read((char*)raw.data(),len);
 auto dos=(IMAGE_DOS_HEADER*)raw.data(); if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0 || (size_t)dos->e_lfanew+sizeof(IMAGE_NT_HEADERS64)>len) return 2;
 auto nt=(IMAGE_NT_HEADERS64*)(raw.data()+dos->e_lfanew);
 if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 2;
 const auto size=nt->OptionalHeader.SizeOfImage;
 auto image=(unsigned char*)VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); if(!image) return 2;
 auto fits=[&](size_t off,size_t n){return off<=size && n<=size-off;};
 if(nt->OptionalHeader.SizeOfHeaders>len || !fits(0,nt->OptionalHeader.SizeOfHeaders)) return 2;
 memcpy(image,raw.data(),nt->OptionalHeader.SizeOfHeaders);
 auto sec=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++) {
  if((unsigned char*)(sec+i+1)>raw.data()+len || sec[i].PointerToRawData>len || sec[i].SizeOfRawData>len-sec[i].PointerToRawData || !fits(sec[i].VirtualAddress,sec[i].SizeOfRawData)) return 2;
  memcpy(image+sec[i].VirtualAddress,raw.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);
 }
 uintptr_t base=(uintptr_t)image; auto delta=base-nt->OptionalHeader.ImageBase;
 auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 if(!fits(dir.VirtualAddress,dir.Size)) return 2;
 for(size_t i=0;i+sizeof(IMAGE_BASE_RELOCATION)<=dir.Size;) {
  auto block=(IMAGE_BASE_RELOCATION*)(image+dir.VirtualAddress+i);
  if(block->SizeOfBlock<8 || block->SizeOfBlock>dir.Size-i) return 2;
  auto e=(WORD*)(block+1);auto count=(block->SizeOfBlock-8)/2;
  for(unsigned j=0;j<count;j++) if((e[j]>>12)==IMAGE_REL_BASED_DIR64) {
   auto rva=block->VirtualAddress+(e[j]&0xfff); if(!fits(rva,8)) return 2; *(uintptr_t*)(image+rva)+=delta;
  }
  i+=block->SizeOfBlock;
 }
 auto r=ScResolver::Resolve(base);ScResolver::LogComparison(r,base);
 int failed=0;
 auto check=[&](bool ok,const char* name){printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)failed++;};
 check(r.globalEngine!=0 && r.engineOffAppInfoCache!=0,"engine and AppInfoCache detected without historical fallback");
 check(r.flushAppMinutesPlayed!=0,"playtime flush resolved");
 check(r.pbMsgCtor && r.pbMsgFinalize && r.pbMsgCleanup,"protobuf constructor/finalizer/destructor resolved");
 check(r.getUserStatsDesc && r.getUserStatsVtable,"schema primitives resolved");
 ProtobufWrapperLayout::Layout layout;
 auto slot=r.serviceTransportVtable ? *(uintptr_t*)(r.serviceTransportVtable+5*sizeof(uintptr_t)) : 0;
 check(slot>=base && slot+192<=base+size && ProtobufWrapperLayout::Detect((uint8_t*)slot,192,layout),"transport wrapper layout validated");
 printf("RESULT cache=%x engine=%llx ctor=%llx finalize=%llx cleanup=%llx body=%x header=%x\n",r.engineOffAppInfoCache,(unsigned long long)(r.globalEngine-base),(unsigned long long)(r.pbMsgCtor-base),(unsigned long long)(r.pbMsgFinalize-base),(unsigned long long)(r.pbMsgCleanup-base),layout.body,layout.header);
 VirtualFree(image,0,MEM_RELEASE);return failed?1:0;
}
