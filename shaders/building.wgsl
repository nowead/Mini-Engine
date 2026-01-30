// Building shader with lighting and shadow mapping
// WebGPU WGSL version

struct UniformBufferObject {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    proj: mat4x4<f32>,
    sunDirection: vec3<f32>,      // Normalized direction TO the sun
    sunIntensity: f32,            // Sun light intensity
    sunColor: vec3<f32>,          // Sun light color
    ambientIntensity: f32,        // Ambient light intensity
    cameraPos: vec3<f32>,         // Camera position for specular
    _padding: f32,
    // Shadow mapping
    lightSpaceMatrix: mat4x4<f32>, // Light view-projection matrix
    shadowMapSize: vec2<f32>,      // Shadow map dimensions
    shadowBias: f32,               // Depth bias
    shadowStrength: f32,           // Shadow darkness (0-1)
}

@group(0) @binding(0) var<uniform> ubo: UniformBufferObject;
@group(0) @binding(1) var shadowMapTex: texture_depth_2d;
@group(0) @binding(2) var shadowMapSampler: sampler;

// Vertex input
struct VertexInput {
    // Per-vertex attributes (binding 0)
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    // Per-instance attributes (binding 1)
    @location(3) instancePosition: vec3<f32>,
    @location(4) instanceColor: vec3<f32>,
    @location(5) instanceScale: vec3<f32>,
}

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) worldPos: vec3<f32>,
    @location(3) posLightSpace: vec4<f32>,
}

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Apply instance transform with independent scale per axis
    let worldPos = input.position * input.instanceScale + input.instancePosition;

    // Transform to clip space
    output.position = ubo.proj * ubo.view * ubo.model * vec4<f32>(worldPos, 1.0);

    // Pass color, normal, and world position to fragment shader
    output.color = input.instanceColor;
    output.normal = input.normal;
    output.worldPos = worldPos;

    // Transform position to light space for shadow mapping
    output.posLightSpace = ubo.lightSpaceMatrix * vec4<f32>(worldPos, 1.0);

    return output;
}

// Calculate shadow using PCF (Percentage Closer Filtering)
// Note: All texture sampling must happen in uniform control flow in WGSL
fn calculateShadow(posLightSpace: vec4<f32>, normal: vec3<f32>, lightDir: vec3<f32>) -> f32 {
    // Perspective divide
    var projCoords = posLightSpace.xyz / posLightSpace.w;

    // Transform X/Y to [0,1] range for texture sampling
    // Z is already in [0,1] from Vulkan-compatible projection matrix
    projCoords.x = projCoords.x * 0.5 + 0.5;
    // WebGPU: Flip Y because texture origin is top-left but NDC Y+ is up
    // NDC y=-1 (scene bottom) should sample texture bottom (uv.y=1)
    projCoords.y = (-projCoords.y) * 0.5 + 0.5;

    // Use minimal bias - PCF averaging helps reduce shadow acne
    let bias = ubo.shadowBias * 0.01;  // Very minimal bias

    // Current fragment depth from light's perspective
    let currentDepth = projCoords.z;

    // Clamp sample coordinates to valid range for texture sampling
    let clampedCoords = clamp(projCoords.xy, vec2<f32>(0.0), vec2<f32>(1.0));

    // PCF: Sample neighboring texels for soft shadows
    // Sample all texels unconditionally (required for uniform control flow)
    let texelSize = 1.0 / ubo.shadowMapSize;

    var shadow: f32 = 0.0;

    // Unroll the loop to ensure uniform control flow
    // Row -1
    let d_m1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, -1.0) * texelSize);
    let d_0_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(0.0, -1.0) * texelSize);
    let d_p1_m1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(1.0, -1.0) * texelSize);
    // Row 0
    let d_m1_0 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, 0.0) * texelSize);
    let d_0_0 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords);
    let d_p1_0 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(1.0, 0.0) * texelSize);
    // Row +1
    let d_m1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(-1.0, 1.0) * texelSize);
    let d_0_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(0.0, 1.0) * texelSize);
    let d_p1_p1 = textureSample(shadowMapTex, shadowMapSampler, clampedCoords + vec2<f32>(1.0, 1.0) * texelSize);

    // Compare depths
    let compDepth = currentDepth - bias;
    shadow += select(0.0, 1.0, compDepth > d_m1_m1);
    shadow += select(0.0, 1.0, compDepth > d_0_m1);
    shadow += select(0.0, 1.0, compDepth > d_p1_m1);
    shadow += select(0.0, 1.0, compDepth > d_m1_0);
    shadow += select(0.0, 1.0, compDepth > d_0_0);
    shadow += select(0.0, 1.0, compDepth > d_p1_0);
    shadow += select(0.0, 1.0, compDepth > d_m1_p1);
    shadow += select(0.0, 1.0, compDepth > d_0_p1);
    shadow += select(0.0, 1.0, compDepth > d_p1_p1);
    shadow /= 9.0;  // Average 3x3 samples

    // Check if outside light frustum - if so, no shadow
    let outsideFrustum = projCoords.z > 1.0 || projCoords.z < 0.0 ||
                         projCoords.x < 0.0 || projCoords.x > 1.0 ||
                         projCoords.y < 0.0 || projCoords.y > 1.0;

    return select(shadow * ubo.shadowStrength, 0.0, outsideFrustum);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let N = normalize(input.normal);
    let L = normalize(ubo.sunDirection);
    let V = normalize(ubo.cameraPos - input.worldPos);
    let H = normalize(L + V);

    // Calculate shadow factor
    let shadow = calculateShadow(input.posLightSpace, N, L);

    // Ambient lighting (not affected by shadow)
    let ambient = ubo.ambientIntensity * input.color;

    // Diffuse lighting (Lambertian) - affected by shadow
    let NdotL = max(dot(N, L), 0.0);
    let diffuse = ubo.sunIntensity * NdotL * ubo.sunColor * input.color;

    // Specular lighting (Blinn-Phong) - affected by shadow
    let NdotH = max(dot(N, H), 0.0);
    let shininess = 32.0;
    let spec = pow(NdotH, shininess);
    let specular = ubo.sunIntensity * spec * ubo.sunColor * 0.3;

    // Apply shadow: ambient is always visible, diffuse/specular reduced in shadow
    var finalColor = ambient + (1.0 - shadow) * (diffuse + specular);

    // Simple tone mapping
    finalColor = finalColor / (finalColor + vec3<f32>(1.0));

    return vec4<f32>(finalColor, 1.0);
}
