#include <quantumvk.hpp>

#include <iostream>
#include <thread>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

// TODO currently invalid example

static void fb_size_cb(GLFWwindow* window, int width, int height);

struct GLFWPlatform : public Vulkan::WSIPlatform
{

	GLFWPlatform()
	{
		width = 1280;
		height = 720;
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		window = glfwCreateWindow(width, height, "GLFW Window", nullptr, nullptr);

		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, fb_size_cb);
	}

	virtual ~GLFWPlatform() 
	{
		if (window)
			glfwDestroyWindow(window);
	}

	virtual VkSurfaceKHR CreateSurface(VkInstance instance, VkPhysicalDevice gpu)
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
			return VK_NULL_HANDLE;

		int actual_width, actual_height;
		glfwGetFramebufferSize(window, &actual_width, &actual_height);
		width = unsigned(actual_width);
		height = unsigned(actual_height);
		return surface;
	}

	virtual std::vector<const char*> GetInstanceExtensions()
	{
		uint32_t count;
		const char** ext = glfwGetRequiredInstanceExtensions(&count);
		return { ext, ext + count };
	}

	virtual uint32_t GetSurfaceWidth()
	{
		return width;
	}

	virtual uint32_t GetSurfaceHeight()
	{
		return height;
	}

	virtual bool Alive(Vulkan::WSI& wsi)
	{
		return !glfwWindowShouldClose(window);
	}

	virtual void PollInput()
	{
		glfwPollEvents();
	}

	void NotifyResize(int width_, int height_)
	{
		resize = true;
		width = static_cast<uint32_t>(width_);
		height = static_cast<uint32_t>(height_);
	}

private:

	GLFWwindow* window = nullptr;
	unsigned width = 0;
	unsigned height = 0;

};

static void fb_size_cb(GLFWwindow* window, int width, int height)
{
	auto* glfw = static_cast<GLFWPlatform*>(glfwGetWindowUserPointer(window));
	glfw->NotifyResize(width, height);
}

