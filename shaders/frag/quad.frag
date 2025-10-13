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
    // Accumulation Pass
    if (u_blend_factor > 0.0) {
        // --- FIX STARTS HERE ---
        // 1. Read the full RGBA color, not just RGB
        vec4 history = texture(accumulationTexture, TexCoords);
        vec4 current = texture(screenTexture, TexCoords);
        
        // 2. Mix the entire RGBA vector. This will blend the alpha channel too.
        FragColor = mix(history, current, u_blend_factor);
        // --- FIX ENDS HERE ---
        return;
    }

    // --- Final Display Pass ---
    // (This part runs only when drawing to the screen and is already correct)
    
    vec2 inv_res = 1.0 / u_resolution;
    
    vec4 color_center_rgba = texture(screenTexture, TexCoords);
    vec3 color_center = color_center_rgba.rgb;
    
    float luma_center = rgb_to_luma(color_center);
    // --- FXAA ---
    float luma_down = rgb_to_luma(texture(screenTexture, TexCoords + vec2(0.0, -inv_res.y)).rgb);
    float luma_up = rgb_to_luma(texture(screenTexture, TexCoords + vec2(0.0, inv_res.y)).rgb);
    float luma_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, 0.0)).rgb);
    float luma_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, 0.0)).rgb);
    float luma_min = min(luma_center, min(min(luma_down, luma_up), min(luma_left, luma_right)));
    float luma_max = max(luma_center, max(max(luma_down, luma_up), max(luma_left, luma_right)));
    
    vec3 final_rgb;
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
        if (luma_avg < luma_min || luma_avg > luma_max) {
            final_rgb = color_center;
        } else {
            final_rgb = blended_color;
        }
    }

    // --- Post-Processing ---
    final_rgb = (final_rgb - 0.5) * u_contrast + 0.5;
    final_rgb *= u_brightness;
    final_rgb = pow(final_rgb, vec3(1.0 / u_gamma));
    
    // The original alpha from the now-transparent accumulation buffer is preserved
    FragColor = vec4(clamp(final_rgb, 0.0, 1.0), color_center_rgba.a);
}
