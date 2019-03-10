// #define OLD_STYLE_API

#include <stdlib.h>
#include <unistd.h>
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hid/IOHIDLib.h>

#if defined(OLD_STYLE_API)
#include <IOKit/hidsystem/IOHIDShared.h>
#include <IOKit/hidsystem/event_status_driver.h>
#endif

#include <string>
#include <vector>

// Copied from IOHIDFamily-870.21.4/IOHIDEventSystemPlugIns/IOHIDAccelerationTable.hpp

#define FIXED_TO_DOUBLE(x) ((x)/65536.0)

typedef struct {
    double   m;
    double   b;
    double   x;
} ACCEL_SEGMENT;

typedef struct {
    double  x;
    double  y;
} ACCEL_POINT;

struct ACCEL_TABLE_ENTRY {

    template<typename T>
    T acceleration () const;

    uint32_t count  () const;
    uint32_t length () const;

    template<typename T>
    T x  (unsigned int index) const;

    template<typename T>
    T y  (unsigned int index) const;

    ACCEL_POINT point (unsigned int) const;

private:

    uint32_t accel_;
    uint16_t count_;
    uint32_t points_[1][2];

} __attribute__ ((packed));


struct ACCEL_TABLE {

    template<typename T>
    T scale () const ;

    uint32_t count () const;

    uint32_t signature () const;

    const ACCEL_TABLE_ENTRY * entry (int index) const;

    friend std::ostream & operator<<(std::ostream &os, const ACCEL_TABLE& t);

private:

    uint32_t scale_;
    uint32_t signature_;
    uint16_t count_;
    ACCEL_TABLE_ENTRY entry_;

} __attribute__ ((packed));

#define APPLE_ACCELERATION_MT_TABLE_SIGNATURE       0x2a425355
#define APPLE_ACCELERATION_DEFAULT_TABLE_SIGNATURE  0x30303240

inline  int32_t ACCEL_TABLE_CONSUME_INT32 (const void ** t) {
    int32_t val = OSReadBigInt32(*t, 0);
    *t = (uint8_t*)*t + 4;
    return val;
}

inline  int16_t ACCEL_TABLE_CONSUME_INT16 (const void ** t) {
    int16_t val = OSReadBigInt16(*t, 0);
    *t = (uint8_t*)*t + 2;
    return val;
}

#define ACCELL_TABLE_CONSUME_POINT(t) {FIXED_TO_DOUBLE(ACCEL_TABLE_CONSUME_INT32(t)), \
    FIXED_TO_DOUBLE(ACCEL_TABLE_CONSUME_INT32(t))}

// Copied from IOHIDFamily-870.21.4/IOHIDEventSystemPlugIns/IOHIDAccelerationTable.cpp

template<>
IOFixed ACCEL_TABLE_ENTRY::acceleration<IOFixed> () const {
    return OSReadBigInt32(&accel_, 0);
}

template<>
double ACCEL_TABLE_ENTRY::acceleration<double> () const {
    return FIXED_TO_DOUBLE(acceleration<IOFixed>());
}

uint32_t ACCEL_TABLE_ENTRY::count () const {
    return OSReadBigInt16(&count_, 0);
}

template<>
IOFixed ACCEL_TABLE_ENTRY::x<IOFixed> (unsigned int index) const {
    return   OSReadBigInt32(&points_[index][0], 0);
}

template<>
double ACCEL_TABLE_ENTRY::x<double>(unsigned int index) const {
    return FIXED_TO_DOUBLE(x<IOFixed>(index));
}

template<>
IOFixed ACCEL_TABLE_ENTRY::y (unsigned int index) const {
    return  OSReadBigInt32(&points_[index][1], 0);
}

template<>
double ACCEL_TABLE_ENTRY::y (unsigned int index) const {
    return FIXED_TO_DOUBLE(y<IOFixed>(index));
}

ACCEL_POINT ACCEL_TABLE_ENTRY::point (unsigned int index) const {
    ACCEL_POINT result;
    result.x = x<double>(index);
    result.y = y<double>(index);
    return result;
}

uint32_t ACCEL_TABLE_ENTRY::length () const {
    return count() * sizeof(uint32_t) * 2 + sizeof (uint32_t) + sizeof(uint16_t) ;
}

template<>
IOFixed ACCEL_TABLE::scale () const {
    return OSReadBigInt32(&scale_, 0);
}

template<>
double ACCEL_TABLE::scale () const {
    return FIXED_TO_DOUBLE(scale<IOFixed>());
}

uint32_t ACCEL_TABLE::count () const {
    return OSReadBigInt16(&count_, 0);
}

uint32_t ACCEL_TABLE::signature() const {
    return signature_;
}

