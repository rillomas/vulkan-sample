// VulkanSample.cpp : アプリケーションのエントリ ポイントを定義します。
//

#include <iostream>
#include <optional>
#include <ranges>
#include <set>
#include <filesystem>
#include <fstream>
#include "framework.h"
#include "VulkanSample.h"
#include <vulkan/vulkan.hpp>

#define MAX_LOADSTRING 100

// グローバル変数:
HINSTANCE hInst;                                // 現在のインターフェイス
WCHAR szTitle[MAX_LOADSTRING];                  // タイトル バーのテキスト
WCHAR szWindowClass[MAX_LOADSTRING];            // メイン ウィンドウ クラス名

// このコード モジュールに含まれる関数の宣言を転送します:
ATOM                MyRegisterClass(HINSTANCE hInstance);
std::optional<HWND> InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

template<class T>
static bool contains(std::vector<T> list, T target) {
	return std::find(list.begin(), list.end(), target) != list.end();
}

static uint32_t ChooseImageCount(vk::SurfaceCapabilitiesKHR capabilities) {
	auto imgCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imgCount > capabilities.maxImageCount) {
		imgCount = capabilities.maxImageCount;
	}
	return imgCount;
}


struct ResourcePerFrame {
	vk::UniqueSemaphore imageAvailable;
	vk::UniqueSemaphore renderFinished;
	vk::UniqueFence inFlight;
};

struct SwapchainSupportDetails {
	vk::SurfaceCapabilitiesKHR capabilities;
	std::vector<vk::SurfaceFormatKHR> formats;
	std::vector<vk::PresentModeKHR> presentModes;

	bool SwapchainAdequate(vk::SurfaceFormatKHR targetFormat, vk::PresentModeKHR targetMode) const {
		return contains(formats, targetFormat) && contains(presentModes, targetMode);
	}
};


struct DeviceAndIndex {
	vk::PhysicalDevice device;
	uint32_t graphicsIndex;
	uint32_t presentIndex;

	std::vector<vk::DeviceQueueCreateInfo> GetQueueCreateInfoList(float* priority) const {
		std::vector<vk::DeviceQueueCreateInfo> qInfoList;
		vk::DeviceQueueCreateInfo qinfo;
		qinfo.queueFamilyIndex = graphicsIndex;
		qinfo.queueCount = 1;
		qinfo.pQueuePriorities = priority;
		qInfoList.push_back(qinfo);
		if (graphicsIndex != presentIndex) {
			// We add two queue info only if they have different indices
			qinfo.queueFamilyIndex = presentIndex;
			qInfoList.push_back(qinfo);
		}
		return qInfoList;
	}

	SwapchainSupportDetails GetSwapchainSupportDetails(vk::UniqueSurfaceKHR& surface) const {
		SwapchainSupportDetails details = {
			device.getSurfaceCapabilitiesKHR(surface.get()),
			device.getSurfaceFormatsKHR(surface.get()),
			device.getSurfacePresentModesKHR(surface.get())
		};
		return details;
	}
};


