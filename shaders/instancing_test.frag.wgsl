// Fragment shader for GPU instancing demo (WGSL for WebGPU)

struct FragmentInput {
    @location(0) color: vec3<f32>,
    @location(1) normal: vec3<f32>,
}

struct FragmentOutput {
    @location(0) color: vec4<f32>,
}

@fragment
fn main(input: FragmentInput) -> FragmentOutput {
    var output: FragmentOutput;

    let lightDir = normalize(vec3<f32>(0.5, 1.0, 0.3));
    let diffuse = max(dot(normalize(input.normal), lightDir), 0.3);
    let finalColor = input.color * diffuse;
    output.color = vec4<f32>(finalColor, 1.0);

    return output;
}
