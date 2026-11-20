//
//  VoodooI2CServices.cpp
//  VoodooI2CServices
//
//  Created by Alexandre on 30/09/2017.
//  Copyright © 2017 Alexandre Daoud. All rights reserved.
//

#include "VoodooI2CServices.hpp"

const IORegistryPlane* VoodooI2CServices::gVoodooI2CPlane = NULL;

#define super IOService
OSDefineMetaClassAndStructors(VoodooI2CServices, IOService);

bool VoodooI2CServices::attachDevice(void* target, void* ref_con, IOService* new_service, IONotifier* notifier) {
    OSBoolean* bool_obj = OSDynamicCast(OSBoolean, new_service->getProperty("isI2CController"));
    if (bool_obj && bool_obj->isTrue()) {
        new_service->attachToParent(getRegistryRoot(), gVoodooI2CPlane);
    } else {
        IORegistryEntry* parent = new_service->getParentEntry(gIOServicePlane);
        new_service->attachToParent(parent, gVoodooI2CPlane);
    }

    return true;
}

bool VoodooI2CServices::detachDevice(void* target, void* ref_con, IOService* new_service, IONotifier* notifier) {
    IORegistryEntry* parent = new_service->getParentEntry(gVoodooI2CPlane);
    new_service->detachFromParent(parent, gVoodooI2CPlane);
    return true;
}

bool VoodooI2CServices::start(IOService* provider) {
    if (!super::start(provider))
        return false;

    gVoodooI2CPlane = IORegistryEntry::makePlane(kVoodooI2CPlane);

    attachToParent(getRegistryRoot(), gVoodooI2CPlane);

    OSDictionary*  property_match = OSDictionary::withCapacity(1);
    OSDictionary* service_match = OSDictionary::withCapacity(1);

    service_match->setObject("VoodooI2CServices Supported", kOSBooleanTrue);
    property_match->setObject(gIOPropertyMatchKey, service_match);

    device_matcher = addMatchingNotification(gIOMatchedNotification, property_match, VoodooI2CServices::attachDevice, this, NULL, 0);
    terminate_matcher = addMatchingNotification(gIOTerminatedNotification, property_match, VoodooI2CServices::detachDevice, this, NULL, 0);

    service_match->release();
    property_match->release();

    registerService();

    return true;
}

void VoodooI2CServices::stop(IOService* provider) {
    /*
     * xe3-macos: fix an upstream use-after-free (verified by kernel panic in
     * the macOS 26 VM, reproducible on every unload).
     *
     * IONotifier::remove() is documented (MacKernelSDK IOKit/IONotifier.h:50-57)
     * as "Removes the notification request and releases it. Since creating an
     * IONotifier instance will leave it with a retain count of one, creating an
     * IONotifier and then removing it will destroy it."
     *
     * So after remove() the object is already deallocated and our own reference
     * is gone. The original code then did OSSafeReleaseNULL(device_matcher),
     * which dereferences the freed pointer to fetch its vtable:
     *
     *   panic(cpu 0): Kernel trap, type = 14=page fault
     *   CR2: 0x0000000000000028
     *   VoodooI2CServices : VoodooI2CServices::stop(IOService*) + 0x39
     *
     * +0x39 is the callq *0x28(%rax) of the release; the vtable slot read back
     * 0 because the freed memory had been zeroed, hence the fault at 0x28.
     *
     * Correct sequence: call remove() once, then just NULL the pointers --
     * never release them again. Both calls are NULL-guarded because stop()
     * must tolerate a partially started driver (if addMatchingNotification()
     * fails, the corresponding member is still NULL).
     */
    if (device_matcher) {
        device_matcher->remove();
        device_matcher = NULL;
    }
    if (terminate_matcher) {
        terminate_matcher->remove();
        terminate_matcher = NULL;
    }

    if (gVoodooI2CPlane)
        detachFromParent(getRegistryRoot(), gVoodooI2CPlane);

    super::stop(provider);
}