struct SwapchainResources {
	vk::UniqueSwapchainKHR swapchain;
	std::vector<vk::UniqueFramebuffer> frameBuffers;
	std::vector<vk::UniqueImageView> imageViews;
	void Cleanup() {
		frameBuffers.clear();
		imageViews.clear();
		swapchain.reset();
	}
	void Init(
		vk::UniqueDevice& device,
		vk::Extent2D extent,
		vk::UniqueSurfaceKHR& surface,
		vk::SurfaceFormatKHR targetFormat,
		vk::SurfaceCapabilitiesKHR capabilities,
		vk::PresentModeKHR targetMode,
		vk::UniqueRenderPass& renderPass,
		DeviceAndIndex targetDevice) {
		vk::SwapchainCreateInfoKHR chainInfo;
		chainInfo.surface = surface.get();
		chainInfo.minImageCount = ChooseImageCount(capabilities);
		chainInfo.imageFormat = targetFormat.format;
		chainInfo.imageColorSpace = targetFormat.colorSpace;
		chainInfo.imageExtent = extent;
		chainInfo.imageArrayLayers = 1;
		chainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
		std::vector<uint32_t> queueFamilyIndices = { targetDevice.graphicsIndex, targetDevice.presentIndex };
		if (targetDevice.graphicsIndex == targetDevice.presentIndex) {
			chainInfo.imageSharingMode = vk::SharingMode::eExclusive;
			chainInfo.queueFamilyIndexCount = 0;
			chainInfo.pQueueFamilyIndices = nullptr;
		}
		else {
			chainInfo.imageSharingMode = vk::SharingMode::eConcurrent;
			chainInfo.queueFamilyIndexCount = (uint32_t)queueFamilyIndices.size();
			chainInfo.pQueueFamilyIndices = queueFamilyIndices.data();
		}
		chainInfo.preTransform = capabilities.currentTransform;
		chainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		chainInfo.presentMode = targetMode;
		chainInfo.clipped = true;
		chainInfo.oldSwapchain = nullptr;
		swapchain = device->createSwapchainKHRUnique(chainInfo);

		auto swapchainImages = device->getSwapchainImagesKHR(swapchain.get());
		imageViews.resize(swapchainImages.size());
		for (size_t i = 0; i < swapchainImages.size(); i++) {
			vk::ImageViewCreateInfo ci;
			ci.image = swapchainImages[i];
			ci.viewType = vk::ImageViewType::e2D;
			ci.format = targetFormat.format;
			ci.components.r = vk::ComponentSwizzle::eIdentity;
			ci.components.g = vk::ComponentSwizzle::eIdentity;
			ci.components.b = vk::ComponentSwizzle::eIdentity;
			ci.components.a = vk::ComponentSwizzle::eIdentity;
			ci.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			ci.subresourceRange.baseMipLevel = 0;
			ci.subresourceRange.levelCount = 1;
			ci.subresourceRange.baseArrayLayer = 0;
			ci.subresourceRange.layerCount = 1;
			imageViews[i] = device->createImageViewUnique(ci);
		}

		frameBuffers.resize(imageViews.size());

		for (size_t i = 0; i < frameBuffers.size(); i++) {
			vk::ImageView attachments[] = { imageViews[i].get() };
			vk::FramebufferCreateInfo fbci;
			fbci.renderPass = renderPass.get();
			fbci.attachmentCount = 1;
			fbci.pAttachments = attachments;
			fbci.width = extent.width;
			fbci.height = extent.height;
			fbci.layers = 1;
			frameBuffers[i] = device->createFramebufferUnique(fbci);
		}
	}
};

static std::vector <std::string> GetInstanceLayers(std::vector<std::string>& layerCandidate) {
	std::vector<std::string> layersInUse;
	if (layerCandidate.empty()) {
		return layersInUse;
	}
	// Check for debug layer and use it if available
	auto layerList = vk::enumerateInstanceLayerProperties();
	for (const auto& l : layerList) {
		std::cout << l.layerName << std::endl;
		if (std::find(layerCandidate.begin(), layerCandidate.end(), l.layerName) != layerCandidate.end()) {
			// found target layer
			layersInUse.push_back(l.layerName);
		}
	}
	return layersInUse;
}

static vk::UniqueInstance CreateInstance(const char* appName, std::vector<const char*>& layers, std::vector<const char*> extensions) {
	auto extensionList = vk::enumerateInstanceExtensionProperties();
	for (const auto& ex : extensionList) {
		std::cout << ex.extensionName << std::endl;
	}

	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = appName;
	appInfo.applicationVersion = VK_MAKE_VERSION(0, 0, 1);
	appInfo.pEngineName = "MyEngine";
	appInfo.engineVersion = VK_MAKE_VERSION(0, 0, 1);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	vk::InstanceCreateInfo createInfo;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledLayerCount = (uint32_t)layers.size();
	createInfo.ppEnabledLayerNames = layers.data();
	createInfo.enabledExtensionCount = (uint32_t)extensions.size();
	createInfo.ppEnabledExtensionNames = extensions.data();
	return vk::createInstanceUnique(createInfo);
}

void CreateConsole() {
	FILE* fp;
	AllocConsole();
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
}

vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR capabilities) {
	return capabilities.currentExtent;
}

