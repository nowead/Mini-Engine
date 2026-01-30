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

// Procedural sky gradient
fn getSkyColor(rayDir: vec3<f32>, sunDir: vec3<f32>) -> vec3<f32> {
    let dir = normalize(rayDir);

    // Sky colors
    let skyColorZenith = vec3<f32>(0.15, 0.35, 0.65);   // Deep blue at top
    let skyColorHorizon = vec3<f32>(0.6, 0.75, 0.9);    // Light blue at horizon
    let groundColor = vec3<f32>(0.2, 0.2, 0.22);        // Dark gray below horizon

    // Gradient based on Y component
    let t = dir.y * 0.5 + 0.5;  // Map [-1,1] to [0,1]

    var skyColor: vec3<f32>;

    // Below horizon
    if (dir.y < 0.0) {
        let groundBlend = smoothstep(-0.1, 0.0, dir.y);
        return mix(groundColor, skyColorHorizon, groundBlend);
    }

    // Above horizon - gradient from horizon to zenith
    skyColor = mix(skyColorHorizon, skyColorZenith, pow(t, 0.5));

    // Sun
    let sunDot = dot(dir, normalize(sunDir));

    // Sun disk
    let sunIntensity = pow(max(sunDot, 0.0), 256.0) * 2.0;  // Sharp sun disk
    let sunColor = vec3<f32>(1.0, 0.95, 0.8);

    // Sun glow (halo)
    let glowIntensity = pow(max(sunDot, 0.0), 8.0) * 0.3;
    let glowColor = vec3<f32>(1.0, 0.8, 0.5);

    // Combine
    var finalColor = skyColor;
    finalColor += sunColor * sunIntensity;
    finalColor += glowColor * glowIntensity;

    // Horizon haze
    let hazeAmount = 1.0 - pow(abs(dir.y), 0.3);
    let hazeColor = vec3<f32>(0.7, 0.75, 0.85);
    finalColor = mix(finalColor, hazeColor, hazeAmount * 0.3);

    return finalColor;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let skyColor = getSkyColor(input.rayDir, ubo.sunDirection);
    return vec4<f32>(skyColor, 1.0);
}
