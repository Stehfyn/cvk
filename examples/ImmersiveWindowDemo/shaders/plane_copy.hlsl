float4 VS(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}

Texture2D<float2> plane : register(t0);

float4 PS(float4 p : SV_Position) : SV_Target
{
    return float4(plane.Load(int3(int2(p.xy), 0)), 0, 1);
}
