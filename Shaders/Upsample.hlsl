Texture2D SourceTexture : register(t0, space2);
SamplerState SourceSampler : register(s0, space2);

struct VsOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

VsOutput VsMain(uint VertexID : SV_VertexID)
{
    VsOutput Output;
    float X = (float)((VertexID << 1) & 2) * 2.0 - 1.0;
    float Y = (float)(VertexID & 2) * 2.0 - 1.0;
    
    Output.Position = float4(X, Y, 0.0, 1.0);
    Output.UV = float2(X * 0.5 + 0.5, 1.0 - (Y * 0.5 + 0.5));
    
    return Output;
}

float4 PsMain(VsOutput Input) : SV_Target0
{
    uint W, H;
    SourceTexture.GetDimensions(W, H);
    float x = 1.0 / (float)W;
    float y = 1.0 / (float)H;

    float3 a = SourceTexture.Sample(SourceSampler, float2(Input.UV.x - x, Input.UV.y + y)).rgb;
    float3 b = SourceTexture.Sample(SourceSampler, float2(Input.UV.x,     Input.UV.y + y)).rgb;
    float3 c = SourceTexture.Sample(SourceSampler, float2(Input.UV.x + x, Input.UV.y + y)).rgb;

    float3 d = SourceTexture.Sample(SourceSampler, float2(Input.UV.x - x, Input.UV.y)).rgb;
    float3 e = SourceTexture.Sample(SourceSampler, float2(Input.UV.x,     Input.UV.y)).rgb;
    float3 f = SourceTexture.Sample(SourceSampler, float2(Input.UV.x + x, Input.UV.y)).rgb;

    float3 g = SourceTexture.Sample(SourceSampler, float2(Input.UV.x - x, Input.UV.y - y)).rgb;
    float3 h = SourceTexture.Sample(SourceSampler, float2(Input.UV.x,     Input.UV.y - y)).rgb;
    float3 i = SourceTexture.Sample(SourceSampler, float2(Input.UV.x + x, Input.UV.y - y)).rgb;

    float3 Color = e * 0.25;
    Color += (b + d + f + h) * 0.125;
    Color += (a + c + g + i) * 0.0625;

    return float4(Color, 1.0);
}