std::optional<DeviceAndIndex> GetSufficientDevice(vk::UniqueInstance& instance, vk::UniqueSurfaceKHR& surface, const std::vector<const char*>& requiredExtensions) {
	auto devices = instance->enumeratePhysicalDevices();
	if (devices.size() <= 0) {
		throw std::runtime_error("Cannot find physical device");
	}
	for (const auto& d : devices) {
		// Check if all required extensions are supported on this device
		auto eprops = d.enumerateDeviceExtensionProperties();
		std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());
		for (const auto& e : eprops) {
			required.erase(e.extensionName);
		}
		if (!required.empty()) {
			// Device couldn't fulfill all required extensions so we skip and go next
			continue;
		}

		auto qprops = d.getQueueFamilyProperties();
		std::optional<uint32_t> graphicsIndex;
		std::optional<uint32_t> presentIndex;
		uint32_t index = 0;
		for (const auto& qp : qprops) {
			// queue must support graphics, compute and transfer
			auto graphicsCapable = (qp.queueFlags & vk::QueueFlagBits::eGraphics) &&
				(qp.queueFlags & vk::QueueFlagBits::eCompute) &&
				(qp.queueFlags & vk::QueueFlagBits::eTransfer);
			if (graphicsCapable) {
				graphicsIndex = index;
			}
			// queue must support presentation
			if (d.getSurfaceSupportKHR(index, surface.get())) {
				presentIndex = index;
			}

			// If we find both queues from a single device we accept it
			if (graphicsIndex.has_value() && presentIndex.has_value()) {
				DeviceAndIndex dai = {
					d,
					graphicsIndex.value(),
					presentIndex.value()
				};
				return dai;
			}
			index++;
		}
	}
	return std::nullopt;
}
std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
	std::ifstream file(path.string(), std::ios::ate | std::ios::binary);
	auto size = file.tellg();
	std::vector<uint8_t> buffer(size);
	file.seekg(0);
	file.read((char*)buffer.data(), size);
	file.close();
	return buffer;
}

vk::UniqueShaderModule CreateShaderModule(vk::UniqueDevice& device, const std::vector<uint8_t>& code) {
	vk::ShaderModuleCreateInfo shaderInfo;
	shaderInfo.codeSize = (uint32_t)code.size();
	shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
	return device->createShaderModuleUnique(shaderInfo);
}

void RecordCommandBuffer(
	vk::CommandBuffer& cb,
	vk::UniqueRenderPass& renderPass,
	vk::UniqueFramebuffer& framebuffer,
	vk::Extent2D extent,
	vk::UniquePipeline& pipeline) {
	vk::CommandBufferBeginInfo cbbi;
	cb.begin(cbbi);
	vk::RenderPassBeginInfo rpbi;
	rpbi.renderPass = renderPass.get();
	rpbi.framebuffer = framebuffer.get();
	rpbi.renderArea.offset = vk::Offset2D(0, 0);
	rpbi.renderArea.extent = extent;
	rpbi.clearValueCount = 1;
	vk::ClearValue clearColor(vk::ClearColorValue(0, 0, 0, 1));
	rpbi.pClearValues = &clearColor;
	cb.beginRenderPass(rpbi, vk::SubpassContents::eInline);
	cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.get());
	cb.setViewport(0, vk::Viewport(0, 0, (float)extent.width, (float)extent.height, 0, 1));
	cb.setScissor(0, vk::Rect2D({ 0,0 }, extent));
	cb.draw(3, 1, 0, 0);
	cb.endRenderPass();
	cb.end();
}

enum WindowState {
	RESTORED,
	MINIMIZED,
	MAXIMIZED,
};

class EventDetector {
public:
	EventDetector(uint32_t width, uint32_t height) :
		_state(RESTORED),
		_width(width),
		_height(height),
		_resized(false),
		_resizing(false) {}

	bool Resized() const {
		return _resized;
	}
	vk::Extent2D Extent() const {
		return vk::Extent2D(_width, _height);
	}
	bool AreaIsZero() const {
		return _width == 0 || _height == 0;
	}

	void UpdateSize(uint32_t width, uint32_t height) {
		if (_width == width && _height == height) {
			// Don't need to resize
			return;
		}
		_width = width;
		_height = height;
		_resized = true;
	}

	void EnterResize() {
		_resizing = true;
	}

	void SetState(WindowState state) {
		_state = state;
	}

	void ExitResize() {
		_resizing = false;
	}
	void ResetResize() {
		_resizing = false;
		_resized = false;
	}

private:
	WindowState _state;
	bool _resizing;
	bool _resized;
	uint32_t _width;
	uint32_t _height;
};

