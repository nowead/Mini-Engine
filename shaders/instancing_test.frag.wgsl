// Instancing test fragment shader
// WebGPU WGSL version

struct FragmentInput {
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
}

@fragment
fn main(input: FragmentInput) -> @location(0) vec4<f32> {
    // Simple lighting calculation
    let lightDir = normalize(vec3<f32>(0.5, 1.0, 0.3));
    let diffuse = max(dot(normalize(input.normal), lightDir), 0.3);

    // Apply lighting to color
    let finalColor = input.color * diffuse;

    return vec4<f32>(finalColor, 1.0);
}