const ACCEL_TABLE_ENTRY * ACCEL_TABLE::entry (int index) const {
    const ACCEL_TABLE_ENTRY *current = &entry_;
    for (int i = 1 ; i <= index; i++) {
        current = (ACCEL_TABLE_ENTRY*)((uint8_t*)current + current->length());
    }
    return current;
}


struct Device {
    IOHIDDeviceRef hidDevice;
    std::string name;
};

IOHIDManagerRef hidManager;
std::vector<Device> devices;
int iohidDeltaX = 0;
int iohidDeltaY = 0;

std::string cfToString(CFStringRef str)
{
    char buf[5000];
    std::string ret;
    if (!CFStringGetCString(str, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        printf("Failed to CFStringGetCString\n");
        abort();
    }
    CFRelease(str);
    return buf;
}

#if defined(OLD_STYLE_API)
void printOldAcceleration()
{
    double mouseAccel, trackpadAccel;
    io_connect_t handle = NXOpenEventStatus();
    if (IOHIDGetAccelerationWithKey(handle, CFSTR(kIOHIDMouseAccelerationType), &mouseAccel) != 0) {
        printf("Failed to IOHIDGetAccelerationWithKey\n");
        abort();
    }
    if (IOHIDGetAccelerationWithKey(handle, CFSTR(kIOHIDTrackpadAccelerationType),
                                    &trackpadAccel) != 0) {
        printf("Failed to IOHIDGetAccelerationWithKey\n");
        abort();
    }
    printf("Old-style acceleration: mouse %g, trackpad %g\n", mouseAccel, trackpadAccel);
    NXCloseEventStatus(handle);
}
#endif

void printDeviceImpl(io_service_t service, int padding)
{
    std::string strPadding(padding, ' ');
    char classname[1000];
    if (IOObjectGetClass(service, classname) != 0) {
        printf("Failed to IOObjectGetClass\n");
        abort();
    }
    
    CFMutableDictionaryRef properties;
    if (IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault,
                                          kNilOptions) != 0) {
        printf("Failed to IORegistryEntryCreateCFProperties\n");
        abort();
    }

    std::vector<std::string> keys;
    CFDictionaryApplyFunction(properties, [](const void* key, const void* value, void* context) {
        ((std::vector<std::string>*)context)->push_back(cfToString((CFStringRef)key));
    }, &keys);
    
    printf("%sClass: %s\n", strPadding.c_str(), classname);
    for (const auto& key : keys) {
        printf("%sProperty: %s\n", strPadding.c_str(), key.c_str());
    }
    printf("\n");
    
    io_iterator_t children;
    IORegistryEntryGetChildIterator(service, kIOServicePlane, &children);
    io_object_t child;
    while ((child = IOIteratorNext(children)) != MACH_PORT_NULL) {
        printDeviceImpl(child, padding + 1);
        IOObjectRelease(child);
    }
    IOObjectRelease(children);
}

void printDevice(IOHIDDeviceRef device)
{
    printDeviceImpl(IOHIDDeviceGetService(device), 0);
}

// Taken from libpointing
int getResolutionImpl(io_service_t service)
{
    CFTypeRef res = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane,CFSTR(kIOHIDPointerResolutionKey), kCFAllocatorDefault,
                kIORegistryIterateRecursively);
    if (res) {
        SInt32 resolution = -1;
        if (CFGetTypeID(res) == CFNumberGetTypeID()
                && CFNumberGetValue((CFNumberRef)res,kCFNumberSInt32Type, &resolution)) {
            resolution = resolution >> 16;
        }
        return resolution;
    } else {
        printf("No %s key found\n", kIOHIDPointerResolutionKey);
    }
    return -1;
}

int getResolution(IOHIDDeviceRef device)
{
    return getResolutionImpl(IOHIDDeviceGetService(device));
}

bool setResolutionImpl(io_service_t service, int resolution)
{
    if (service == MACH_PORT_NULL) {
        return false;
    }

    CFTypeRef res = IORegistryEntryCreateCFProperty(service, CFSTR(kIOHIDPointerResolutionKey),
                                                    kCFAllocatorDefault, kNilOptions);
    if (res) {
        SInt32 i32Resolution = resolution << 16;
        CFNumberRef numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type,
                                               &i32Resolution);
        if (IORegistryEntrySetCFProperty(
                    service, CFSTR(kIOHIDPointerResolutionKey), numberRef) != 0) {
            printf("Failed to IORegistryEntrySetCFProperty\n");
            abort();
        }
        CFRelease(numberRef);
        return true;
    }

    io_iterator_t children;
    IORegistryEntryGetChildIterator(service, kIOServicePlane, &children);
    bool result = false;
    while (true) {
        io_object_t child = IOIteratorNext(children);
        if (child == MACH_PORT_NULL) {
            break;
        }
        if (setResolutionImpl(child, resolution)) {
            result = true;
            break;
        }
        IOObjectRelease(child);
    }
    IOObjectRelease(children);

    return result;
}

