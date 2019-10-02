#include <Core/DeviceManager.hpp>
#include <Util/Util.hpp>

using namespace std;

DeviceManager::DeviceManager() : mInstance(VK_NULL_HANDLE), mAssetDatabase(nullptr), mGLFWInitialized(false), mMaxFramesInFlight(0) {}
DeviceManager::~DeviceManager() {
	safe_delete(mAssetDatabase);

	if (mGLFWInitialized) glfwTerminate();

	for (auto& w : mWindows)
		safe_delete(w);

	for (auto& d : mDevices) {
		d->FlushCommandBuffers();
		safe_delete(d);
	}

	vkDestroyInstance(mInstance, nullptr);
}

VkPhysicalDevice DeviceManager::GetPhysicalDevice(uint32_t index, const vector<const char*>& extensions) const {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);

	if (deviceCount == 0) return VK_NULL_HANDLE;

	vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data());

	if (index >= deviceCount) return VK_NULL_HANDLE;

	// 'index' represents the index of the SUITABLE devices, count the suitable devices
	uint32_t i = 0;
	for (const auto& device : devices) {
		if (CheckDeviceExtensionSupport(device, extensions)) {
			if (i == index) return device;
			i++;
		}
	}
	return VK_NULL_HANDLE;
}

void DeviceManager::CreateInstance() {
	if (mInstance != VK_NULL_HANDLE)
		vkDestroyInstance(mInstance, nullptr);

	if (!mGLFWInitialized) {
		if (glfwInit() == GLFW_FALSE) {
			const char* msg;
			glfwGetError(&msg);
			printf("Failed to initialize GLFW! %s\n", msg);
			throw runtime_error("Failed to initialize GLFW!");
		}
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		mGLFWInitialized = true;
		printf("Initialized glfw.\n");
	}

	vector<const char*> instanceExtensions {
		#ifdef _DEBUG
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME
		#endif
	};
	
	// request GLFW extensions
	uint32_t glfwExtensionCount = 0;
	printf("Requesting glfw extensions... ");
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	printf("Done.\n");
	for (uint32_t i = 0; i < glfwExtensionCount; i++)
		instanceExtensions.push_back(glfwExtensions[i]);

	vector<const char*> validationLayers {
		#ifdef _DEBUG
		"VK_LAYER_KHRONOS_validation"
		#endif
	};


	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
	vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	set<const char*> availableLayerSet;
	for (const VkLayerProperties& layer : availableLayers)
		availableLayerSet.insert(layer.layerName);

	for (auto& it = validationLayers.begin(); it != validationLayers.end();) {
		if (!availableLayerSet.count(*it)) {
			printf("Removing unsupported layer: %s\n", *it);
			it = validationLayers.erase(it);
		} else
			it++;
	}

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "VkCAVE";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_1;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
	createInfo.ppEnabledExtensionNames = instanceExtensions.data();
	createInfo.enabledLayerCount = (uint32_t)validationLayers.size();
	createInfo.ppEnabledLayerNames = validationLayers.data();
	printf("Creating vulkan instance... ");
	ThrowIfFailed(vkCreateInstance(&createInfo, nullptr, &mInstance));
	printf("Done.\n");
}

void DeviceManager::Initialize(const vector<DisplayCreateInfo>& displays) {
	vector<const char*> deviceExtensions {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME // needed to obtain a swapchain
	};

	vector<const char*> validationLayers {
		#ifdef _DEBUG
		"VK_LAYER_KHRONOS_validation",
		"VK_LAYER_RENDERDOC_Capture",
		#endif
	};

	uint32_t minImageCount = ~0;

	// create windows
	for (const auto& it : displays) {
		uint32_t device = it.mDevice;
		VkPhysicalDevice physicalDevice = GetPhysicalDevice(device, deviceExtensions);
		while (physicalDevice == VK_NULL_HANDLE) {
			// requested device index does not exist or is not suitable, fallback to previous device
			if (device == 0) throw runtime_error("Failed to find suitable device!");
			device--;
			physicalDevice = GetPhysicalDevice(device, deviceExtensions);
		}
		if (physicalDevice == VK_NULL_HANDLE) continue; // could not find any suitable devices...

		if ((uint32_t)mDevices.size() <= device) mDevices.resize((size_t)device + 1);

		auto w = new Window(mInstance, "VkCAVE " + to_string(mWindows.size()), it.mWindowPosition, it.mMonitor);
		if (!mDevices[device]) mDevices[device] = new Device(mInstance, deviceExtensions, validationLayers, w->Surface(), physicalDevice, device);
		w->CreateSwapchain(mDevices[device]);
		minImageCount = gmin(minImageCount, w->mImageCount);
		mWindows.push_back(w);
	}

	for (const auto& device : mDevices)
		device->mMaxFramesInFlight = minImageCount;

	mAssetDatabase = new ::AssetDatabase(this);
}

bool DeviceManager::PollEvents() const {
	for (const auto& w : mWindows)
		if (w->ShouldClose())
			return false;
	glfwPollEvents();
	return true;
}