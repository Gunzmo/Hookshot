#include "../gpu/render/rx_platform.h"
#include <cassert>
#include <cstring>
#include <cerrno>
#include <unistd.h>
int main() {
    constexpr size_t size = 32 << 20;
    auto *p = static_cast<unsigned char *>(rx_platform::allocate(size));
    assert(p && !p[0] && !p[size-1]);
    memset(p,0x42,size);
    assert(rx_platform::reset(p+(16<<20),16<<20));
    assert(p[0]==0x42 && p[(16<<20)-1]==0x42 && !p[16<<20] && !p[size-1]);
    p[size-1]=0x37; assert(p[size-1]==0x37);
    rx_platform::release(p,size);
    unsigned char state;
    assert(mincore(p,4096,&state)==-1 && errno==ENOMEM);
    char dir[]="/tmp/hookshot-webgpu-platform-XXXXXX";
    assert(mkdtemp(dir));
    auto a=std::filesystem::path(dir)/"from", b=std::filesystem::path(dir)/"to";
    FILE *f=fopen(a.c_str(),"wb"); assert(f); fputs("new",f); fclose(f);
    f=fopen(b.c_str(),"wb"); assert(f); fputs("old",f); fclose(f);
    assert(rx_platform::replace(a.c_str(),b.c_str()));
    f=fopen(b.c_str(),"rb"); assert(f); char bytes[4]={}; assert(fread(bytes,1,3,f)==3); fclose(f);
    assert(!strcmp(bytes,"new")); std::filesystem::remove_all(dir);
    assert(rx_platform::casecmp("VULKAN","vulkan")==0);
    puts("WebGPU native allocation, partial reset, release and cache replacement passed");
}