bool setResolution(IOHIDDeviceRef device, int resolution)
{
    return setResolutionImpl(IOHIDDeviceGetService(device), resolution);
}

double getAccelerationImpl(io_service_t service)
{
    CFTypeRef res = IORegistryEntrySearchCFProperty(
                service, kIOServicePlane, CFSTR(kIOHIDPointerAccelerationTableKey),
                kCFAllocatorDefault, kIORegistryIterateRecursively);
    if (res) {
        ACCEL_TABLE* table = (ACCEL_TABLE*)CFDataGetBytePtr((CFDataRef)res);
        if (table->signature() != APPLE_ACCELERATION_MT_TABLE_SIGNATURE
                && table->signature() != APPLE_ACCELERATION_MT_TABLE_SIGNATURE) {
            printf("Wrong accel table signature %x\n", table->signature());
            return -1;
        }

        // Something something modify accel tables;

        return -1;
    } else {
        printf("No %s key found\n", kIOHIDPointerAccelerationTableKey);
    }
    return -1;
}

double getAcceleration(IOHIDDeviceRef device)
{
    return getAccelerationImpl(IOHIDDeviceGetService(device));
}

int32_t getIOHIDInt32Property(IOHIDDeviceRef device, CFStringRef prop)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(device, prop);
    if (!ref) {
        printf("Failed to IOHIDDeviceGetProperty\n");
        abort();
    }
    int32_t ret;
    if (!CFNumberGetValue((CFNumberRef)ref, kCFNumberSInt32Type, &ret)) {
        printf("Failed to CFNumberGetValue\n");
        abort();
    }
    return ret;
}

std::string getIOHIDStringProperty(IOHIDDeviceRef device, CFStringRef prop, bool allowNull)
{
    CFStringRef str = (CFStringRef)IOHIDDeviceGetProperty(device, prop);
    if (!str) {
        printf("Failed to IOHIDDeviceGetProperty\n");
        if (allowNull) {
            return nullptr;
        } else {
            abort();
        }
    }
    CFRetain(str);
    return cfToString(str);
}

void iohidInputCallback(void* /*context*/, IOReturn /*res*/, void* /*sender*/, IOHIDValueRef val)
{
    printf("FIRED\n");
    IOHIDElementRef elem = IOHIDValueGetElement(val);
    CFIndex value = IOHIDValueGetIntegerValue(val);
    uint32_t page = IOHIDElementGetUsagePage(elem);
    uint32_t usage = IOHIDElementGetUsage(elem);
    if (page == kHIDPage_GenericDesktop && value != 0) {
        switch (usage) {
            case kHIDUsage_GD_X:
                iohidDeltaX += (int)value;
                break;
            case kHIDUsage_GD_Y:
                iohidDeltaY += (int)value;
                break;
            default:
                break;
        }
    }
}

