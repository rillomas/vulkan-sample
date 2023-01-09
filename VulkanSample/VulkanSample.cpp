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

static vk::Instance CreateInstance(const char* appName, std::vector<const char*>& layers, std::vector<const char*> extensions) {
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
	return vk::createInstance(createInfo);
}

void CreateConsole() {
	FILE* fp;
	AllocConsole();
	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);
}

template<class T>
static bool contains(std::vector<T> list, T target) {
	return std::find(list.begin(), list.end(), target) != list.end();
}

struct SwapChainSupportDetails {
	vk::SurfaceCapabilitiesKHR capabilities;
	std::vector<vk::SurfaceFormatKHR> formats;
	std::vector<vk::PresentModeKHR> presentModes;

	bool SwapChainAdequate(vk::SurfaceFormatKHR targetFormat, vk::PresentModeKHR targetMode) const {
		return contains(formats, targetFormat) && contains(presentModes, targetMode);
	}

};

vk::Extent2D ChooseSwapExtent(vk::SurfaceCapabilitiesKHR capabilities) {
	return capabilities.currentExtent;
}

uint32_t ChooseImageCount(vk::SurfaceCapabilitiesKHR capabilities) {
	auto imgCount = capabilities.minImageCount + 1;
	if (capabilities.maxImageCount > 0 && imgCount > capabilities.maxImageCount) {
		imgCount = capabilities.maxImageCount;
	}
	return imgCount;
}

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

	SwapChainSupportDetails GetSwapChainSupportDetails(vk::SurfaceKHR& surface) const {
		SwapChainSupportDetails details = {
			device.getSurfaceCapabilitiesKHR(surface),
			device.getSurfaceFormatsKHR(surface),
			device.getSurfacePresentModesKHR(surface)
		};
		return details;
	}
};

std::optional<DeviceAndIndex> GetSufficientDevice(vk::Instance& instance, vk::SurfaceKHR& surface, const std::vector<const char*>& requiredExtensions) {
	auto devices = instance.enumeratePhysicalDevices();
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
			if (d.getSurfaceSupportKHR(index, surface)) {
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

vk::ShaderModule CreateShaderModule(vk::Device& device, const std::vector<uint8_t>& code) {
	vk::ShaderModuleCreateInfo shaderInfo;
	shaderInfo.codeSize = (uint32_t)code.size();
	shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
	return device.createShaderModule(shaderInfo);
}

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
	auto surface = instance.createWin32SurfaceKHR(win32info);

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
	auto details = targetDevice->GetSwapChainSupportDetails(surface);
	auto targetFormat = vk::SurfaceFormatKHR(vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear);
	auto targetMode = vk::PresentModeKHR::eFifo;
	if (!details.SwapChainAdequate(targetFormat, targetMode)) {
		std::cerr << "Could not find sufficient swap chain" << std::endl;
		return false;
	}
	vk::PhysicalDeviceFeatures deviceFeature;
	vk::DeviceCreateInfo info;
	info.pQueueCreateInfos = qInfoList.data();
	info.queueCreateInfoCount = (uint32_t)qInfoList.size();
	info.pEnabledFeatures = &deviceFeature;
	info.enabledLayerCount = (uint32_t)actualLayers.size();
	info.ppEnabledLayerNames = actualLayers.data();
	info.enabledExtensionCount = (uint32_t)deviceExtensions.size();
	info.ppEnabledExtensionNames = deviceExtensions.data();
	auto device = targetDevice->device.createDevice(info);
	auto presentQueue = device.getQueue(targetDevice->presentIndex, 0);
	auto extent = ChooseSwapExtent(details.capabilities);

	vk::SwapchainCreateInfoKHR chainInfo;
	chainInfo.surface = surface;
	chainInfo.minImageCount = ChooseImageCount(details.capabilities);
	chainInfo.imageFormat = targetFormat.format;
	chainInfo.imageColorSpace = targetFormat.colorSpace;
	chainInfo.imageExtent = extent;
	chainInfo.imageArrayLayers = 1;
	chainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	std::vector<uint32_t> queueFamilyIndices = { targetDevice->graphicsIndex, targetDevice->presentIndex };
	if (targetDevice->graphicsIndex == targetDevice->presentIndex) {
		chainInfo.imageSharingMode = vk::SharingMode::eExclusive;
		chainInfo.queueFamilyIndexCount = 0;
		chainInfo.pQueueFamilyIndices = nullptr;
	}
	else {
		chainInfo.imageSharingMode = vk::SharingMode::eConcurrent;
		chainInfo.queueFamilyIndexCount = (uint32_t)queueFamilyIndices.size();
		chainInfo.pQueueFamilyIndices = queueFamilyIndices.data();
	}
	chainInfo.preTransform = details.capabilities.currentTransform;
	chainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	chainInfo.presentMode = targetMode;
	chainInfo.clipped = true;
	chainInfo.oldSwapchain = nullptr;
	auto swapchain = device.createSwapchainKHR(chainInfo);

	auto swapchainImages = device.getSwapchainImagesKHR(swapchain);
	std::vector<vk::ImageView> imageViews(swapchainImages.size());
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
		imageViews[i] = device.createImageView(ci);
	}

	// Create shaders
	auto fragmentCode = ::ReadFile("fragment.spv");
	auto vertexCode = ::ReadFile("vertex.spv");

	auto fragment = CreateShaderModule(device, fragmentCode);
	auto vertex = CreateShaderModule(device, vertexCode);

	vk::PipelineShaderStageCreateInfo vertShaderStageInfo;
	vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
	vertShaderStageInfo.module = vertex;
	vertShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo fragShaderStageInfo;
	fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
	fragShaderStageInfo.module = fragment;
	fragShaderStageInfo.pName = "main";

	vk::PipelineShaderStageCreateInfo stages[] = { vertShaderStageInfo, fragShaderStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;

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
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

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

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
	auto pipelineLayout = device.createPipelineLayout(pipelineLayoutInfo);




	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_VULKANSAMPLE));

	MSG msg;

	// メイン メッセージ ループ:
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	device.destroyShaderModule(fragment);
	device.destroyShaderModule(vertex);
	for (auto& iv : imageViews) {
		device.destroyImageView(iv);
	}
	device.destroySwapchainKHR(swapchain);
	device.destroy();
	instance.destroySurfaceKHR(surface);
	instance.destroy();

	return (int)msg.wParam;
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
