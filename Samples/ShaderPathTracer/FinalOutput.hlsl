Texture2D TexHDR : register(t0);
SamplerState Smp : register(s0);

cbuffer PostProcessParams : register(b0) {
    float Exposure;    // Множитель яркости
    float3 Padding;    // Для выравнивания 16 байт
};

struct VS_OUTPUT {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VS_OUTPUT VS_Quad(float3 Pos : POSITION, float2 UV : TEXCOORD0) {
    VS_OUTPUT o; o.Pos = float4(Pos, 1); o.UV = UV; return o;
}

// ACES Tone Mapping (Knarkowicz) - "Кинематографичное" сжатие диапазона
float3 ACESFilm(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Точный перевод из Линейного пространства в sRGB (стандарт Rec.709 для мониторов)
// Лучше чем pow(x, 1/2.2), так как правильнее обрабатывает темные оттенки.
float3 LinearToSRGB(float3 x) {
    // Точная формула sRGB (piece-wise)
    // Если очень темно - линейный участок, иначе гамма-кривая 2.4
    return (x < 0.0031308f) ? (12.92f * x) : (1.055f * pow(abs(x), 1.0f / 2.4f) - 0.055f);
}

float4 PS_ToneMap(VS_OUTPUT input) : SV_Target{
    // 1. Читаем линейный HDR цвет
    float3 color = TexHDR.Sample(Smp, input.UV).rgb;

    // 2. Применяем ЭКСПОЗИЦИЮ (Exposure)
    // Это как "ISO" или "Выдержка" в фотоаппарате.
    color *= Exposure;

    // 3. Tone Mapping (HDR -> LDR [0..1])
    color = ACESFilm(color);

    // 4. Gamma Correction / Color Space Conversion
    // Монитор ожидает данные в sRGB/Rec.709 пространстве
    color = LinearToSRGB(color);

    return float4(color, 1.0f);
}
