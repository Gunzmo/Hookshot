// Integration with the real runtime, NVN replay, shader compiler, WebGPU device and GPU readback.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <sys/mman.h>
#include "gpu/render/rx_internal.h"
extern "C" uint8_t* hos_guest_base();
extern "C" void rx_wait_idle();
extern "C" void wgr_nvn_submit(const uint64_t*, int);
using namespace rx;

namespace {
std::vector<uint8_t> records;
template<class T> void record(uint32_t op, const T& v) {
    const size_t at = records.size(), size = (sizeof(nvn_op_hdr) + sizeof v + 7) & ~7u;
    records.resize(at + size);
    const nvn_op_hdr h{op, uint32_t(size)};
    std::memcpy(records.data() + at, &h, sizeof h);
    std::memcpy(records.data() + at + sizeof h, &v, sizeof v);
}
void push(uint32_t offset, const float (&values)[2]) {
    struct { nvn_p_push_const p; float values[2]; } v{{offset, sizeof values}, {values[0], values[1]}};
    record(NOP_PUSH_CONST, v);
}
void pixels(Image* image, const std::vector<uint8_t>& expected) {
    rx_wait_idle();
    const uint64_t size = 256 * image->height;
    WGPUBufferDescriptor bd{};
    bd.size = size, bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    auto buffer = wgpuDeviceCreateBuffer(dev.device, &bd);
    auto e = wgpuDeviceCreateCommandEncoder(dev.device, nullptr);
    WGPUTexelCopyTextureInfo src{};
    src.texture = image->tex;
    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = buffer, dst.layout.bytesPerRow = 256, dst.layout.rowsPerImage = image->height;
    const WGPUExtent3D extent{image->width, image->height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(e, &src, &dst, &extent);
    auto cmd = wgpuCommandEncoderFinish(e, nullptr);
    wgpuQueueSubmit(dev.queue, 1, &cmd);
    bool done = false;
    WGPUBufferMapCallbackInfo cb{};
    cb.mode = WGPUCallbackMode_AllowProcessEvents;
    cb.userdata1 = &done;
    cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* data, void*) {
        assert(status == WGPUMapAsyncStatus_Success);
        *static_cast<bool*>(data) = true;
    };
    wgpuBufferMapAsync(buffer, WGPUMapMode_Read, 0, size, cb);
    while (!done) wgpuDevicePoll(dev.device, true, nullptr);
    const auto* p = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(buffer, 0, size));
    for (uint32_t y = 0; y < image->height; ++y)
        for (uint32_t x = 0; x < image->width * 4; ++x) {
            const auto want = expected[(y * image->width * 4 + x) % expected.size()];
            if (std::abs(int(p[y * 256 + x]) - int(want)) > 1) {
                std::fprintf(stderr, "pixel (%u,%u) got %u, wanted %u\n", x, y, p[y * 256 + x], want);
                std::abort();
            }
        }
    // Clear the actual GPU image between cases: a skipped dispatch must fail the next pixel check.
    std::vector<uint8_t> clear(image->width * image->height * 4);
    WGPUTexelCopyBufferLayout clear_layout{};
    clear_layout.bytesPerRow = image->width * 4, clear_layout.rowsPerImage = image->height;
    wgpuQueueWriteTexture(dev.queue, &src, clear.data(), clear.size(), &clear_layout, &extent);
    wgpuBufferUnmap(buffer);
    wgpuBufferRelease(buffer);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(e);
}
nvn_tex texture(uint64_t gpu, uint32_t width, uint32_t height, uint64_t serial) {
    nvn_tex t{};
    t.gpu = gpu, t.serial = serial;
    t.d.target = NVN_TEXTURE_TARGET_2D, t.d.format = NVN_FORMAT_RGBA8;
    t.d.width = width, t.d.height = height, t.d.depth = 1, t.d.levels = 1;
    t.d.flags = 0x10, t.d.stride = width * 4;
    nvn_tex_layout(&t.d, &t.L);
    return t;
}
}

