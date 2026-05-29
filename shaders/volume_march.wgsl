// Volume ray marching with depth-aware compositing (WebGPU port of
// volume_march.{vert,frag}.glsl). Fullscreen pass run after deferred lighting:
// rebuild the camera ray, intersect the volume AABB, clamp the far end to scene
// depth (opaque geometry occludes the volume), accumulate Beer-Lambert opacity
// front-to-back, output PREMULTIPLIED color (blended One/OneMinusSrcAlpha).
//
// Matches deferred_lighting.wgsl's depth reconstruction: WebGPU has NO projection
// Y-flip, so ndc.y = 1 - uv.y*2 (not uv.y*2 - 1 as on Vulkan).

struct VolumeUBO {
    invView:   mat4x4<f32>,
    invProj:   mat4x4<f32>,
    cameraPos: vec4<f32>,
    aabbMin:   vec4<f32>,
    aabbMax:   vec4<f32>,
    params:    vec4<f32>,   // x=stepSize, y=extinction, z=densityScale, w=maxSteps
    tf:        vec4<f32>,   // x=densityThreshold, y=colorMix, z=useLUT (0/1), w=useDepth
    lowColor:  vec4<f32>,
    highColor: vec4<f32>,
    window:    vec4<f32>,   // x=windowCenter, y=windowWidth (intensity -> [0,1]); zw spare
    light:     vec4<f32>,   // xyz = light dir (world), w = shadingEnable (0/1)
    shade:     vec4<f32>,   // x=ambient, y=diffuse, z=gradEps (texture-space step), w spare
    shadow:    vec4<f32>,   // x=enable (0/1), y=stepSize (world), z=maxSteps, w=strength
};

@group(0) @binding(0) var<uniform> ubo: VolumeUBO;
@group(0) @binding(1) var depthTex:      texture_depth_2d;
@group(0) @binding(2) var volumeTex:     texture_3d<f32>;
@group(0) @binding(3) var volumeSampler: sampler;
@group(0) @binding(4) var tfLUT:         texture_2d<f32>;   // 256x1 density -> (rgb, opacity)

struct VSOut {
    @builtin(position) position: vec4<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) vertIdx: u32) -> VSOut {
    let uv = vec2<f32>(f32((vertIdx << 1u) & 2u), f32(vertIdx & 2u));
    var out: VSOut;
    out.position = vec4<f32>(uv * 2.0 - 1.0, 0.0, 1.0);
    return out;
}

// Ray-AABB slab test; returns (tNear, tFar), tNear > tFar means miss.
fn intersectAABB(ro: vec3<f32>, rd: vec3<f32>, bmin: vec3<f32>, bmax: vec3<f32>) -> vec2<f32> {
    let invD = 1.0 / rd;
    let t0 = (bmin - ro) * invD;
    let t1 = (bmax - ro) * invD;
    let tsmall = min(t0, t1);
    let tbig   = max(t0, t1);
    let tn = max(max(tsmall.x, tsmall.y), tsmall.z);
    let tf = min(min(tbig.x, tbig.y), tbig.z);
    return vec2<f32>(tn, tf);
}

