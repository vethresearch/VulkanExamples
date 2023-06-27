#version 450

layout (binding = 0) uniform sampler2D samplerColor;

layout (location = 0) in vec2 inUV;
// layout (location = 1) in float inLodBias;
// layout (location = 2) in vec3 inNormal;
// layout (location = 3) in vec3 inViewVec;
// layout (location = 4) in vec3 inLightVec;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec4 color = texture(samplerColor, inUV, 0.0);

	// vec3 N = normalize(inNormal);
	// vec3 L = normalize(inLightVec);
	// vec3 V = normalize(inViewVec);
	// vec3 R = reflect(-L, N);
	// vec3 diffuse = max(dot(N, L), 0.0) * vec3(1.0);
	// float specular = pow(max(dot(R, V), 0.0), 16.0) * color.a;

	// outFragColor = vec4(0.0, 1.0, 0.0, 1.0);	// Force green

	outFragColor = color;	// Force raw sampler color
	// outFragColor = vec4(inUV, 0.0f, 1.0f);	// Force to uv value

}