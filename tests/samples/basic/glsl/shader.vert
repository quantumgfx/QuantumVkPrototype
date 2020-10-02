#version 450

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec3 in_col;

layout(location = 0) out vec3 frag_col;

layout(set = 0, binding = 0) uniform UBO {
	
	vec2 offset;

} off;


void main()
{

	gl_Position = vec4(in_pos.x + off.offset.x, in_pos.y + off.offset.y, 0.0, 1.0);
	frag_col = in_col;

}