void test_webgpu_nvn() {
    constexpr uint64_t base = 0x02000000;
    auto* memory = hos_guest_base() + base;
    assert(mmap(memory, 0x10000, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == memory);
    auto put32 = [&](size_t at, uint32_t value) { std::memcpy(memory + at, &value, 4); };
    put32(0, 0x98761234);
    memory[0x56] = 5;
    memory[0x58] = memory[0x5c] = memory[0x60] = 1; // workgroup size
    memory[0xa9] = 15; // 60 push bytes, 64 bytes reserved
    memory[0xb1] = 1, memory[0xb2] = 1, memory[0xb3] = 1, memory[0xb6] = 1;
    NvnState s;
    s.has_prog = true, s.prog.serial = 100, s.prog.nstage = 1;
    s.prog.sd[0].control = base;
    s.grid[0] = 7, s.grid[1] = 8, s.grid[2] = 9;
    s.cs_push_n = 60;
    for (unsigned i = 0; i < 60; ++i) s.cs_push[i] = i + 1;
    s.ubo[5][0].addr = 0x1122334455667788, s.ubo[5][0].size = 0x90;
    s.ssbo[5][0].addr = 0x9988776655443322, s.ssbo[5][0].size = 0x80;
    s.tex[5][0] = 123, s.img[5][0] = 456;
    uint8_t bank[0xa00];
    nvn_driver_bank(s, 0, 5, bank);
    assert(!std::memcmp(bank + 0x10, s.cs_push, 60));
    assert(!std::memcmp(bank + 0x50, &s.ubo[5][0].addr, 8));
    assert(!std::memcmp(bank + 0x60, &s.ssbo[5][0].addr, 8));
    for (unsigned at = 0; at < sizeof bank; at += 4) {
        uint32_t value;
        std::memcpy(&value, bank + at, 4);
        assert(nvn_driver_bank_word(s, 0, 5, at) == value);
    }

    // Replay two partial push updates, then execute a native shader that writes c0[0x10..0x1f] to RGBA.
    memory[0xa9] = 4, memory[0xb1] = memory[0xb2] = memory[0xb3] = 0;
    s.prog.serial++;
    s.prog.sd[0].data = base + 0x1000;
    auto* code = reinterpret_cast<uint64_t*>(memory + 0x1000);
    unsigned n = 0;
    auto insn = [&](uint64_t lo, uint64_t hi) { code[n++] = lo; code[n++] = hi; };
    for (unsigned r = 0; r < 4; ++r)
        insn(0x7a02 | (uint64_t(r) << 16) | (uint64_t(0x10 + r * 4) << 38), 0xf00);
    insn(0x00047802, 0xf00); // R4 = 0
    insn(0x00057802, 0xf00); // R5 = 0
    insn(0x7b9b | (uint64_t(4) << 24) | (uint64_t(0x20 / 4) << 40) | (uint64_t(3) << 61), 0x00100f00);
    insn(0x794d, 0x03800000);
    put32(0x38, n * 8), put32(0x40, n * 8), put32(0x44, n * 8);
    auto colour = texture(base + 0x4000, 1, 1, 1);
    nvn_register_texture(1, colour, nullptr);
    push(0, {0.25f, 0.5f});
    push(8, {0.75f, 1.0f});
    record(NOP_BIND_PROGRAM, nvn_p_bind_program{&s.prog, 1 << 5});
    nvn_p_bind_handles bind{};
    bind.stage = 5, bind.n = 1, bind.h[0] = 1;
    record(NOP_BIND_IMG, bind);
    record(NOP_DISPATCH, nvn_p_dispatch{1, 1, 1, 0});
    record(NOP_END, uint64_t(0));
    const uint64_t handle = reinterpret_cast<uint64_t>(records.data());
    wgr_nvn_submit(&handle, 1);
    pixels(nvn_target(&colour, nullptr), {64, 128, 191, 255});

    // Execute the reported FSETP (including @!P1 and UR5), not just its decoder.
    for (const auto [operand, red] : {std::pair{0x3f000000u, uint8_t(223)}, std::pair{0x40000000u, uint8_t(64)}}) {
        n = 8; // retain four c0 colour loads
        insn(0x00057882 | (uint64_t(0x3f800000) << 32), 0); // UR5 = 1.0
        insn(0x00227802 | (uint64_t(operand) << 32), 0xf00); // R34 = operand
        insn(0xff00720b | (uint64_t(255) << 32), 0x03f21000); // P1 = (RZ < RZ), false
        insn(0x0000000522009c0b, 0x000fc8000bf1b000); // P0 = R34 < UR5, @!P1
        insn(0x802 | (uint64_t(0x3f600000) << 32), 0xf00); // @P0 R0 = 0.875
        insn(0x00047802, 0xf00), insn(0x00057802, 0xf00);
        insn(0x7b9b | (uint64_t(4) << 24) | (uint64_t(0x20 / 4) << 40) | (uint64_t(3) << 61), 0x00100f00);
        insn(0x794d, 0x03800000);
        put32(0x38, n * 8), put32(0x40, n * 8), put32(0x44, n * 8);
        ++s.prog.serial;
        records.clear();
        record(NOP_BIND_PROGRAM, nvn_p_bind_program{&s.prog, 1 << 5});
        record(NOP_DISPATCH, nvn_p_dispatch{1,1,1,0});
        record(NOP_END, uint64_t(0));
        const uint64_t h = reinterpret_cast<uint64_t>(records.data());
        wgr_nvn_submit(&h, 1);
        pixels(nvn_target(&colour, nullptr), {red,128,191,255});
    }

    // Check both words of UIMAD.WIDE against host 64-bit multiplication, including carry-heavy operands.
    for (uint32_t operand : {0u, 8u, 0xffffffffu}) {
        n = 8;
        insn(0x00067882 | (uint64_t(operand) << 32), 0); // UR6
        insn(0x88888889060478a5, 0x002fe2000f8e003f); // exact reported instruction
        insn(0x000a7c02 | (uint64_t(4) << 32), 0x08000f00); // R10 = UR4
        insn(0x000b7c02 | (uint64_t(5) << 32), 0x08000f00); // R11 = UR5
        const uint64_t expected_product = uint64_t(operand) * 0x88888889u;
        insn(0x0a00780c | (uint64_t(uint32_t(expected_product)) << 32), 0x03f02000); // P0 = low == expected
        insn(0x0b00780c | (uint64_t(expected_product >> 32) << 32), 0x00702000); // P0 &= high == expected
        insn(0x802 | (uint64_t(0x3f600000) << 32), 0xf00); // @P0 R0 = 0.875
        insn(0x00047802, 0xf00), insn(0x00057802, 0xf00);
        insn(0x7b9b | (uint64_t(4) << 24) | (uint64_t(0x20 / 4) << 40) | (uint64_t(3) << 61), 0x00100f00);
        insn(0x794d, 0x03800000);
        put32(0x38, n * 8), put32(0x40, n * 8), put32(0x44, n * 8);
        ++s.prog.serial;
        records.clear();
        record(NOP_BIND_PROGRAM, nvn_p_bind_program{&s.prog, 1 << 5});
        record(NOP_DISPATCH, nvn_p_dispatch{1,1,1,0});
        record(NOP_END, uint64_t(0));
        const uint64_t h = reinterpret_cast<uint64_t>(records.data());
        wgr_nvn_submit(&h, 1);
        pixels(nvn_target(&colour, nullptr), {223,128,191,255});
    }

    // Rejected movie upscale: a 2x2 colour input reaches a 4x4 output, including edge clamping and interpolation.
    auto in = texture(base + 0x5000, 2, 2, 2);
    auto out = texture(base + 0x6000, 4, 4, 3);
    const uint8_t input[]{0,0,0,255, 240,0,0,255, 0,240,0,255, 240,240,0,255};
    std::memcpy(memory + 0x5000, input, sizeof input);
    nvn_register_texture(2, in, nullptr), nvn_register_texture(3, out, nullptr);
    memory[0xb3] = 1;
    s.prog.serial++, s.tex[5][0] = 2, s.img[5][0] = 3;
    // This shader contains the unsupported HMMA from the user's log: normal dispatch must use the fallback.
    n = 0;
    insn(0x0000000c1800723c, 0x006fe600000008ff);
    insn(0x794d, 0x03800000);
    put32(0x38, n * 8), put32(0x40, n * 8), put32(0x44, n * 8);
    nvn_dispatch(s, 1, 1, 1);
    assert(nvn_target(&out, nullptr)->rendered);
    std::vector<uint8_t> expected;
    for (auto g : {0,60,180,240}) for (auto r : {0,60,180,240})
        expected.insert(expected.end(), {uint8_t(r),uint8_t(g),0,255});
    pixels(nvn_target(&out, nullptr), expected);
    // The final network pass can translate even though its earlier tensor features were never produced.
    // Its control signature (21 push words plus SSBO) selects the colour fallback in that same frame.
    n = 0;
    insn(0x794d, 0x03800000);
    put32(0x38, n * 8), put32(0x40, n * 8), put32(0x44, n * 8);
    memory[0xa9] = 21, memory[0xb2] = 1, ++s.prog.serial;
    nvn_dispatch(s, 1, 1, 1);
    pixels(nvn_target(&out, nullptr), expected);
    // With no rejected tensor dispatch in the new frame, this translated no-op stays a no-op.
    ++frame_number;
    nvn_dispatch(s, 1, 1, 1);
    pixels(nvn_target(&out, nullptr), {0,0,0,0});
    // A view that cannot be upscaled can still alias the output: keep it in the hazard check.
    nvn_view swizzled{};
    swizzled.set_swizzle = 1;
    nvn_register_texture(4, out, &swizzled);
    memory[0xb3] = 2, ++s.prog.serial;
    s.tex[5][1] = 4;
    assert(!nvn_upscale_fallback(s));
    memory[0xb3] = 1, ++s.prog.serial;
    // Read/write feedback, downscales and later undeclared slots are not movie outputs.
    s.tex[5][0] = 3;
    assert(!nvn_upscale_fallback(s));
    s.tex[5][0] = 0, s.tex[5][1] = 2;
    assert(!nvn_upscale_fallback(s));
    s.tex[5][0] = 3, s.img[5][0] = 2;
    assert(!nvn_upscale_fallback(s));
    puts("Native NVN push replay, packed compute banks and movie upscale GPU pixels passed");
}
