// Skybox shader - procedural sky gradient with sun
// WebGPU WGSL version

struct UniformBufferObject {
    invViewProj: mat4x4<f32>,  // Inverse view-projection matrix
    sunDirection: vec3<f32>,   // Normalized sun direction
    time: f32,                 // For animation (optional)
}

@group(0) @binding(0) var<uniform> ubo: UniformBufferObject;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) rayDir: vec3<f32>,
}

// Fullscreen triangle - no vertex input needed
// Generates positions: (-1,-1), (3,-1), (-1,3)
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;

    // Generate fullscreen triangle vertices
    var positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 3.0, -1.0),
        vec2<f32>(-1.0,  3.0)
    );

    let pos = positions[vertexIndex];
    output.position = vec4<f32>(pos, 0.9999, 1.0);  // Near far plane

    // Calculate world-space ray direction from clip-space position
    let worldPos = ubo.invViewProj * vec4<f32>(pos, 1.0, 1.0);
    output.rayDir = worldPos.xyz / worldPos.w;

    return output;
}

// Procedural sky: gradient + sun disk, sunset-toned
fn proceduralSky(rayDir: vec3<f32>, sunDir: vec3<f32>) -> vec3<f32> {
    let dir = normalize(rayDir);
    let elev = dir.y;

    // Sky gradient: deep blue at zenith → warm orange at horizon → dark ground
    let zenith  = vec3<f32>(0.05, 0.15, 0.40);
    let horizon = vec3<f32>(0.60, 0.40, 0.25);
    let ground  = vec3<f32>(0.10, 0.08, 0.05);

    var sky: vec3<f32>;
    if (elev > 0.0) {
        sky = mix(horizon, zenith, sqrt(elev));
    } else {
        sky = mix(horizon, ground, clamp(-elev * 4.0, 0.0, 1.0));
    }

    // Sun disk + glow
    let s = max(0.0, dot(dir, normalize(sunDir)));
    sky += vec3<f32>(1.0, 0.95, 0.8) * (pow(s, 512.0) + pow(s, 8.0) * 0.5);

    // Horizon haze (warm tones to match sunset palette)
    let hazeAmount = 1.0 - pow(abs(elev), 0.3);
    let hazeColor = vec3<f32>(0.65, 0.50, 0.35);
    sky = mix(sky, hazeColor, hazeAmount * 0.3);

    return sky;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let color = proceduralSky(input.rayDir, ubo.sunDirection);
    return vec4<f32>(color, 1.0);
}
