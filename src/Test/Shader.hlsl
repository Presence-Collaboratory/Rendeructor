// Simple test shader

cbuffer Constants : register(b0) {
    row_major float4x4 mvp;
    float time;
};

Texture2D colorTex : register(t0);
Texture2D renderTex : register(t0);
SamplerState linearSampler : register(s0);

struct VS_IN {
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VS_OUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

// Простой вершинный шейдер
VS_OUT VS_Simple(VS_IN input) {
    VS_OUT output;
    output.pos = mul(float4(input.pos, 1.0), mvp);
    output.uv = input.uv;
    return output;
}

// Fullscreen вершинный шейдер
VS_OUT VS_Fullscreen(VS_IN input) {
    VS_OUT output;
    output.pos = float4(input.pos, 1.0); // Уже в NDC
    output.uv = input.uv;
    return output;
}

// Простой пиксельный шейдер - цветной градиент
float4 PS_Simple(VS_OUT input) : SV_Target{
    // Простой цветной градиент для теста
    float3 color = float3(
        0.5 + 0.5 * sin(time + input.uv.x * 3.14),
        0.5 + 0.5 * cos(time + input.uv.y * 3.14),
        0.5 + 0.5 * sin(time * 0.7 + (input.uv.x + input.uv.y) * 1.57)
    );

// Добавляем немного текстуры
float4 texColor = colorTex.Sample(linearSampler, input.uv * 2.0);

return float4(color * 0.7 + texColor.rgb * 0.3, 1.0);
}

// Fullscreen пиксельный шейдер - просто отображаем текстуру
float4 PS_Fullscreen(VS_OUT input) : SV_Target{
    // Просто отображаем текстуру
    float4 color = renderTex.Sample(linearSampler, input.uv);

    // Добавляем простой эффект виньетки
    float2 uvCentered = input.uv * 2.0 - 1.0;
    float vignette = 1.0 - dot(uvCentered, uvCentered) * 0.3;

    return color * vignette;
}
