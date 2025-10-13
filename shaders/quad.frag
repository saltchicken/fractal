#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D screenTexture;
uniform vec2 u_resolution;

// --- Tunable FXAA Parameters ---
// Lower this to apply AA to lower contrast edges (more aggressive).
// Good values are between 0.06 and 0.125.
#define FXAA_CONTRAST_THRESHOLD 2.083

// --- End of Parameters ---

// Converts RGB to perceived luminance
float rgb_to_luma(vec3 rgb) {
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

void main() {
    vec2 inv_res = 1.0 / u_resolution;

    // --- FXAA ---
    // 1. Sample the center pixel and its neighbors
    vec3 color_center = texture(screenTexture, TexCoords).rgb;
    float luma_center = rgb_to_luma(color_center);

    float luma_down = rgb_to_luma(texture(screenTexture, TexCoords + vec2(0.0, -inv_res.y)).rgb);
    float luma_up = rgb_to_luma(texture(screenTexture, TexCoords + vec2(0.0, inv_res.y)).rgb);
    float luma_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, 0.0)).rgb);
    float luma_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, 0.0)).rgb);

    // 2. Find the direction of the steepest gradient (the edge)
    float luma_min = min(luma_center, min(min(luma_down, luma_up), min(luma_left, luma_right)));
    float luma_max = max(luma_center, max(max(luma_down, luma_up), max(luma_left, luma_right)));
    
    // 3. If contrast is too low, it's not an edge, so exit
    if (luma_max - luma_min < luma_max * FXAA_CONTRAST_THRESHOLD) {
        FragColor = vec4(color_center, 1.0);
        return;
    }

    // 4. Sample corner pixels
    float luma_down_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, -inv_res.y)).rgb);
    float luma_up_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, inv_res.y)).rgb);
    float luma_up_left = rgb_to_luma(texture(screenTexture, TexCoords + vec2(-inv_res.x, inv_res.y)).rgb);
    float luma_down_right = rgb_to_luma(texture(screenTexture, TexCoords + vec2(inv_res.x, -inv_res.y)).rgb);

    // 5. Determine edge direction and blend amount
    vec2 dir;
    dir.x = -((luma_up_left + luma_up_right) - (luma_down_left + luma_down_right));
    dir.y =  ((luma_up_left + luma_down_left) - (luma_up_right + luma_down_right));
    
    float dir_reduce = max((luma_down_left + luma_up_right + luma_up_left + luma_down_right) * (0.25 * 0.5), 1.0 / 16.0);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    
    dir = min(vec2(8.0), max(vec2(-8.0), dir * rcp_dir_min)) * inv_res;

    // 6. Sample along the calculated direction and average
    vec3 result1 = texture(screenTexture, TexCoords + dir * (1.0/3.0 - 0.5)).rgb;
    vec3 result2 = texture(screenTexture, TexCoords + dir * (2.0/3.0 - 0.5)).rgb;
    vec3 blended_color = (result1 + result2) * 0.5;

    // 7. Choose the final color based on which is closer to the center pixel's luma
    float luma_avg = rgb_to_luma(blended_color);
    if (luma_avg < luma_min || luma_avg > luma_max) {
        FragColor = vec4(color_center, 1.0);
    } else {
        FragColor = vec4(blended_color, 1.0);
    }
}