@fragment
fn fs_main(@builtin(position) fragPos: vec4<f32>) -> @location(0) vec4<f32> {
    let screenSize = vec2<f32>(textureDimensions(depthTex));
    let uv         = fragPos.xy / screenSize;
    let fragCoord  = vec2<i32>(fragPos.xy);

    // WebGPU NDC: y=+1 is top, matching @builtin(position).y=0 at top.
    let ndcXY = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    // Camera ray from the far-plane world point.
    var vFar = ubo.invProj * vec4<f32>(ndcXY, 1.0, 1.0);
    vFar = vFar / vFar.w;
    let worldFar = (ubo.invView * vFar).xyz;
    let ro = ubo.cameraPos.xyz;
    let rd = normalize(worldFar - ro);

    // Scene occlusion distance from the depth buffer. tf.w = useDepth: 0 disables
    // occlusion (standalone viewer, no geometry / dummy depth) -> march full box.
    var sceneDist = 1e30;
    if (ubo.tf.w > 0.5) {
        let depth = textureLoad(depthTex, fragCoord, 0);
        if (depth < 1.0) {
            var vS = ubo.invProj * vec4<f32>(ndcXY, depth, 1.0);
            vS = vS / vS.w;
            let worldS = (ubo.invView * vS).xyz;
            sceneDist = length(worldS - ro);
        }
    }

    let hit   = intersectAABB(ro, rd, ubo.aabbMin.xyz, ubo.aabbMax.xyz);
    let tNear = max(hit.x, 0.0);
    let tFar  = min(hit.y, sceneDist);
    if (tNear >= tFar) {
        return vec4<f32>(0.0);
    }

    let stepSize   = ubo.params.x;
    let extinction = ubo.params.y;
    let maxSteps   = i32(ubo.params.w);
    let boxSize    = ubo.aabbMax.xyz - ubo.aabbMin.xyz;

    var accum = vec4<f32>(0.0);   // premultiplied rgb + alpha
    var t     = tNear;
    for (var i = 0; i < maxSteps; i = i + 1) {
        if (t >= tFar || accum.a > 0.99) {
            break;
        }
        let wp  = ro + rd * t;
        let uvw = (wp - ubo.aabbMin.xyz) / boxSize;     // [0,1]^3
        // textureSampleLevel: valid in the non-uniform loop (no derivatives).
        let raw = textureSampleLevel(volumeTex, volumeSampler, uvw, 0.0).r;
        // Window/level: map [center - width/2, center + width/2] -> [0,1] (contrast).
        let n = clamp((raw - (ubo.window.x - ubo.window.y * 0.5)) / max(ubo.window.y, 1e-6), 0.0, 1.0);
        var density = n * ubo.params.z;
        density = max(density - ubo.tf.x, 0.0);

        if (density > 0.0) {
            var color: vec3<f32>;
            var opacityWeight: f32;
            if (ubo.tf.z > 0.5) {
                let lut = textureSampleLevel(tfLUT, volumeSampler,
                                             vec2<f32>(clamp(density, 0.0, 1.0), 0.5), 0.0);
                color = lut.rgb;
                opacityWeight = lut.a;
            } else {
                color = mix(ubo.lowColor.rgb, ubo.highColor.rgb,
                            clamp(density * ubo.tf.y, 0.0, 1.0));
                opacityWeight = density;
            }

            // Gradient shading: density gradient = surface normal, lit with Lambert.
            if (ubo.light.w > 0.5) {
                let e = ubo.shade.z;
                let gx = textureSampleLevel(volumeTex, volumeSampler, uvw + vec3<f32>(e, 0.0, 0.0), 0.0).r
                       - textureSampleLevel(volumeTex, volumeSampler, uvw - vec3<f32>(e, 0.0, 0.0), 0.0).r;
                let gy = textureSampleLevel(volumeTex, volumeSampler, uvw + vec3<f32>(0.0, e, 0.0), 0.0).r
                       - textureSampleLevel(volumeTex, volumeSampler, uvw - vec3<f32>(0.0, e, 0.0), 0.0).r;
                let gz = textureSampleLevel(volumeTex, volumeSampler, uvw + vec3<f32>(0.0, 0.0, e), 0.0).r
                       - textureSampleLevel(volumeTex, volumeSampler, uvw - vec3<f32>(0.0, 0.0, e), 0.0).r;
                let g = vec3<f32>(gx, gy, gz);
                let glen = length(g);
                // Normal points toward DECREASING density (outward from a dense body).
                let L = normalize(ubo.light.xyz);
                let ndl = select(0.0, max(dot(-g / glen, L), 0.0), glen > 1e-6);

                // Volumetric soft shadow: secondary ray toward the light accumulates
                // optical depth so dense regions self-shadow.
                var shadowF = 1.0;
                if (ubo.shadow.x > 0.5 && ndl > 0.0) {
                    let sStep = ubo.shadow.y;
                    let sMax  = i32(ubo.shadow.z);
                    var tau   = 0.0;
                    var st    = sStep;   // start one step out to avoid self-occlusion
                    for (var s = 0; s < sMax; s = s + 1) {
                        let suvw = (wp + L * st - ubo.aabbMin.xyz) / boxSize;
                        if (any(suvw < vec3<f32>(0.0)) || any(suvw > vec3<f32>(1.0))) { break; }
                        let sr = textureSampleLevel(volumeTex, volumeSampler, suvw, 0.0).r;
                        let sn = clamp((sr - (ubo.window.x - ubo.window.y * 0.5)) / max(ubo.window.y, 1e-6), 0.0, 1.0);
                        let sd = max(sn * ubo.params.z - ubo.tf.x, 0.0);
                        tau = tau + sd * extinction * ubo.shadow.w * sStep;
                        if (tau > 8.0) { break; }
                        st = st + sStep;
                    }
                    shadowF = exp(-tau);
                }
                color = color * (ubo.shade.x + ubo.shade.y * ndl * shadowF);
            }

            let alpha = 1.0 - exp(-opacityWeight * extinction * stepSize);
            accum = vec4<f32>(accum.rgb + (1.0 - accum.a) * color * alpha,
                              accum.a   + (1.0 - accum.a) * alpha);
        }
        t = t + stepSize;
    }

    return accum;  // premultiplied; blended One/OneMinusSrcAlpha over HDR
}