std::optional<uint32_t> findMemoryTypeIndex(
	vk::PhysicalDevice& device,
	const vk::MemoryRequirements& requirement,
	const vk::MemoryPropertyFlags& property) {
	auto memoryProps = device.getMemoryProperties();
	for (uint32_t idx = 0; idx < memoryProps.memoryTypeCount; idx++) {
		auto flags = memoryProps.memoryTypes[idx].propertyFlags;
		auto matches = (flags & property) == property;
		if (requirement.memoryTypeBits & (1 << idx) && matches) {
			return idx;
		}
	}
	return std::nullopt;
}

template<class T>
struct StorageBuffer {
	StorageBuffer(vk::UniqueDevice& device, vk::PhysicalDevice& physicalDevice, const std::vector<T>& data) {
		memorySize = sizeof(T) * data.size();
		vk::BufferCreateInfo bci;
		bci.size = memorySize;
		bci.usage = vk::BufferUsageFlagBits::eStorageBuffer;
		buffer = device->createBufferUnique(bci);

		auto requirement = device->getBufferMemoryRequirements(buffer.get());
		auto memoryTypeIndex = findMemoryTypeIndex(physicalDevice, requirement, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		if (!memoryTypeIndex.has_value()) {
			throw std::runtime_error("Failed to find sufficient memory type index");
		}
		vk::MemoryAllocateInfo mai;
		mai.allocationSize = requirement.size;
		mai.memoryTypeIndex = memoryTypeIndex.value();
		memory = device->allocateMemoryUnique(mai);
		device->bindBufferMemory(buffer.get(), memory.get(), 0);
		mapped = reinterpret_cast<T*>(device->mapMemory(memory.get(), 0, memorySize));
		std::copy(data.begin(), data.end(), mapped);
	}
	T* mapped;
	size_t memorySize;
	vk::UniqueBuffer buffer;
	vk::UniqueDeviceMemory memory;
};

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	CreateConsole();

	// グローバル文字列を初期化する
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_VULKANSAMPLE, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// アプリケーション初期化の実行:
	auto hwnd = InitInstance(hInstance, nCmdShow);
	if (!hwnd.has_value())
	{
		std::cerr << "Failed to create window" << std::endl;
		return FALSE;
	}
	constexpr size_t TITLE_SIZE = MAX_LOADSTRING * sizeof(WCHAR);
	char windowTitle[TITLE_SIZE];
	size_t size;
	wcstombs_s(&size, windowTitle, TITLE_SIZE, szTitle, MAX_LOADSTRING - 1);

