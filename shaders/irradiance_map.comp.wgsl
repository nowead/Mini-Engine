@group(0) @binding(0) var envMap: texture_cube<f32>;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var outputIrradiance: texture_storage_2d_array<rgba16float, write>;

const PI: f32 = 3.14159265359;

fn getCubeDir(face: u32, uv: vec2<f32>) -> vec3<f32> {
    let st = uv * 2.0 - 1.0;
    switch (face) {
        case 0u: { return normalize(vec3<f32>( 1.0, -st.y, -st.x)); }
        case 1u: { return normalize(vec3<f32>(-1.0, -st.y,  st.x)); }
        case 2u: { return normalize(vec3<f32>( st.x,  1.0,  st.y)); }
        case 3u: { return normalize(vec3<f32>( st.x, -1.0, -st.y)); }
        case 4u: { return normalize(vec3<f32>( st.x, -st.y,  1.0)); }
        case 5u: { return normalize(vec3<f32>(-st.x, -st.y, -1.0)); }
        default: { return vec3<f32>(0.0); }
    }
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) globalID: vec3<u32>) {
    let size = textureDimensions(outputIrradiance);

    if (globalID.x >= size.x || globalID.y >= size.y || globalID.z >= 6u) {
        return;
    }

    let uv = (vec2<f32>(f32(globalID.x), f32(globalID.y)) + 0.5) / vec2<f32>(f32(size.x), f32(size.y));
    let normal = getCubeDir(globalID.z, uv);

    // Build tangent frame
    let up = select(vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 0.0, 1.0), abs(normal.z) < 0.999);
    let right = normalize(cross(up, normal));
    let upDir = cross(normal, right);

    var irradiance = vec3<f32>(0.0);
    let sampleDelta: f32 = 0.025;
    var nrSamples: f32 = 0.0;

    var phi: f32 = 0.0;
    loop {
        if (phi >= 2.0 * PI) { break; }
        var theta: f32 = 0.0;
        loop {
            if (theta >= 0.5 * PI) { break; }

            let tangentSample = vec3<f32>(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            let sampleVec = tangentSample.x * right + tangentSample.y * upDir + tangentSample.z * normal;

            irradiance += textureSampleLevel(envMap, envSampler, sampleVec, 0.0).rgb * cos(theta) * sin(theta);
            nrSamples += 1.0;

            theta += sampleDelta;
        }
        phi += sampleDelta;
    }

    irradiance = PI * irradiance / nrSamples;

    textureStore(outputIrradiance, vec2<u32>(globalID.xy), globalID.z, vec4<f32>(irradiance, 1.0));
}
