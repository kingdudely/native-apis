#pragma once

#include <IOKit/hidsystem/IOLLEvent.h>
#include <mach/mach_types.h>

extern "C" {
	kern_return_t IOHIDPostEvent(io_connect_t connect, UInt32 eventType, IOGPoint location, const NXEventData *eventData, UInt32 eventDataVersion, IOOptionBits eventFlags, IOOptionBits options);
}

// Thin wrapper around IOHIDPostEvent: builds a zeroed NXEventData, hands it
// to the caller to fill in via `fill`, and posts it. Callers only specify
// the event type and the union member(s) they care about.
template <typename Fill>
inline void PostNXEvent(io_connect_t conn, UInt32 eventType, Fill&& fill,
                         IOOptionBits eventFlags = 0, IOOptionBits options = 0,
                         IOGPoint loc = {0, 0}) {
    NXEventData ev = {};
    fill(ev);
    IOHIDPostEvent(conn, eventType, loc, &ev, kNXEventDataVersion, eventFlags, options);
}