	std::vector<std::string> layerCandidate;
#ifdef _DEBUG
	layerCandidate.push_back("VK_LAYER_KHRONOS_validation");
#endif
	auto layers = GetInstanceLayers(layerCandidate);
	std::vector<const char*> actualLayers;
	actualLayers.reserve(layers.size());
	for (auto& c : layers) {
		actualLayers.push_back(c.c_str());
	}
	std::vector<const char*> extensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};
	auto instance = CreateInstance(windowTitle, actualLayers, extensions);

	vk::Win32SurfaceCreateInfoKHR win32info;
	win32info.hwnd = hwnd.value();
	win32info.hinstance = hInstance;
	auto surface = instance->createWin32SurfaceKHRUnique(win32info);

	std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	};
	auto targetDevice = GetSufficientDevice(instance, surface, deviceExtensions);
	if (!targetDevice.has_value()) {
		std::cerr << "Could not find sufficient device" << std::endl;
		return false;
	}

	float priority = 1.0f;
	auto qInfoList = targetDevice->GetQueueCreateInfoList(&priority);
	auto details = targetDevice->GetSwapchainSupportDetails(surface);
	auto targetFormat = vk::SurfaceFormatKHR(vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear);
	auto targetMode = vk::PresentModeKHR::eFifo;
	if (!details.SwapchainAdequate(targetFormat, targetMode)) {
		std::cerr << "Could not find sufficient swap chain" << std::endl;
		return false;
	}
	vk::PhysicalDeviceFeatures deviceFeature;
	vk::DeviceCreateInfo info;
	info.setQueueCreateInfos(qInfoList);
	info.pEnabledFeatures = &deviceFeature;
	info.setPEnabledLayerNames(actualLayers);
	info.setPEnabledExtensionNames(deviceExtensions);
	auto device = targetDevice->device.createDeviceUnique(info);
	auto presentQueue = device->getQueue(targetDevice->presentIndex, 0);
	auto extent = ChooseSwapExtent(details.capabilities);

	// Create shaders
	auto fragmentCode = ::ReadFile("fragment.spv");
	auto vertexCode = ::ReadFile("vertex.spv");
	auto computeCode = ::ReadFile("compute.spv");

	auto fragment = CreateShaderModule(device, fragmentCode);
	auto vertex = CreateShaderModule(device, vertexCode);
	auto compute = CreateShaderModule(device, computeCode);

	vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
	vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
	vertShaderStageInfo.module = vertex.get();
	vertShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
	fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
	fragShaderStageInfo.module = fragment.get();
	fragShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo computeShaderStageInfo;
	computeShaderStageInfo.stage = vk::ShaderStageFlagBits::eCompute;
	computeShaderStageInfo.module = compute.get();
	computeShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo stages[] = { vertShaderStageInfo, fragShaderStageInfo };

	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
	inputAssembly.primitiveRestartEnable = false;

	vk::Viewport viewport(0.0f, 0.0f, (float)extent.width, (float)extent.height, 0.0f, 1.0f);
	vk::Rect2D scissor(vk::Offset2D(0, 0), extent);
	std::vector<vk::DynamicState> dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};
	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState.dynamicStateCount = (uint32_t)dynamicStates.size();
	dynamicState.pDynamicStates = dynamicStates.data();
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.setViewports(viewport);
	viewportState.setScissors(scissor);

	vk::PipelineRasterizationStateCreateInfo rasterInfo;
	rasterInfo.depthClampEnable = false;
	rasterInfo.rasterizerDiscardEnable = false;
	rasterInfo.polygonMode = vk::PolygonMode::eFill;
	rasterInfo.lineWidth = 1.0f;
	rasterInfo.cullMode = vk::CullModeFlagBits::eBack;
	rasterInfo.frontFace = vk::FrontFace::eClockwise;
	rasterInfo.depthBiasClamp = false;

	vk::PipelineMultisampleStateCreateInfo multisample;
	multisample.sampleShadingEnable = false;
	multisample.rasterizationSamples = vk::SampleCountFlagBits::e1;

	vk::PipelineColorBlendAttachmentState colorblend;
	colorblend.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	colorblend.blendEnable = false;

	vk::PipelineColorBlendStateCreateInfo colorblendInfo;
	colorblendInfo.logicOp = vk::LogicOp::eCopy;
	colorblendInfo.logicOpEnable = false;
	colorblendInfo.setAttachments(colorblend);

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
	auto pipelineLayout = device->createPipelineLayoutUnique(pipelineLayoutInfo);

	vk::AttachmentDescription colorAttachment;
	colorAttachment.format = targetFormat.format;
	colorAttachment.samples = vk::SampleCountFlagBits::e1;
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
	colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
	colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
	colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

	vk::AttachmentReference colorAttachmentRef;
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::SubpassDescription subpass;
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.setColorAttachments(colorAttachmentRef);

	vk::SubpassDependency dependency;
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependency.srcAccessMask = vk::AccessFlagBits::eNone;
	dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

	vk::RenderPassCreateInfo renderPassInfo;
	renderPassInfo.setAttachments(colorAttachment);
	renderPassInfo.setSubpasses(subpass);
	renderPassInfo.setDependencies(dependency);
	auto renderPass = device->createRenderPassUnique(renderPassInfo);

	SwapchainResources swapchainResources;
	swapchainResources.Init(device, extent, surface, targetFormat, details.capabilities, targetMode, renderPass, targetDevice.value());

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	vk::GraphicsPipelineCreateInfo pipelineInfo;
	pipelineInfo.setStages(stages);
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterInfo;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = nullptr;
	pipelineInfo.pColorBlendState = &colorblendInfo;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = pipelineLayout.get();
	pipelineInfo.renderPass = renderPass.get();
	pipelineInfo.subpass = 0;

	auto graphicsPipeline = device->createGraphicsPipelineUnique(nullptr, pipelineInfo);
	if (graphicsPipeline.result != vk::Result::eSuccess) {
		std::cerr << "Failed to create graphics pipeline: " << graphicsPipeline.result << std::endl;
		return -1;
	}

	vk::DescriptorPoolSize poolSize;
	poolSize.type = vk::DescriptorType::eStorageBuffer;
	poolSize.descriptorCount = 10;
	vk::DescriptorPoolCreateInfo dpci;
	dpci.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	dpci.setPoolSizes(poolSize);
	dpci.maxSets = 1;
	auto descPool = device->createDescriptorPoolUnique(dpci);

	vk::DescriptorSetLayoutBinding dslb;
	dslb.descriptorCount = 3;
	dslb.descriptorType = vk::DescriptorType::eStorageBuffer;
	dslb.stageFlags = vk::ShaderStageFlagBits::eCompute;

	vk::DescriptorSetLayoutCreateInfo dslci;
	dslci.setBindings(dslb);
	auto descSetLayout = device->createDescriptorSetLayoutUnique(dslci);

	vk::PipelineLayoutCreateInfo plci;
	plci.setSetLayouts(descSetLayout.get());
	auto computePipelineLayout = device->createPipelineLayoutUnique(plci);

	vk::ComputePipelineCreateInfo cpci;
	cpci.stage = computeShaderStageInfo;
	cpci.layout = computePipelineLayout.get();
	auto computePipeline = device->createComputePipelineUnique(nullptr, cpci);
	if (computePipeline.result != vk::Result::eSuccess) {
		std::cerr << "Failed to create compute pipeline: " << computePipeline.result << std::endl;
		return -1;
	}

	vk::DescriptorSetAllocateInfo dsai;
	dsai.descriptorPool = descPool.get();
	dsai.setSetLayouts(descSetLayout.get());
	auto descSets = device->allocateDescriptorSetsUnique(dsai);

	StorageBuffer<uint32_t> bufA(device, targetDevice->device, { 0,1,2,3 });
	StorageBuffer<uint32_t> bufB(device, targetDevice->device, { 4,5,6,7 });
	StorageBuffer<uint32_t> bufC(device, targetDevice->device, { 0,0,0,0 });

	std::vector<vk::DescriptorBufferInfo> descBufferInfo = {
		vk::DescriptorBufferInfo(bufA.buffer.get(), 0, bufA.memorySize),
		vk::DescriptorBufferInfo(bufB.buffer.get(), 0, bufB.memorySize),
		vk::DescriptorBufferInfo(bufC.buffer.get(), 0, bufC.memorySize),
	};
	vk::WriteDescriptorSet wds;
	wds.setDstSet(descSets[0].get());
	wds.setDstBinding(0);
	wds.setDescriptorType(vk::DescriptorType::eStorageBuffer);
	wds.setDescriptorCount(3);
	wds.setBufferInfo(descBufferInfo);
	device->updateDescriptorSets(wds, nullptr);

	vk::CommandPoolCreateInfo poolInfo;
	poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
	poolInfo.queueFamilyIndex = targetDevice->graphicsIndex;
	auto commandPool = device->createCommandPoolUnique(poolInfo);

	constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
	vk::CommandBufferAllocateInfo cbai;
	cbai.commandPool = commandPool.get();
	cbai.level = vk::CommandBufferLevel::ePrimary;
	cbai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
	auto commandBuffers = device->allocateCommandBuffers(cbai);

	std::vector<ResourcePerFrame> frameResources(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		auto& rpf = frameResources[i];
		rpf.imageAvailable = device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
		rpf.renderFinished = device->createSemaphoreUnique(vk::SemaphoreCreateInfo());
		rpf.inFlight = device->createFenceUnique(vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
	}
	auto graphicsQueue = device->getQueue(targetDevice->graphicsIndex, 0);
	RECT rect;
	if (!GetWindowRect(hwnd.value(), &rect)) {
		std::cerr << "Failed to get window size" << std::endl;
		return -1;
	}
	EventDetector eventDetector(rect.right - rect.left, rect.bottom - rect.top);
	SetWindowLongPtr(hwnd.value(), GWLP_USERDATA, (LONG_PTR)&eventDetector);
	// メイン メッセージ ループ:
	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_VULKANSAMPLE));
	bool rendering = true;
	size_t numFrames = 0;
	while (rendering)
	{
		MSG msg;
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			switch (msg.message) {
			case WM_QUIT:
				rendering = false;
				break;
			}
			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else {
			Sleep(10);
			if (eventDetector.AreaIsZero()) {
				// Don't render when window area is zero
				continue;
			}
			else if (eventDetector.Resized()) {
				// Update buffer size based on new window size
				extent = eventDetector.Extent();
				std::cout << "Resizing swapchain to " << extent.width << "x" << extent.height << std::endl;
				device->waitIdle();
				swapchainResources.Cleanup();
				swapchainResources.Init(device, extent, surface, targetFormat, details.capabilities, targetMode, renderPass, targetDevice.value());
				eventDetector.ResetResize();
			}
			const size_t commandBufferIndex = numFrames % MAX_FRAMES_IN_FLIGHT;
			auto& cb = commandBuffers[commandBufferIndex];
			auto& rpf = frameResources[commandBufferIndex];
			// render
			auto result = device->waitForFences(rpf.inFlight.get(), true, UINT64_MAX);
			device->resetFences(rpf.inFlight.get());
			auto index = device->acquireNextImageKHR(swapchainResources.swapchain.get(), UINT64_MAX, rpf.imageAvailable.get(), nullptr);
			cb.reset();
			RecordCommandBuffer(cb, renderPass, swapchainResources.frameBuffers[index.value], extent, graphicsPipeline.value);
			vk::SubmitInfo submitInfo;
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = &rpf.imageAvailable.get();
			vk::PipelineStageFlags waitStages[] = { vk::PipelineStageFlagBits::eColorAttachmentOutput };
			submitInfo.pWaitDstStageMask = waitStages;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &cb;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &rpf.renderFinished.get();
			graphicsQueue.submit(submitInfo, rpf.inFlight.get());
			vk::PresentInfoKHR pi;
			pi.waitSemaphoreCount = 1;
			pi.pWaitSemaphores = &rpf.renderFinished.get();
			pi.swapchainCount = 1;
			pi.pSwapchains = &swapchainResources.swapchain.get();
			pi.pImageIndices = &index.value;
			result = presentQueue.presentKHR(pi);
			numFrames++;
		}
	}
	// Wait for idle before destroying resources since they may still be in use
	device->waitIdle();
	swapchainResources.Cleanup();
	return 0;
}