void initIOHID()
{
    hidManager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (hidManager == nullptr) {
        printf("Failed to IOHIDManagerCreate\n");
        abort();
    }
    IOHIDManagerSetDeviceMatching(hidManager, NULL);
    IOHIDManagerOpen(hidManager, kIOHIDOptionsTypeNone);
    IOHIDManagerRegisterInputValueCallback(hidManager, iohidInputCallback, nullptr);
    IOHIDManagerScheduleWithRunLoop(hidManager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFSetRef deviceSet = IOHIDManagerCopyDevices(hidManager);
    CFIndex num = CFSetGetCount(deviceSet);
    IOHIDDeviceRef* hidDevices = new IOHIDDeviceRef[num];
    CFSetGetValues(deviceSet, (const void**)hidDevices);
    for (int i = 0; i < num; i++) {
        IOHIDDeviceRef hidDevice = hidDevices[i];
        int32_t usagePage = getIOHIDInt32Property(hidDevice, CFSTR(kIOHIDPrimaryUsagePageKey));
        int32_t usage = getIOHIDInt32Property(hidDevice, CFSTR(kIOHIDPrimaryUsageKey));
        std::string manufacturer = getIOHIDStringProperty(hidDevice, CFSTR(kIOHIDManufacturerKey),
                                                          true);
        std::string product = getIOHIDStringProperty(hidDevice, CFSTR(kIOHIDProductKey), true);
        printf("Device: %d: %s: %s (%d %d)\n", i, manufacturer.c_str(), product.c_str(), usagePage,
               usage);
        printDevice(hidDevice);
        if (usagePage == kHIDPage_GenericDesktop && usage == kHIDUsage_GD_Mouse) {
            printf("Got mouse device\n");
            if (IOHIDDeviceOpen(hidDevice, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
                printf("Failed to IOHIDDeviceOpen\n");
                continue;
            }
            IOHIDDeviceRegisterInputValueCallback(hidDevice, iohidInputCallback, nullptr);
            IOHIDDeviceScheduleWithRunLoop(hidDevice, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            IOHIDQueueRef queue = IOHIDQueueCreate(kCFAllocatorDefault, hidDevice, 1024,
                                                   kIOHIDOptionsTypeNone);
            Device device;
            device.hidDevice = hidDevice;
            device.name = manufacturer;
            device.name += " ";
            device.name += product;
            devices.push_back(device);
        }
    }
}

void processIOHID()
{
    while (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) == kCFRunLoopRunHandledSource);
}

CGPoint getCurrentCgPoint()
{
    CGEventRef cgEvent = CGEventCreate(NULL);
    CGPoint cgPoint = CGEventGetLocation(cgEvent);
    CFRelease(cgEvent);
    return cgPoint;
}

// Very simple class for computing amortized averages (the average of the last N elements).
template<typename T>
class RollingAverage
{
public:
    RollingAverage(size_t size)
        : mBuffer(new T[size]),
          mSize(size),
          mCounter(0),
          mItems(0)
    {
    }
    
    ~RollingAverage()
    {
        delete[] mBuffer;
    }
    
    void push(T t)
    {
        mBuffer[mCounter % mSize] = t;
        mCounter++;
        if (mItems < mSize) {
            mItems++;
        }
    }
    
    T average() const
    {
        T avg = T();
        if (mItems > 0) {
            for (size_t i = 0; i < mItems; i++) {
                avg += mBuffer[i];
            }
            avg /= mItems;
        }
        return avg;
    }
    
private:
    T* mBuffer;
    size_t mSize;
    size_t mCounter;
    size_t mItems;
};

int main(int argc, char** argv)
{
#if defined(OLD_STYLE_API)
    printOldAcceleration();
#endif

    initIOHID();

    for (const Device& device: devices) {
        printf("%s resolution: %d\n", device.name.c_str(), getResolution(device.hidDevice));
        printf("%s acceleration: %g\n", device.name.c_str(), getAcceleration(device.hidDevice));
    }

    FILE* speedDump = nullptr;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--set-resolution") == 0) {
            const char* name = argv[i + 1];
            int resolution = atoi(argv[i + 2]);
            for (const Device& device: devices) {
                if (device.name.find_first_of(name) != std::string::npos) {
                    printf("Setting resolution for %s to %d\n", device.name.c_str(), resolution);
                    if (!setResolution(device.hidDevice, resolution)) {
                        printf("Setting resolution failed\n");
                    }
                }
            }
            i += 2;
        } else if (strcmp(argv[i], "--dump") == 0) {
            speedDump = fopen(argv[i + 1], "wt");
            printf("Dumping speeds to %s\n", argv[i + 1]);
            i++;
        }
    }

    CGPoint lastCgPoint = getCurrentCgPoint();
    RollingAverage<float> iohidSpeed(5);
    RollingAverage<float> cgSpeed(5);
    while (true) {
        bool movement = false;
        
        iohidDeltaX = 0;
        iohidDeltaY = 0;
        processIOHID();
        if (iohidDeltaX != 0 || iohidDeltaY != 0) {
            printf("IOHID Delta: %d %d\n", iohidDeltaX, iohidDeltaY);
            iohidSpeed.push(sqrt(iohidDeltaX * iohidDeltaX + iohidDeltaY * iohidDeltaY));
            movement = true;
        }
        
        CGPoint cgPoint = getCurrentCgPoint();
        float cgDeltax = cgPoint.x - lastCgPoint.x;
        float cgDeltay = cgPoint.y - lastCgPoint.y;
        if (cgDeltax != 0 || cgDeltay != 0) {
            printf("CG deltas: %d %d (%g %g)\n", (int)round(cgDeltax), (int)round(cgDeltay),
                   cgDeltax, cgDeltay);
            lastCgPoint = cgPoint;
            cgSpeed.push(sqrt(cgDeltax * cgDeltax + cgDeltay * cgDeltay));
            movement = true;
        }
        
        if (movement) {
            printf("IOHID speed: %g, CG speed: %g\n", iohidSpeed.average(), cgSpeed.average());
            if (speedDump != nullptr) {
                fprintf(speedDump, "%g %g\n", iohidSpeed.average(), cgSpeed.average());
                fflush(speedDump);
            }
            printf("\n");
        }
        usleep(1000);
    }
}
