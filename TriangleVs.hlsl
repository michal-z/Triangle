[RootSignature("RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)")]
float4 TriangleVs(float3 pos : POSITION) : SV_POSITION
{
	return float4(pos, 1.0f);
}