//
//  関数: MyRegisterClass()
//
//  目的: ウィンドウ クラスを登録します。
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VULKANSAMPLE));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_VULKANSAMPLE);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

//
//   関数: InitInstance(HINSTANCE, int)
//
//   目的: インスタンス ハンドルを保存して、メイン ウィンドウを作成します
//
//   コメント:
//
//        この関数で、グローバル変数でインスタンス ハンドルを保存し、
//        メイン プログラム ウィンドウを作成および表示します。
//
std::optional<HWND> InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // グローバル変数にインスタンス ハンドルを格納する

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return std::nullopt;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	return hWnd;
}

//
//  関数: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  目的: メイン ウィンドウのメッセージを処理します。
//
//  WM_COMMAND  - アプリケーション メニューの処理
//  WM_PAINT    - メイン ウィンドウを描画する
//  WM_DESTROY  - 中止メッセージを表示して戻る
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto detector = (EventDetector*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (message)
	{
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// 選択されたメニューの解析:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		// TODO: HDC を使用する描画コードをここに追加してください...
		EndPaint(hWnd, &ps);
	}
	break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_ENTERSIZEMOVE:
		if (detector) {
			detector->EnterResize();
		}
		break;
	case WM_SIZE:
	{
		if (detector) {
			switch (wParam) {
			case SIZE_RESTORED:
				detector->SetState(WindowState::RESTORED);
				break;
			case SIZE_MINIMIZED:
				detector->SetState(WindowState::MINIMIZED);
				break;
			case SIZE_MAXIMIZED:
				detector->SetState(WindowState::MAXIMIZED);
				break;
			}
			auto width = LOWORD(lParam);
			auto height = HIWORD(lParam);
			detector->UpdateSize(width, height);
		}
		break;
	}
	case WM_EXITSIZEMOVE:
	{
		if (detector) {
			detector->ExitResize();
		}
		break;
	}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// バージョン情報ボックスのメッセージ ハンドラーです。
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
