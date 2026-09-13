#pragma once

#include <IOKit/hidsystem/IOHIDLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>

inline io_connect_t GetHIDConnect() {
	static io_connect_t hidConnect = MACH_PORT_NULL;

	if (hidConnect != MACH_PORT_NULL) {
		return hidConnect;
	}

	io_service_t service = IOServiceGetMatchingService(
		kIOMainPortDefault, IOServiceMatching(kIOHIDSystemClass));
	if (service == MACH_PORT_NULL) {
		return MACH_PORT_NULL;
	}

	io_connect_t connect = MACH_PORT_NULL;
	kern_return_t kr = IOServiceOpen(service, mach_task_self(),
									kIOHIDParamConnectType, &connect);
	IOObjectRelease(service);

	if (kr == KERN_SUCCESS) {
		hidConnect = connect;
	}

	return hidConnect;
}