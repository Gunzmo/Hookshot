#pragma once

namespace rx {
// Same linear movie upscale used when Vulkan cannot compile an NVN2 network (vk_draw.cpp).
// textureLoad avoids requiring filterable float formats or a particular sampler state.
inline constexpr char movie_upscale_wgsl[] = R"(
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var dst: texture_storage_2d<rgba8unorm, write>;
@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let out_size = textureDimensions(dst);
    if (any(id.xy >= out_size)) { return; }
    let in_size = textureDimensions(src);
    let pos = (vec2<f32>(id.xy) + 0.5) * vec2<f32>(in_size) / vec2<f32>(out_size) - 0.5;
    let p = vec2<i32>(floor(pos));
    let f = fract(pos);
    let hi = vec2<i32>(in_size) - 1;
    let a = textureLoad(src, clamp(p, vec2<i32>(0), hi), 0);
    let b = textureLoad(src, clamp(p + vec2<i32>(1, 0), vec2<i32>(0), hi), 0);
    let c = textureLoad(src, clamp(p + vec2<i32>(0, 1), vec2<i32>(0), hi), 0);
    let d = textureLoad(src, clamp(p + vec2<i32>(1, 1), vec2<i32>(0), hi), 0);
    textureStore(dst, id.xy, mix(mix(a, b, f.x), mix(c, d, f.x), f.y));
}
)";
} // namespace rx
