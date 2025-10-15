#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

uniform sampler2D screenTexture; // The raw points from the current frame
uniform sampler2D accumulationTexture; // The accumulated history
uniform vec2 u_resolution;

// Post-processing uniforms
uniform float u_brightness;
uniform float u_contrast;
uniform float u_gamma;
uniform float u_blend_factor; // Controls accumulation

// --- Tunable FXAA Parameters ---
#define FXAA_CONTRAST_THRESHOLD 0.083

// Helper function
float rgb_to_luma(vec3 rgb) {
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main() {
    // Accumulation Pass (This branch is OK as it's based on a uniform)
    if (u_blend_factor > 0.0) {
        vec4 history = texture(accumulationTexture, TexCoords);
        vec4 current = texture(screenTexture, TexCoords);
        FragColor = mix(history, current, u_blend_factor);
        return;
    }

    // --- Final Display Pass ---
    vec2 inv_res = 1.0 / u_resolution;
    vec4 color_center_rgba = texture(screenTexture, TexCoords);
    vec3 color_center = color_center_rgba.rgb;
    float alpha = color_center_rgba.a;

    // --- Start of unconditional calculations ---
    // These calculations will now run for EVERY pixel. While it seems like more
    // work for background pixels, it's often much faster than stalling the GPU
    // with divergent branches.

    // --- FXAA Calculation ---
    // This block still contains a branch, but it's more spatially coherent
    // (edge pixels are usually next to other edge pixels) and less of a
    // performance issue than the original alpha test.
    vec3 final_rgb;
    { // local scope for FXAA variables
        float luma_center = rgb_to_luma(color_center);
        float luma_down = rgb_to_luma(texture(screenTexture, TexCoords + vec2(0.0, -inv_res.y)).rgb);
        float luma_up = rgb_to_luma(texture(screenTexture, TexCoords + vec2(0.0, inv_res.y)).rgb);
        float luma_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, 0.0)).rgb);
        float luma_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, 0.0)).rgb);
        float luma_min = min(luma_center, min(min(luma_down, luma_up), min(luma_left, luma_right)));
        float luma_max = max(luma_center, max(max(luma_down, luma_up), max(luma_left, luma_right)));
        
        if (luma_max - luma_min < luma_max * FXAA_CONTRAST_THRESHOLD) {
            final_rgb = color_center;
        } else {
            float luma_down_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, -inv_res.y)).rgb);
            float luma_up_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, inv_res.y)).rgb);
            float luma_up_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, inv_res.y)).rgb);
            float luma_down_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, -inv_res.y)).rgb);
            vec2 dir;
            dir.x = -((luma_up_left + luma_up_right) - (luma_down_left + luma_down_right));
            dir.y =  ((luma_up_left + luma_down_left) - (luma_up_right + luma_down_right));
            
            float dir_reduce = max((luma_down_left + luma_up_right + luma_up_left + luma_down_right) * 0.125, 1.0 / 16.0);
            float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
            
            dir = min(vec2(8.0), max(vec2(-8.0), dir * rcp_dir_min)) * inv_res;
            vec3 result1 = texture(screenTexture, TexCoords + dir * (1.0/3.0 - 0.5)).rgb;
            vec3 result2 = texture(screenTexture, TexCoords + dir * (2.0/3.0 - 0.5)).rgb;
            vec3 blended_color = (result1 + result2) * 0.5;
            float luma_avg = rgb_to_luma(blended_color);
            
            // Replace inner branch with branchless mix
            bool use_original = luma_avg < luma_min || luma_avg > luma_max;
            final_rgb = mix(blended_color, color_center, float(use_original));
        }
    }
    
    // --- Tone Mapping ---
    final_rgb = final_rgb / (final_rgb + vec3(1.0));
    
    // --- Post-Processing ---
    final_rgb = (final_rgb - 0.5) * u_contrast + 0.5;
    final_rgb *= u_brightness;
    final_rgb = pow(final_rgb, vec3(1.0 / u_gamma));
    
    // --- Final Selection (Branchless) ---
    // Use step() to get 0.0 for background pixels and 1.0 for visible pixels.
    float visibility = step(0.001, alpha);
    
    // Multiply the final result by the visibility factor.
    // This effectively zeroes out background pixels without a divergent `if`.
    FragColor = vec4(clamp(final_rgb, 0.0, 1.0), alpha) * visibility;
}