int main() 
{
	glfwInit();

	if (!Vulkan::Context::InitLoader(nullptr))
		QM_LOG_ERROR("Failed to load vulkan dynamic library");

	{
		GLFWPlatform platform;

		Vulkan::WSI wsi;
		wsi.SetPlatform(&platform);
		wsi.SetBackbufferSrgb(true);
		wsi.Init(1, nullptr, 0);

		{
			Vulkan::Device& device = wsi.GetDevice();


			const char* vertex_code = R"(
#version 450

layout(location = 0) in vec2 in_pos;

layout(location = 0) out vec2 frag_pos;

void main()
{

	gl_Position = vec4(in_pos, 0.0, 1.0);
	frag_pos = in_pos;

}
)";
			Vulkan::ShaderHandle vert_shader = device.CreateShaderGLSL(vertex_code, Vulkan::ShaderStage::Vertex);


			const char* frag_code = R"(
#version 450

//	Simplex 4D Noise 
//	by Ian McEwan, Ashima Arts
//
vec4 permute(vec4 x){return mod(((x*34.0)+1.0)*x, 289.0);}
float permute(float x){return floor(mod(((x*34.0)+1.0)*x, 289.0));}
vec4 taylorInvSqrt(vec4 r){return 1.79284291400159 - 0.85373472095314 * r;}
float taylorInvSqrt(float r){return 1.79284291400159 - 0.85373472095314 * r;}

vec4 grad4(float j, vec4 ip){
  const vec4 ones = vec4(1.0, 1.0, 1.0, -1.0);
  vec4 p,s;

  p.xyz = floor( fract (vec3(j) * ip.xyz) * 7.0) * ip.z - 1.0;
  p.w = 1.5 - dot(abs(p.xyz), ones.xyz);
  s = vec4(lessThan(p, vec4(0.0)));
  p.xyz = p.xyz + (s.xyz*2.0 - 1.0) * s.www; 

  return p;
}

float snoise(vec4 v){
  const vec2  C = vec2( 0.138196601125010504,  // (5 - sqrt(5))/20  G4
                        0.309016994374947451); // (sqrt(5) - 1)/4   F4
// First corner
  vec4 i  = floor(v + dot(v, C.yyyy) );
  vec4 x0 = v -   i + dot(i, C.xxxx);

// Other corners

// Rank sorting originally contributed by Bill Licea-Kane, AMD (formerly ATI)
  vec4 i0;

  vec3 isX = step( x0.yzw, x0.xxx );
  vec3 isYZ = step( x0.zww, x0.yyz );
//  i0.x = dot( isX, vec3( 1.0 ) );
  i0.x = isX.x + isX.y + isX.z;
  i0.yzw = 1.0 - isX;

//  i0.y += dot( isYZ.xy, vec2( 1.0 ) );
  i0.y += isYZ.x + isYZ.y;
  i0.zw += 1.0 - isYZ.xy;

  i0.z += isYZ.z;
  i0.w += 1.0 - isYZ.z;

  // i0 now contains the unique values 0,1,2,3 in each channel
  vec4 i3 = clamp( i0, 0.0, 1.0 );
  vec4 i2 = clamp( i0-1.0, 0.0, 1.0 );
  vec4 i1 = clamp( i0-2.0, 0.0, 1.0 );

  //  x0 = x0 - 0.0 + 0.0 * C 
  vec4 x1 = x0 - i1 + 1.0 * C.xxxx;
  vec4 x2 = x0 - i2 + 2.0 * C.xxxx;
  vec4 x3 = x0 - i3 + 3.0 * C.xxxx;
  vec4 x4 = x0 - 1.0 + 4.0 * C.xxxx;

// Permutations
  i = mod(i, 289.0); 
  float j0 = permute( permute( permute( permute(i.w) + i.z) + i.y) + i.x);
  vec4 j1 = permute( permute( permute( permute (
             i.w + vec4(i1.w, i2.w, i3.w, 1.0 ))
           + i.z + vec4(i1.z, i2.z, i3.z, 1.0 ))
           + i.y + vec4(i1.y, i2.y, i3.y, 1.0 ))
           + i.x + vec4(i1.x, i2.x, i3.x, 1.0 ));
// Gradients
// ( 7*7*6 points uniformly over a cube, mapped onto a 4-octahedron.)
// 7*7*6 = 294, which is close to the ring size 17*17 = 289.

  vec4 ip = vec4(1.0/294.0, 1.0/49.0, 1.0/7.0, 0.0) ;

  vec4 p0 = grad4(j0,   ip);
  vec4 p1 = grad4(j1.x, ip);
  vec4 p2 = grad4(j1.y, ip);
  vec4 p3 = grad4(j1.z, ip);
  vec4 p4 = grad4(j1.w, ip);

// Normalise gradients
  vec4 norm = taylorInvSqrt(vec4(dot(p0,p0), dot(p1,p1), dot(p2, p2), dot(p3,p3)));
  p0 *= norm.x;
  p1 *= norm.y;
  p2 *= norm.z;
  p3 *= norm.w;
  p4 *= taylorInvSqrt(dot(p4,p4));

// Mix contributions from the five corners
  vec3 m0 = max(0.6 - vec3(dot(x0,x0), dot(x1,x1), dot(x2,x2)), 0.0);
  vec2 m1 = max(0.6 - vec2(dot(x3,x3), dot(x4,x4)            ), 0.0);
  m0 = m0 * m0;
  m1 = m1 * m1;
  return 49.0 * ( dot(m0*m0, vec3( dot( p0, x0 ), dot( p1, x1 ), dot( p2, x2 )))
               + dot(m1*m1, vec2( dot( p3, x3 ), dot( p4, x4 ) ) ) ) ;

}

 float noise(vec4 position, int octaves, float frequency, float persistence) {
    float total = 0.0; // Total value so far
    float maxAmplitude = 0.0; // Accumulates highest theoretical amplitude
    float amplitude = 1.0;
    for (int i = 0; i < octaves; i++) {

        // Get the noise sample
        total += snoise(position * frequency) * amplitude;

        // Make the wavelength twice as small
        frequency *= 2.0;

        // Add to our maximum possible amplitude
        maxAmplitude += amplitude;

        // Reduce amplitude according to persistence for the next octave
        amplitude *= persistence;
    }

    // Scale the result by the maximum amplitude
    return total / maxAmplitude;
}

// All components are in the range [0…1], including hue.
vec3 hsv_to_rgb(vec3 c)
{
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

layout(location = 0) in vec2 frag_pos;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform UBO 
{
	float hue;
	float variance;
	float x_offset;
	float t;
	
} ubo;

void main()
{

	float offset_x = noise(vec4(frag_pos, 1.0, ubo.t/ 100.0), 3, 3, 0.8) / 10.0 + ubo.x_offset;
	float offset_y = noise(vec4(frag_pos, 10.0, ubo.t/ 100.0), 3, 3, 0.8) / 10.0;

	float n = abs(noise(vec4(frag_pos.x + offset_x, frag_pos.y + offset_y, -1.0, ubo.t/20.0), 5, 2, 0.5));

	float act_hue = ubo.hue + n * ubo.variance;

	if(act_hue > 1.0)
		act_hue -= 1.0;
	else if(act_hue < 0.0)
		act_hue += 1.0;

	vec3 color = hsv_to_rgb(vec3(act_hue, 1, .6));

	out_color = vec4(color, 1.0);
}
)";
			Vulkan::ShaderHandle frag_shader = device.CreateShaderGLSL(frag_code, Vulkan::ShaderStage::Fragment);

			Vulkan::GraphicsProgramShaders p_shaders;
			p_shaders.vertex = vert_shader;
			p_shaders.fragment = frag_shader;

			Vulkan::ProgramHandle program = device.CreateGraphicsProgram(p_shaders);

			float current_time = 0;
			float current_delta = 1.0f/60.0f;
			float current_hue = 0.1f;
			float current_target = 0.1f;

			std::srand(100);

			while (platform.Alive(wsi))
			{

				Util::Timer timer;
				timer.start();

				wsi.BeginFrame();
				{
					auto cmd = device.RequestCommandBuffer();

					// Just render a clear color to screen.
					// There is a lot of stuff going on in these few calls which will need its own sample to explore w.r.t. synchronization.
					// For now, you'll just get a blue-ish color on screen.
					Vulkan::RenderPassInfo rp = device.GetSwapchainRenderPass(Vulkan::SwapchainRenderPass::ColorOnly);
					rp.clear_color[0].float32[0] = 0.1f;
					rp.clear_color[0].float32[1] = 0.2f;
					rp.clear_color[0].float32[2] = 0.3f;
					cmd->BeginRenderPass(rp);

					cmd->SetOpaqueState();

					cmd->SetProgram(program);
					cmd->SetVertexAttrib(0, 0, VK_FORMAT_R32G32_SFLOAT, 0);
					cmd->SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
					cmd->SetCullMode(VK_CULL_MODE_NONE);

					float vert_data[12] = { -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f,
											1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f };


					void* vertex_data = cmd->AllocateVertexData(0, sizeof(vert_data), sizeof(float) * 2);
					memcpy(vertex_data, vert_data, sizeof(vert_data));

					if ((float)std::rand() / (float)RAND_MAX > .993f)
					{
						QM_LOG_TRACE("CHANGE\n");
						current_target = (float)std::rand() / (float)RAND_MAX;
					}

					float dist = current_target - current_hue;
					
					current_hue += dist * current_delta * 0.5f;

					float unif_data[4] = { current_hue, 0.3f , current_time / 10.0f, current_time };
					void* uniform_data = cmd->AllocateConstantData(0, 0, sizeof(float) * 4);
					memcpy(uniform_data, unif_data, sizeof(float) * 4);

					cmd->Draw(6);

					cmd->EndRenderPass();
					device.Submit(cmd);
				}

				wsi.EndFrame();

				current_delta = timer.end();
				current_time += current_delta;


				//QM_LOG_INFO("Frame time (ms): %f\n", time_milli);
			}
		}

	}

	glfwTerminate();
	
}