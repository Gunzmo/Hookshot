// Native instructions reported by the Linux renderer, with a terminating EXIT.
#include <cassert>
#include <cstdio>
#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
using namespace Shader::Ampere;

static Instruction instruction(uint64_t lo, uint64_t hi) {
    return {{uint32_t(lo), uint32_t(lo >> 32), uint32_t(hi), uint32_t(hi >> 32)}};
}
static LoweredProgram lower(Instruction insn) {
    const Instruction code[]{insn, instruction(0x794d, 0x03800000)};
    return LowerToMaxwell(code);
}
int main() {
    // TXQ with two components from compute descriptor bank 0.
    auto txq = lower(instruction(0x0000160002027b6f, 0x000f4200000003ff));
    assert(txq && txq.required_texture_buffer == 0);
    const auto q = txq.instructions[0];
    assert((q >> 51) == (0xdf48000000000000ULL >> 51));
    assert(((q >> 31) & 15) == 3); // xy mask, not the old fixed bank-2 restriction
    assert((q & 255) == 2);

    // Font FSETP with a uniform-register source. It must read UR5's mapped GPR,
    // and preserve the original comparison, accumulator and destinations.
    auto font = lower(instruction(0x0000000522009c0b, 0x000fc8000bf1b000));
    assert(font);
    const auto f = font.instructions[0];
    assert((f >> 52) == 0x5bb);
    assert(((f >> 20) & 255) == 221); // UR5 -> R221
    assert(((f >> 8) & 255) == 34);
    assert(((f >> 48) & 15) == 11); // unordered less-than
    assert(((f >> 16) & 15) == 9); // @!P1

    // UIMAD.WIDE immediate, used for the movie's dispatch/address calculations.
    auto movie = lower(instruction(0x88888889060478a5, 0x002fe2000f8e003f));
    assert(movie);
    // The final two adds write the high and low words of UR4:UR5, not ordinary R4:R5.
    auto end = movie.instructions.size() - 1; // before EXIT
    assert((movie.instructions[end - 2] & 255) == 221);
    assert((movie.instructions[end - 1] & 255) == 220);
    assert(movie.instructions.size() > 10); // full multiply, not a move or no-op

    // Mixed use must reserve uniform storage even when FSETP is the only UR read.
    auto invalid = instruction(0x0000000522009c0b, 0x000fc8000bf1b000);
    invalid.words[0] = (invalid.words[0] & 0x00ffffff) | (246u << 24);
    assert(!lower(invalid));
    invalid = instruction(0x88888889060578a5, 0x002fe2000f8e003f); // odd wide destination
    assert(!lower(invalid));
    invalid = instruction(0x0000160002027b6f, 0x000f4200000010ff); // unsupported TXQ mode
    assert(!lower(invalid));
    puts("NVN2 font comparison, texture query and movie arithmetic regressions passed");
}
